# ESP32 camera and two-way audio communication
Video doorbell on ESP32-CAM board, with a speaker and a microphone for two-way audio communication in Home Assistant and other optional components such as buttons, relays etc.

## Components
- ESP32-CAM board (5€)
  - alternatives possible, but substantial wiring changes required
- OV2640 camera with a ribbon cable (short recommended) (choose a wider angle lens, 120° - 160°) (~7€)
  - alternatives include other ESP32 compatible cameras such as OV5620 which provides better resolution and low light performance due to larger sensor, but ESP32 can be underpowered for full resolution; also to note is that this camera overheats quickly when paired with the ESP32-CAM board, so it would require a small heatsink on the back of the camera
- INMP441 microphone (2€)
  - other I2C microphones possible with wiring changes
- MAX98357 amplifier (1€)
  - other I2C amps possible with wiring changes
- Speaker (4-8 ohm impedance, ~3 W) (5-10€)
- Any kind of button
- Low voltage level (5 V) relay
- Perfboard, jumper wires, pin headers, thermally conductive pads, and other basic soldering supplies
- ESP32-CAM-MB development programmer board for easy firmware flashing over USB

None of these are required components, all are substitable with other equivalents.
Also you can include more components that you may want or exclude any that you don't need, but be mindful of limited available pins on the ESP32-CAM (more info on the board https://www.best-microcontroller-projects.com/esp32-cam.html).

## Wiring and assembly
![diagram](https://github.com/user-attachments/assets/e7d41b37-6bdb-458e-8228-acb570b99bae)

### Contact me if you want to buy a PCB so I can buy in bulk for all interested, or maybe even if you want it assembled.

End result:

In order to attach the camera to the board you have to lift the latch below the card slot, apply the ribbon with camera facing up and close the latch. Be careful when handling the camera as it is delicate.

## Flashing esp32
1. Install ESPHome if you haven't
   - https://esphome.io/guides/getting_started_command_line.html#installation - I would recommend using Docker and installing it on your home server, you will also need it later for two-way communication and Home Assistant
   - or install as an add-on if running Home Assistant OS
2. 
