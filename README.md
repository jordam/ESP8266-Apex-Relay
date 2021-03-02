# ESP8266-Apex-Relay

This project integrates specific wifi relay boards with the Neptune Systems Apex Controller.
It will allow you to wirelessly control devices with the Apex.

## Features

- Toggle relays wirelessly from the Apex.
- Configurable feedback control to confirm your command was received
- Supports multiple apex controllers and multiple relay boards on one network.
- Streamlined setup, cheap hardware

## Hardware

Currently this project should run on following relay boards, all from the same manufacturer.
[LC Technologies 5v 4 channel](https://www.icstation.com/esp8266-wifi-channel-relay-module-remote-control-switch-wireless-transmitter-smart-home-p-13420.html)
[LC Technologies 12v 4 channel](https://www.icstation.com/esp8266-wifi-channel-relay-module-remote-control-switch-wireless-transmitter-smart-home-p-13421.html)
[LC Technologies 5v 2 channel](https://www.icstation.com/esp8266-wifi-channel-relay-module-smart-home-remote-control-switch-android-phone-control-transmission-distance-100m-p-12592.html)
[LC Technologies 12v 2 channel](https://www.icstation.com/esp8266-wifi-channel-relay-module-smart-home-remote-control-switch-android-phone-control-transmission-distance-100m-p-12593.html)

You will need to change the Relay Count setting from 4 to 2 for the 2 channel relay boards.

All relay boards I received were rated for 240V AC / 30V DC at 10A. I personally use them at low voltage and suggest you do the same unless you are skilled working with high voltage projects. 

I also recommend purchasing two other components if you have not worked with the esp8266 platform before.

[Spare ESP-01 Chip](https://www.icstation.com/esp8266-remote-serial-port-wifi-transceiver-wireless-module-apsta-wifi-board-smart-home-p-4928.html)
[ESP-01 USB Programmer](https://www.icstation.com/esp8266-wifi-module-pinboard-cellphone-computer-wireless-communication-adapter-wifi-board-module-p-8857.html)

You will need the programmer to upload this software to the device. There is currently no supported way to upload this software to the board or reset the settings without an ESP-01 programmer.

## Installation

Install Arduino and the esp8266 add-on. [(HOW-TO)](https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/)

If you have bought a spare ESP-01 board I recommend removing the existing ESP-01 from the relay board and labeling it as "STOCK" with a piece of tape and setting it aside. This allows you to keep a backup of the original software that was running on this relay board. You can then plug your spare ESP-01 into the USB programmer for the next step.

If you have not bought a spare ESP-01 you will need to unplug the ESP-01 from the relay board and into the USB programmer. Continuing with this process will overwrite the original program that came with the relay.

Open ESP8266-Apex-Relay.ino in the Arduino IDE.

Under the tools menu make sure the following settings are set.

- Board: "Generic ESP8266 Module"
- Erase Flash: "All Flash Contents"

Set the switch on the USB programmer to 'prog' and plug in the USB programmer.

Press upload. The sketch should compile, and after a short time it should upload the software to the ESP-01. If it says 'Connecting' followed by a red error warning you the selected serial port does not exist you either need to change the Tools -> Port setting, unplug and plug the devices back in, or ensure the switch on the USB programmer is set to 'prog' not 'uart'.

Once you have uploaded the software you can plug the ESP-01 into the relay board. Power the relay board on, wait at least 10 seconds, power it down, then turn it back on. This 10 second initial configuration cycle is needed the first boot after it is reprogrammed.

Once you have completed the first boot and have powered it back it should be ready for configuration and will launch a wifi access point. Use a phone, or computer to look for a "RelayConfig" wifi to confirm it is awaiting configuration.

# Configuration

Once you have completed installation you will need to configure the device.
Connect to the "RelayConfig" access point with your phone or computer and go to http://192.168.1.1/ .

Configure the following
- Enter in your local wifi name. This must be a 2.4GHz wifi, not 5GHz. 
- Enter in your local wifi password.
- Change the Apex Host, User, and Pass if you have changed them on your device. This needs to be a valid login for the local apex account not for apex fusion. The defaults are pre-set for you if you have not modified them.
- If you would like to change the default outlet name of "Relay" to something else you can. Do not set a name larger than 6 letters or anything you cant enter into Apex.
- Change the error mode if desired
    - None (No feedback from commands, uses 1 outlet per relay)
    - Default (You set _C outlet to request a change, _V is set by relay board to confirm. 2 outlets per relay)
    - Extreme (4 virtual outlets generated per relay + 1 per board. Adds 'error' outlet for easier handling of error states)
- Change the relay count if you have more or less than 4 relays on your board.
- The polling delay indicates how often the relay board will check in with the Apex controller. I do not recommend changing this unless you have a specific need.

Once you have entered your options just hit submit and wait for it to try to connect to the Apex. You will see a set of new outlets show up in Apex shortly if it worked. 

If the wifi info was wrong it should reset itself and start the RelayConfig access point allowing you to try again.

If you encounter an unforeseen issue you can restart this process by following the instructions in "Installation" again to reinstall the software.

# Usage

Standard usage will generate two outlets per relay on the relay board. For a 4 relay board this should generate the following.
```
W_Relay_1_C  (Relay 1 Control)
W_Relay_1_V  (Relay 1 Verify)
W_Relay_2_C  (Relay 2 Control)
W_Relay_2_V  (Relay 2 Verify)
W_Relay_3_C  (Relay 3 Control)
W_Relay_3_V  (Relay 3 Verify)
W_Relay_4_C  (Relay 4 Control)
W_Relay_4_V  (Relay 4 Verify)
```

Control outlets can be used to control outlets on the board from Apex. Verify outlets allow the Apex to wait and confirm the wireless command was received and acted on.

I use this to confirm remote valves are opened before automatically turning on high pressure pumps for instance. If my wifi went down my Apex would attempt to open the remote valve via the control outlet (as it does not understand it is wireless) but the verify outlet would never change states, allowing it to detect and prevent actions the require the wireless command to complete.

'Extreme' feedback mode generates a lot of extra outlets behind the scenes to track when verify and control outlets don’t all match. This mode will generate a W_Relay_E outlet. When this outlet is set to ON it means there is an error because not all relay states are currently in sync. This can allow you to easily reference one unified outlet to validate your commands at the expense of using up extra virtual outlets.

# Development

Want to contribute? Great! I'm open to pull requests an collaboration to add features and relay boards.

On my end I would like to target more consumer oriented ESP8266 relay devices. I have a few 120v devices that in theory might be a good candidate if they can be flashed OTA. This route could simplify installation further and open up standard outlets to non electricians.

# License

Copyright (c) 2021 (https://github.com/jordam)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
