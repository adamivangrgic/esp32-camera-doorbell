# ESP32 camera and two-way audio communication
Video doorbell on ESP32-CAM board, with a speaker and a microphone for two-way audio communication in Home Assistant and other optional components such as buttons, relays etc.
This tuttorial assumes some previous experience with ESPHome, Home assistant, Docker, soldering etc.

## Why and how did I make this
I've wanted to make a video doorbell with two-way communication but as ESPHome natively doesn't support it and ESP32 is pretty underpowered it looked like it couldn't be done. Maybe two-way communication is possible using SIP but it looked like that would make the camera unusable. So it looked like I had three options, either buy a doorbell for 100-200€, sacrifice either video or audio or make a frankenstein contraption with two ESP32 boards that would each do their respective job.
I wasn't satisfied with my options so I looked into writing my custom code for the two-way audio interface, as ESP32 camera component already exists and is supported.
Long story short I succesfully built my custom two-way audio component. Now that I had a way to recieve and transmmit audio over UDP protocol, I needed a valid target on the other side. Since Home assistant doesn't natively support two-way audio functionality and can't be a target for a UDP transmission I needed an intermediary server that supports such functionality (go2rtc). All that is left now is an audio consumer component on the Home assistant side (WebRTC Camera component).

## Components
- ESP32-CAM board (5€)
  - alternatives possible, but substantial wiring changes required
- OV2640 camera with a ribbon cable (short recommended) (choose a wider angle lens, 120° - 160°) (~7€)
  - alternatives include other ESP32 compatible cameras such as OV5640 which provides better resolution and low light performance due to larger sensor, but ESP32 can be underpowered for full resolution; also to note is that this camera overheats quickly when paired with the ESP32-CAM board, so it would require a small heatsink on the back of the camera
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


In order to attach the camera to the board you have to lift the latch below the card slot, apply the ribbon with camera facing up and close the latch. Be careful when handling the camera as it is delicate.

## Flashing esp32
1. Install ESPHome if you haven't
   - https://esphome.io/guides/getting_started_command_line.html#installation - I would recommend using Docker and installing it on your home server, you will also need it later for two-way communication and Home Assistant
   - or install as an add-on if running Home Assistant OS
2. Clone this repo and place the files in your mounted ESPHome main directory along with other yaml configs if you have any (directory which contains .platofrmio)
3. Doorbel config should appear on your ESPHome dashboard, there you can configure it to your liking.
4. Flash your esp32 as normal.

## Deploying go2rtc
docker-compose.yaml :
```
services:
  go2rtc:
    image: alexxit/go2rtc
    network_mode: host
    # privileged: true         # only for FFmpeg hardware transcoding
    restart: unless-stopped
    environment:
      - TZ=Europe/Zagreb
    volumes:
      - "/home/user/go2rtc:/config"   # dir for go2rtc.yaml file
```

1. Assuming you have docker and docker-compose installed, copy the above config and paste it into a yaml file.
2. Execute docker compose up (you will probably need to su privileges)
3. Navigate to go2rtc dashboard on port 1984, assuming your server is on 192.168.1.2, http://192.168.1.2:1984

```
streams:
    parlafon:
        - "exec: ffmpeg -f s16le -ar 16000 -ac 1 -i udp://0.0.0.0:10801 -acodec libopus -b:a 64k -bufsize 128K -rtsp_transport tcp -f rtsp -af 'volume=30.0,highpass=f=200,lowpass=f=3000,afftdn=nf=-25,adynamicsmooth,equalizer=f=1000:width_type=h:width=2000:g=4,equalizer=f=3000:width_type=h:width=2000:g=2' {output}"
        - "exec: ffmpeg -f alaw -ar 8000 -i - -c:a pcm_s16le -ar 16000 -ac 1 -f s16le -af 'volume=0.35' 'udp://192.168.1.60:10801?pkt_size=1024'#backchannel=1"

webrtc:
    api: true
    candidates:
        - 192.168.1.2:8555
        - stun:8555
```
4. Paste the code above under the config tab in the web dashboard, make changes to the code according to your needs (if you use other IP addresses or ports, audio manipulation, volume etc.)
5. Save & restart.

## Home assistant card
1. Install WebRTC camera (https://github.com/AlexxIT/WebRTC)
2. Configure the integration according to the instructions
3. Incorporate it into your Home assistant dashboard, example config below (change url based on your stream name, or use actual url)

```
type: custom:webrtc-camera
streams:
  - url: parlafon
    mode: webrtc
    media: audio,microphone
ui: true
muted: false
```

## Optionally
Since I made the communication between devices flexible, it is possible to establish communication between either an esp32 and a server, or even between two esp32 devices, you would just need a way to assign partner ip address in some way (physical button, via HA etc.), though I may add this functinality or a config example in the future.
