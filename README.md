# COB-PWM-Controller

Robust ESP32-C3 COB LED PWM controller for continuous, responsive operation.

## Hardware
- ESP32-C3-DevKitM-1
- COB LED with an appropriate MOSFET/LED driver stage
- PWM control: GPIO 3
- No OLED/display
- Bluetooth is not used

## Firmware
- Brightness range is 1–100%
- OFF is a separate power state; brightness never uses 0
- 20 kHz PWM
- Persistent brightness and power state using NVS/Preferences
- Web control
- OTA updates through ArduinoOTA
- Wi-Fi station mode
- Wi-Fi sleep disabled for responsiveness
- No deep sleep or light sleep
- Responsive main loop
- No display dependencies

## Setup
1. Open include/config.h.
2. Set WIFI_SSID and WIFI_PASSWORD.
3. Build with PlatformIO.
4. Flash the ESP32-C3.
5. Open the IP address printed by Serial Monitor.

## Safety
Do not drive a COB LED directly from an ESP32 GPIO. GPIO 3 is only the PWM control signal for a suitable MOSFET/LED driver circuit. Use an appropriate current-limited LED power supply and suitable grounding.

## Status
Initial real firmware foundation. Hardware validation should be performed after a successful build and before connecting the COB load.
