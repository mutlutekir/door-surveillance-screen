# Smart Door Surveillance & Weather Panel

A high-performance smart home panel built on an ESP32-S3 and a MIPI RGB display (like the Waveshare variants). It features a live MJPEG camera feed (from go2rtc/Home Assistant), real-time weather with 8-hour forecasts, and capacitive touch interaction. Everything is rendered natively using the LVGL graphics library in ESPHome.

## Features

- 📹 **Live Camera Feed (Optimized for 2 FPS)**: Pulls an MJPEG stream directly from your local network via go2rtc. The stream is deliberately configured to **2 FPS**. This is a crucial hardware optimization that prevents the ESP32-S3 CPU from overheating, eliminates network bloat/latency, and ensures instant frame delivery as soon as the screen wakes up.
- 🌤️ **Live Weather & Forecasts**: Fetches weather data directly from Home Assistant's `weather.get_forecasts` API, rendering beautiful Material Design icons and a gradient background.
- 👈 **Touch Interaction**: Tap the screen to seamlessly slide between the Camera view and the Weather dashboard. Tap again to put the screen to sleep.
- 🚪 **Automation Ready**: Exposed switches allow Home Assistant to automatically turn on the screen when a door opens (showing weather) or when motion/a doorbell is triggered (showing the camera).
- ⚙️ **Advanced Hardware SPI Init**: Contains a custom C++ bit-banging sequence to initialize the MIPI RGB display properly through the PCA9554 I2C expander.

## Hardware

| Component | Details |
|---|---|
| Board | ESP32-S3 (Requires PSRAM) |
| Display | Custom MIPI RGB (480x480) with GT911 Touch |
| I2C Expander | PCA9554 (Controls SPI pins for Display Init) |

## Prerequisites

1. **Home Assistant** running the ESPHome add-on.
2. A **Camera stream** accessible via MJPEG. It is highly recommended to use [go2rtc](https://github.com/AlexxIT/go2rtc) to resize the feed to 480x480 and limit it to **2 FPS**.
3. A configured **Weather Entity** in Home Assistant that supports hourly forecasts (e.g., `weather.home`).

## go2rtc Configuration (Highly Recommended)

To achieve the best performance without overheating the ESP32, you should configure a dedicated low-fps, cropped stream in your `go2rtc.yaml`. The `preload` directive is crucial here, as it keeps the stream ready in the background.

Add the following to your `go2rtc.yaml` config:

```yaml
preload:
  door_panel_stream: "video"

streams:
  # Your main camera feed (replace credentials and IP)
  door_camera:
    - onvif://username:password@192.168.X.X:2020

  # The optimized stream for the ESP32 Panel (480x480, high quality, 2 FPS)
  door_panel_stream:
    - "ffmpeg:door_camera#video=mjpeg#width=480#height=480#q:v=3#fps=2"


Installation & Configuration
Copy the door-surveillance-screen.yaml contents into a new ESPHome node.

Ensure you have the materialdesignicons-webfont.ttf file placed in a fonts/ folder relative to your ESPHome configuration directory.

Update the following placeholders in the YAML with your own details:

YOUR_API_KEY_HERE: Your ESPHome API encryption key.

YOUR_FALLBACK_PASSWORD_HERE: The fallback AP password.

weather.your_weather_entity_hourly: Search and replace this with your actual Home Assistant weather entity (e.g., weather.home).

binary_sensor.your_door_sensor: Search and replace with the door sensor that should trigger the screen.

http://YOUR_GO2RTC_IP:1984/api/stream.mjpeg?src=door_panel_stream: Update this URL to point to your live camera stream (matching the go2rtc config above).

Flash the code to your ESP32-S3.

How It Works
Vacuum Background Logic: When the screen is turned off, the ESP32 doesn't drop the connection. Instead, it silently "vacuums" and discards the incoming frames. This prevents the go2rtc server from putting ffmpeg to sleep, ensuring a fresh frame is instantly ready the millisecond you wake the screen.

Sleep Mode: The backlight turns off automatically after 30 seconds of inactivity to save power and reduce heat.

Waking Up: The screen can be woken up by a physical touch, or via Home Assistant automations triggering the exposed Screen switch.

Smart Routing: If triggered by the door opening, it slides in the Weather dashboard. If triggered by a motion sensor (or tapped while on the weather screen), it slides in the Camera feed.

License
MIT
