# ESP32-C6 HomeKit HDMI-CEC TV Controller

## ✨ Features

* **Native HomeKit TV Integration:** Appears as a Television accessory in the Apple Home app.
* **Power Control:** Turn your TV on and off directly from your Apple devices.
* **Input Switching:** Easily switch between HDMI 1, HDMI 2, and HDMI 3.
* **Smart Command Queuing:** Implements a background queue for input switching (800ms delay) to prevent crashing the CEC bus when tapping inputs rapidly in the Home app.
* **Control Center Remote:** Fully supports the iOS Control Center Apple TV Remote widget (Up, Down, Left, Right, Select, Back, Play/Pause).
* **Volume Control:** Maps the physical volume buttons on your iOS device (when the remote widget is open) to CEC volume commands.
* **Two-Way State Synchronization:** Runs in promiscuous mode. If you use your physical TV remote to turn off the TV or change the HDMI input, the Home app updates its state automatically to stay in sync.

## 🛠 Hardware Setup
This firmware is specifically designed for a modified **SMLight SLWF-08** HDMI-CEC controller.

1. **Desolder** the factory ESP-12F (ESP8266) module from the SMLight board.
2. **Solder** a `WT0132C6-S5` (ESP32-C6) module in its place. It shares the same form factor, making it a viable drop-in replacement that supports the modern ESP32 architecture required by HomeSpan.
3. **CEC Line:** The firmware expects the CEC line to be connected to **GPIO 4**.

## 💻 Software Dependencies

To compile and upload this code using the Arduino IDE, you will need the following libraries:

1. **HomeSpan:** The core Apple HomeKit framework for ESP32.
   * *Install via Arduino Library Manager or [HomeSpan GitHub](https://github.com/HomeSpan/HomeSpan).*
2. **CEC by s-moch:** A lightweight HDMI-CEC library for Arduino.
   * *Download and install manually from: [https://github.com/s-moch/CEC](https://github.com/s-moch/CEC).*

## 🚀 Installation & Flashing

1. Set up your Arduino IDE to support ESP32 boards (specifically the ESP32-C6).
2. Install the required dependencies listed above.
3. Open the provided `.ino` sketch.
4. Ensure your Board settings are configured for your specific ESP32-C6 module.
5. Compile and upload the code to your board.
6. Open the Serial Monitor at `115200` baud.
7. Follow the standard [HomeSpan provisioning process](https://github.com/HomeSpan/HomeSpan/blob/master/docs/CLI.md) (typing `W` in the Serial Monitor to configure your Wi-Fi, or using the HomeSpan setup portal).
8. Scan the HomeSpan setup code (default: `466-37-726`) in the Apple Home app to pair your new TV accessory!

## ⚙️ Customization

If your TV uses different logical or physical addresses for its inputs, you can easily modify the code:
* Search for the `pendingInput` execution block in the `loop()` of `HomeSpanTV` to change the transmitted CEC bytes for HDMI 1, 2, and 3.
* Update the `reportedInput` parsing logic in `OnReceiveComplete` if your TV reports different physical addresses (currently looks for `0x1000`, `0x2000`, `0x3000`).
* Change the default TV name by editing `HomeSpanTV* tv = (new HomeSpanTV("Samsung TV"));`.

## 📝 License & Credits

* Apple HomeKit support is powered by [HomeSpan](https://github.com/HomeSpan/HomeSpan).
* HDMI-CEC communication is handled by [s-moch/CEC](https://github.com/s-moch/CEC).
