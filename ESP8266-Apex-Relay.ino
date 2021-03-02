// ESP Code for relay module with stock MCU firmware
// http://www.icstation.com/esp8266-wifi-channel-relay-module-remote-control-switch-wireless-transmitter-smart-home-p-13420.html
// Wifi, apex login, and outlet name is set from the RelayConfig wifi on first boot
// Connect to http://192.168.1.1/ on RelayConfig
// Polls apex every 10s by default, Adjustable on RelayConfig wifi.
//

// Disclaimer
// All work product by Developer is provided ​“AS IS”. 
// Developer makes no other warranties, express or implied, and hereby disclaims all implied warranties, including any warranty of merchantability and warranty of fitness for a particular purpose.
// This software is experimental and unaffiliated with Neptune Systems, Espressif, or LC Technologies. End user assumes all risks, burden of maintence, and is responsible for assuring compliance with other entities.
//

// Notes
// Blue light
//   If your relay board lights up solid blue on startup it is in the wrong mode.
//   Unplug the relay, plug back in while holding down the s2 button. This should change modes.
// Can't connect to RelayConfig
//   Power cycle the device and try again. First boot has a known issue.
//   Connect to 192.168.1.1

#include "ESP8266WiFi.h" // Can not use actual http client due to noncompliant apex spec. Using raw tcp client.
#include "ESP8266WebServer.h"
#include <mDNSResolver.h>       //https://github.com/madpilot/mDNSResolver
#include <ESP8266HTTPClient.h>
#include <base64.h>

String NAME_TO_RESOLVE;  // Stores the domain name of the apex
int NUMBER_OF_RELAYS; // Stores the count of relays
int APEX_POLL_DELAY; // Stores the time between apex polling in secconds

IPAddress ServerIP (0, 0, 0, 0); // Stores the apex ip address
unsigned long lastResolved = 0; // Stores the time apex was last resolved.
bool resolved = false; // Indicates the apex is resolving

using namespace mDNSResolver;
WiFiUDP udp;
Resolver resolver(udp);

//Settings buffers
String login;
String password;
String ap;
String appassword;
int apmode;
String myoutletname;
String errorMode;

// Storing states
bool mcuRelayState[] = {false, false, false, false}; // State sent to the mcu from the esp
bool apexRelayControlState[] = {false, false, false, false}; // State requested by the apex
bool apexRelayVerifyState[] = {false, false, false, false}; // State sent to the apex to confirm relay has flipped
bool apexRelayError = false; // Error indicator on apex side for communication failure
// Storing creation flags
bool apexRelayControlCreated[] = {false, false, false, false}; // Stores flags indicating the virtual control outlets have been created
bool apexRelayVerifyCreated[] = {false, false, false, false}; // Stores flags indicating the virtual verify outlets have been created
bool apexRelayABCreated[] = {false, false, false, false}; // Stores flags indicating the virtual comparitor AB outlets have been created (for extreme error methods)
bool apexRelayBACreated[] = {false, false, false, false}; // Stores flags indicating the virtual comparitor BA outlets have been created (for extreme error methods)
bool apexRelayErrorCreated = false; // Stores a flag indicating the error outlet has been created on the apex side (used for extreme error methods)


long apexLastSync = 0; // Tracks our last connection time with apex. Used to re-poll every X secconds


int apexLoopsMissed = 10; // Count of how many times we have failled to communicate with the apex. Will stop MCU heartbeat if too high. // Setting to 10 means heartbeats will not heappen before apex is contacted on boot
bool triggerFullResync = false; // Flag used to trigger a force resync of the MCU with our relay state. Used after connection failure.


// Used to send commands to the MCU
byte relayMessage[] = {0xA0, 0x02, 0x01, 0xA3 }; // Buffer to send relay command
int count = 0; // Byte counter

unsigned long relaySent = 0; // Time relay message was sent. Used to resend if timed out and prevent flooding

unsigned long wifiLastSeen = 0; // Time wifi was last seen


bool initializing = false; // Flag to indicate we are still initializing ( need user input on open web server )

const IPAddress apIp(192, 168, 1, 1); // Open web server / AP ip address


ESP8266WebServer server(80); // Start a web server for configuration

// CONFIG ROUTINES
/// Write config to file (string)
void writeConfig(String file, String data){
  File f;
  f = SPIFFS.open("/" + file, "w");
  f.print(data);
  f.close();
}

/// Write config to file (int)
void writeConfig(String file, int data){
  writeConfig(file, (String) data);
}
/// Write config to file (long)
void writeConfig(String file, long data){
  writeConfig(file, (String) data);
}
/// Read config from file (string)
String readConfig(String file){
  File f;
  f = SPIFFS.open("/" + file, "r");
  String result = f.readString();
  f.close();
  return result;
}
/// Read config from file (long)
long readConfigLong(String file){
  return readConfig(file).toInt();
}

// Setup routine used before properly configured
void initConfigSetup(){
  initializing = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP("RelayConfig", nullptr, 1);
  
  server.on("/", configWebServerIndex);
  server.on("/configure", configWebServerConfig);
  server.begin(); //Start the server
}


// Web page used to configure settings
void configWebServerIndex() { //Handler for the web server index page
  String message = "<html><body>";
  message += "<form action=\"/configure\" method=\"post\">";
  message += "WIFI NAME: <input type=\"text\" name=\"apname\" id=\"apname\" value=\"\"><br>";
  message += "WIFI PASS: <input type=\"text\" name=\"appass\" id=\"appass\" value=\"\"><br>";
  message += "APEX HOST: <input type=\"text\" name=\"host\" id=\"host\" value=\"apex.local\"><br>";
  message += "APEX USER: <input type=\"text\" name=\"user\" id=\"user\" value=\"admin\"><br>";
  message += "APEX PASS: <input type=\"text\" name=\"pass\" id=\"pass\" value=\"1234\"><br>";
  message += "OUTLET NAME: <input type=\"text\" name=\"outlet\" id=\"outlet\" value=\"Relay\"><br>";
  message += "<br>--- ERROR MODE ---<br>";
  message += "<select name=\"emode\" id=\"emode\"><option value=\"none\">None</option><option value=\"default\" selected>Default</option><option value=\"extreme\">Extreme</option></select><br><br>";
  message += "None: Creates one virtual outlet per relay.<br>No way to confirm command was recieved.<br>Minimal virtual outlet useage<br><br>";
  message += "Default: Creates two virtual outlets for each relay<br>Verify outlet is used to confirm relay has changed state<br>8 virtual outlets for a 4x relay board<br><br>";
  message += "Extreme: Creates four virtual outlets for each relay<br>Add one 'error' virtual outlet for the board<br>'error' outlet allows for easier programming<br>17 virtual outlets for a 4x relay board!<br><br>";
  message += "<br><br>--- DO NOT MODIFY BELOW UNLESS 100% SURE ---<br>";
  message += "RELAY COUNT: <input type=\"text\" name=\"rcount\" id=\"rcount\" value=\"4\"><br>";
  message += "POLLING DELAY: <input type=\"text\" name=\"pdelay\" id=\"pdelay\" value=\"10\"><br><br><br>";
  message += "You will lose connection to RelayConfig wifi on submit<br>";
  message += "Virtual outlets will automatically be created if successful<br>";
  message += "RelayConfig wifi should show back up if failled<br>";
  message += "Reflash to reset. Do not press buttons on board while running the stock firmware.<br>";
  message += "<input type=\"submit\">";
  message += "</form>";
  message += "</html></body>";
  server.send(200, "text/html", message);
}

//Handler for the web server config submission
void configWebServerConfig() { 
  if (server.hasArg("plain")== false){ //Check if body received
        server.send(200, "text/plain", "Body not received");
        return;
  
  }
  // Update our configuration

  String ap = "";
  String appassword = "";
  String message = "";
  String reply = "";
  String apexhost = "";
  int relayCount = 4;
  int pollingDelay = 10;
  
  ap = server.arg("apname");
  
  appassword = server.arg("appass");

  NAME_TO_RESOLVE = server.arg("host");
  NUMBER_OF_RELAYS = (int) server.arg("rcount").toInt();
  APEX_POLL_DELAY = (int) server.arg("pdelay").toInt();

  int counter = 0;
  WiFi.begin(ap, appassword);
  yield();
  while (WiFi.status() != WL_CONNECTED && counter < 15)
  { // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++counter);
    Serial.print(' ');
  }

  if (WiFi.status() != WL_CONNECTED)
  { // Failed to connect.
    String reply = "WIFI CONNECTION FAILLED\n";
    server.send(200, "text/plain", reply);
    return;
  } else {
    writeConfig("ap", ap);
    writeConfig("appassword", appassword);

    message = server.arg("user");
    writeConfig("login", message);
  
    message = server.arg("pass");
    writeConfig("password", message);

    message = server.arg("outlet");
    writeConfig("oname", message);

    message = server.arg("emode");
    if (message == "none"){
      writeConfig("emode", "none");
    } else {
      if (message == "extreme"){
        writeConfig("emode", "extreme");
      } else {
        writeConfig("emode", "default");
      }
    }

    writeConfig("NAME_TO_RESOLVE", NAME_TO_RESOLVE);
    writeConfig("NUMBER_OF_RELAYS", NUMBER_OF_RELAYS);
    writeConfig("APEX_POLL_DELAY", APEX_POLL_DELAY);
    writeConfig("apmode", "2");
    ESP.restart();
  }
}



// Routine to send a relay command to the MCU
void sendRelay(char relayId, char state){
  relayMessage[1] = relayId+1; // Relay commands are offset by one. No relay 0 on board
  relayMessage[2] = state; 
  relayMessage[3] = int(relayMessage[0]) + int(relayMessage[1]) + int(relayMessage[2
  ]);
  Serial.write(relayMessage, sizeof(relayMessage));
  Serial.write(relayMessage, sizeof(relayMessage));
  Serial.write(relayMessage, sizeof(relayMessage));
  Serial.write(relayMessage, sizeof(relayMessage));
  // Store the current time to prevent flooding with commands
  relaySent = millis();
}


// Routine to resolve apex domain and set ServerIP
void resolve_blocking(){
  resolver.setLocalIP(WiFi.localIP());
  ServerIP = resolver.search(NAME_TO_RESOLVE.c_str());
  if (ServerIP != INADDR_NONE) {
    lastResolved = millis();
    resolved = true;
  }
}

// Handles converting from a String outlet name and String outlet state into the proper set of bool flags for the state of the outlet and the created indicator.
void handleOutletFromApex(String outletName, String outletState){
  String outletType;
  String outletId;
  long outletNumber;
  int outletStrOffset;
  bool outletBool;
  
  if (outletState.indexOf("ON") > -1){ // all on states contain ON
    outletBool = true;
  } else {
    outletBool = false;
  }
  if (outletName.indexOf("W_") == 0){ // If an outlet begins with W_ it marks a wireless outlet
    if (outletName.indexOf(myoutletname) == 2){ // If the outlet is W_( our outlet name )
      outletStrOffset = outletName.indexOf("_", 3 + myoutletname.length());
      outletId = outletName.substring(3 + myoutletname.length(), outletStrOffset); // Get the outlet number (or E for the error indicator)
      if (outletId == "E"){ // Error state indicator from apex
        apexRelayError = outletBool;
        apexRelayErrorCreated = true;
      } else { // If we are not the E outlet we should be a virtual outlet number
        outletNumber = outletId.toInt();
        if (outletNumber == 0 || outletNumber > NUMBER_OF_RELAYS){
          return; // Exit this element if we cant parse the relay number correctly;
        }
        // Are we control or are we verify?
        outletType = outletName.substring( 1 + outletName.indexOf("_", outletStrOffset), outletName.length());
        if (outletType == "C"){ // Control
          apexRelayControlState[outletNumber-1] = outletBool;
          apexRelayControlCreated[outletNumber-1] = true;
        } 
        if (outletType == "V"){ // Verify
          apexRelayVerifyState[outletNumber-1] = outletBool;
          apexRelayVerifyCreated[outletNumber-1] = true;
        }
        if (outletType == "AB"){ // Comparitor 
          apexRelayABCreated[outletNumber-1] = true;
        }
        if (outletType == "BA"){ // Comparitor
          apexRelayBACreated[outletNumber-1] = true;
        }
      }
    }
  }
}

// Routine to request apex config. Does not control apex, only gets current state
// Will reset apexLoopsMissed, sets triggerFullResync if apexLoopsMissed gets too high to trigger a full flush to the MCU
void requestApexConfig(){
  WiFiClient client;
  
  String ipStr = String(ServerIP[0]) + '.' + String(ServerIP[1]) + '.' + String(ServerIP[2]) + '.' + String(ServerIP[3]);
  String url =  "/cgi-bin/status.xml";
  const int httpPort = 80;
  
  if (!client.connect(ipStr, httpPort)) {
    apexConnectionError();
    return;
  }
  client.setTimeout(1000);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + ipStr + "\r\n" + "Connection: close\r\n\r\n");
  client.readStringUntil('>');
  String payload = "";
  String localData;
  int payloadLength = 0;
  
  String outletName;
  String outletState;
  


  String xmltag;
  bool readingOutlet = false;
  byte timeoutCount = 0;

  
  while (true){
    
    xmltag = client.readStringUntil('>') + ">";
    if (xmltag.length() == 1){ // We have an empty response, abort
      timeoutCount++;
      if (timeoutCount > 4){
        apexConnectionError();
        return; // Return not break, we dont have a complete apex response
      }
      Serial.write("0RESP");
    }
    if (xmltag.indexOf("</status>") > -1){
      break; // XML OVER
    }
    if (xmltag.indexOf("<outlet>") > -1){
      readingOutlet = true;
    }
    if (xmltag.indexOf("</outlet>") > -1){
      if (readingOutlet){ // We have just finished reading an outlet
        readingOutlet = false;
        handleOutletFromApex(outletName, outletState); // Handle the outlet info
      }
    }
    if (readingOutlet == true){
      if (xmltag.indexOf("</name>") > -1){
        outletName = xmltag.substring(0, xmltag.indexOf("<"));
      }
      if (xmltag.indexOf("</state>") > -1){
        outletState = xmltag.substring(0, xmltag.indexOf("<"));
      }
    }
  }
  if (apexLoopsMissed >= 10){ // If we have more than 10 loops missed.
    triggerFullResync = true; // Indicate all outlets should be flushed to the MCU
  }
  apexLoopsMissed = 0;
}

// Contains the logic to validate our state lines up with the apex state.
// Utilizes triggerFullResync to invert the internal mcuState array thus forcing a full reflush of all relays
void apexDoLogic(){
  if (millis() - apexLastSync > 1000*APEX_POLL_DELAY){ // Run every 10s
    apexLastSync = millis();
    requestApexConfig();
    String apexResponse = "";
  
    byte relayOn = 0;
    bool mcuState;
    bool apexControl;
    bool apexVerify;
    while (relayOn < NUMBER_OF_RELAYS){
      mcuState = mcuRelayState[relayOn];
      apexControl = apexRelayControlState[relayOn];
      apexVerify = apexRelayVerifyState[relayOn];
      if (triggerFullResync){
        mcuRelayState[relayOn] = ! apexControl;
      }
      
      if (mcuState == apexControl && apexVerify != apexControl ) { // We have confirmed a change on the MCU that apex has not registered
        if (mcuState == 1){
          apexResponse = apexResponse + "W_" + String(myoutletname) + "_" + String(relayOn+1) + "_V_state=2&"; // Set corresponding verify outlet to manual on
        } else {
          apexResponse = apexResponse + "W_" + String(myoutletname) + "_" + String(relayOn+1) + "_V_state=1&"; // Set corresponding verify outlet to manual off
        }
      }
      relayOn ++;
    }
    // Indicate we have triggered a resync if it was needed.
    triggerFullResync = false;
    // Check if we have an update to push
    if (apexResponse.length() > 0){
      if (errorMode != "none"){ // Skip updates when we request low useage 'none' error mode
        updateApex(apexResponse);
      }
    }
  }
}

// Routine to indicate we have encounted a connection error from the apex.
void apexConnectionError(){
  // Add one to the missed loops counter
  apexLoopsMissed++;
  // Might be caused by a bad ip address
  resolve_blocking();
}

// Creates an outlet on the apex with default programming
void createOutlet(String outletName){
  String program = "Set OFF";
  createOutlet(outletName, program);
}

// Creates an outlet on the apex with custom programming
void createOutlet(String outletName, String program){
  String auth = base64::encode(String(login) + ":" + String(password));
  WiFiClient client;
  
  String ipStr = String(ServerIP[0]) + '.' + String(ServerIP[1]) + '.' + String(ServerIP[2]) + '.' + String(ServerIP[3]);
  const int httpPort = 80;

  String content = "{\"name\":\""+ outletName +"\",\"ctype\":\"Advanced\",\"prog\":\"" + program + "\",\"log\":false}";

  String Post = String("POST /rest/config/oconf HTTP/1.1\r\n") + String("Host: ") + ipStr + String("\r\nAuthorization: Basic ") + auth + String("\r\nConnection: close\r\nContent-Length: ") + String(content.length()) + String("\r\n\r\n") + content;
  if (!client.connect(ipStr, httpPort)) {
    apexConnectionError();
    return;
  }
  
  client.print(Post);
  client.readStringUntil('{');
}

// Generate missing apex outlets if needed
void generateApexOutlets(){
  if (apexLoopsMissed > 0){ // If we are in an error state do not generate apex outlets
    return;
  }
  int ron = 0;
  String program = "";
  String ctrl;
  String vrfy;
  String ab;
  String ba;

  String extremeProgram = "SET OFF \\n";
  
  while (ron < NUMBER_OF_RELAYS){
    ctrl = "W_" + myoutletname + "_" + String(ron+1) + "_C";
    vrfy = "W_" + myoutletname + "_" + String(ron+1) + "_V";
    ab = "W_" + myoutletname + "_" + String(ron+1) + "_AB";
    ba = "W_" + myoutletname + "_" + String(ron+1) + "_BA";

    extremeProgram += "IF Output " + ab + " = ON Then ON \\n";
    extremeProgram += "IF Output " + ba + " = ON Then ON \\n";
    
    if (errorMode != "none"){ // Skip creating verify outlets for none error mode
      if (apexRelayVerifyCreated[ron] == false){ // Verify outlet not found
        createOutlet(vrfy);
        apexRelayVerifyCreated[ron] = true;
      }
    }
    // Always generate missing control relays
    if (apexRelayControlCreated[ron] == false){ // Control outlet not found
      createOutlet(ctrl);
      apexRelayControlCreated[ron] = true;
    }
    // Generate extreme outlets if requested
    if (errorMode == "extreme"){
      
      if (apexRelayABCreated[ron] == false){ // AB outlet not found
        program = "SET OFF \\n";
        program += "IF Output " + ctrl + " = ON Then ON \\n";
        program += "IF Output " + vrfy + " = ON Then OFF \\n";
        createOutlet("W_" + myoutletname + "_" + String(ron+1) + "_AB", program);
      }
      if (apexRelayBACreated[ron] == false){ // BA outlet not found
        program = "SET OFF \\n";
        program += "IF Output " + ctrl + " = OFF Then ON \\n";
        program += "IF Output " + vrfy + " = OFF Then OFF \\n";
        createOutlet("W_" + myoutletname + "_" + String(ron+1) + "_BA", program);
      }
    }
    ron++;
  }
  // Check for error outlet in extreme mode
  if (errorMode == "extreme" && apexRelayErrorCreated == false){
      createOutlet("W_" + myoutletname + "_E", extremeProgram);
  }
}

// Should error handle bad authorization
void updateApex(String apexResponse){
  String auth = base64::encode(String(login) + ":" + String(password));
  WiFiClient client;
  
  String ipStr = String(ServerIP[0]) + '.' + String(ServerIP[1]) + '.' + String(ServerIP[2]) + '.' + String(ServerIP[3]);
  String url =  "/status.sht?" + apexResponse;
  const int httpPort = 80;
  
  if (!client.connect(ipStr, httpPort)) {
    apexConnectionError();
    return;
  }
  
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + ipStr + "\r\n" + "Authorization: Basic " + auth + "\r\n" + "Connection: close\r\n\r\n");
  client.readStringUntil('<');
}

// Function to update the MCU with the current state of the esp relays when applicable
void updateMCU(){
  bool apexControl;
  bool mcuState;
  if (1){//
    if (millis() - relaySent > 100){ // If it has been more than a tenth of a seccond
      byte relayOn = 0;
      while (relayOn < NUMBER_OF_RELAYS){
        apexControl = apexRelayControlState[relayOn];
        mcuState = mcuRelayState[relayOn];
        if (apexControl != mcuState){ // We have a pending relay update
          sendRelay(relayOn, apexControl); // Update
          mcuRelayState[relayOn] = apexControl;
          return; // Exit, one update per run
        }
        relayOn++;
      }
    }
  }
}

// Confirm wifi is connected, reboot if we have lost connection for too long.
void checkWifi(){
  if  (WiFi.status() == WL_CONNECTED) {
    wifiLastSeen = millis();
    return;
  }
  if (millis() - wifiLastSeen > 120000){ // Missing wifi for more than 2 minutes. Reset
    ESP.restart(); // Bail and retry
  }
}

// Initialization routine
void setup() {
  Serial.begin(115200);
  delay(3000);
  // Initialize / load configuration
  SPIFFS.begin();
  if (SPIFFS.exists("/init") == false) {
    writeConfig("login", "admin");
    writeConfig("password", "1234");
    writeConfig("ap", "ESPAP");
    writeConfig("appassword", "");
    writeConfig("apmode", "1");
    writeConfig("emode", "default");
    writeConfig("oname", "FRELAYF"); // FRELAYF is a default name, will show if glitch happens
    writeConfig("init", "1");
  } 
  
  login = readConfig("login");
  password = readConfig("password");
  ap = readConfig("ap");
  appassword = readConfig("appassword");
  apmode = readConfigLong("apmode");
  myoutletname = readConfig("oname");
  errorMode = readConfig("emode");
  NAME_TO_RESOLVE = readConfig("NAME_TO_RESOLVE");
  NUMBER_OF_RELAYS = (int) readConfigLong("NUMBER_OF_RELAYS");
  APEX_POLL_DELAY = (int) readConfigLong("APEX_POLL_DELAY");

  WiFi.hostname("EspRelay");
  wifi_set_sleep_type(NONE_SLEEP_T);


  if (apmode == 2){
    WiFi.persistent( false );
    WiFi.mode(WIFI_STA);
    WiFi.begin(ap, appassword);
    int wifiTrys = 0;
    while (WiFi.status() != WL_CONNECTED) {
      wifiTrys ++;
      if (wifiTrys > 30){
        ESP.restart(); // Bail and retry
      }
      delay(500);
    }
    resolve_blocking();
    requestApexConfig();
    generateApexOutlets();
  } else {
    if (apmode == 1){ // We are uninitialized
      initConfigSetup();
    } else { // Bad apmode file. Reset on next power cycle by removing initilization token.
      SPIFFS.remove("/init");
    }
  }
}

// Loop routine
void loop() {
  if (initializing == true){ // server needs to be handled while config is running
    server.handleClient(); //Handling of incoming requests
  } else { // Handle regular tasks
    apexDoLogic();
    updateMCU();
    checkWifi();
  }
}
