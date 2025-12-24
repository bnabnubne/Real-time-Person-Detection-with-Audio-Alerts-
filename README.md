# Real-time Person Detection with Audio Alerts

This project implements a **real-time person detection system** running on a **Raspberry Pi 4**, using a lightweight deep learning model and a multi-threaded C++ pipeline.  
When a person is detected, the system triggers an **audio alert** and sends detection results to a **UI dashboard** running on a local machine.

---

## System Overview

**Main features:**
- Real-time camera capture using Raspberry Pi Camera (libcamera + GStreamer)
- Person detection using **YOLOFastestV2** with **NCNN**
- Multi-threaded C++ pipeline (camera, detection, audio, communication)
- Audio alert via Bluetooth speaker
- Real-time data streaming from Pi to UI via UDP + WebSocket bridge

---

## Hardware Requirements

- Raspberry Pi 4  
- Raspberry Pi Camera Module 2
- HDMI monitor (for display)
- Bluetooth speaker (for audio alerts)
- Wi-Fi connection (same network for Pi and local machine)

> Note: Keyboard and mouse are controlled via **VNC Viewer** because USB ports on our Pi were unstable during development.

---

## Software Requirements

### On Raspberry Pi

- Raspberry Pi OS
- C++17 compiler (g++)
- OpenCV
- GStreamer + libcamera
- NCNN (built from source)
- ALSA (for audio playback)

**Install dependencies:**

```bash
sudo apt update
sudo apt install -y \
    g++ cmake \
    libopencv-dev \
    libcamera-dev \
    libgstreamer1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    alsa-utils
```

**NCNN (built from source)**

NCNN must be built manually on the Pi.

```bash
git clone https://github.com/Tencent/ncnn.git
cd ncnn
mkdir build && cd build
cmake ..
make -j4
sudo make install
```

After building, it installs to:
```text
~/ncnn/build/install
```

Make sure ncnn_DIR is set correctly in CMakeLists.txt.
```text
set(ncnn_DIR "/home/pi/ncnn/build/install/lib/cmake/ncnn")
find_package(ncnn REQUIRED)
``` 
**Python Virtual Environment (Raspberry Pi)**

The WebSocket bridge runs inside a Python virtual environment.
Create and activate the virtual environment:
```bash
cd <project_root>
python3 -m venv venv
source venv/bin/activate
pip install flask websocket-client numpy
```
> Note: <project_root> refers to the directory created after cloning this repository.

### On Local Machine (UI)
- Python 3.8+
```bash
pip install flask websocket-client opencv-python matplotlib numpy
``` 
---
## Build Instructions (Raspberry Pi)

```bash
mkdir build
cd build
cmake ..
make -j4
```

## How to Run

### Step 1: Run Detection System on Raspberry Pi
1.	Connect the Raspberry Pi Camera.
2.	Ensure audio output is available (speaker/headphone).
3.	Run:
  ```bash
cd build
./yolo_cam
```
4.	Press q or ESC in the window to quit.
   
### Step 2: Run WebSocket Bridge on Raspberry Pi

Open a new terminal on the Pi:

```bash
cd <project_root>
source venv/bin/activate
python ws_bridge.py
```

> Make sure `ws_bridge.py` is running before starting the UI on the local machine.

### Step 3: Run UI on Local Machine

Make sure the Raspberry Pi and the local machine are connected to the **same Wi-Fi network** .
On your local PC:
 ```bash
python ui.py
```
The UI will be available at:
[http://localhost:8080](http://localhost:8080/)

> The UI connects to the Raspberry Pi using its local IP address. Make sure the IP address in `ui.py` matches your Raspberry Pi IP.
---
## Notes  

By default, the system runs **NCNN inference on CPU** (`kUseVulkan = false`), because during testing, CPU execution provided **more stable and higher FPS** on our setup.

To enable **GPU (Vulkan) acceleration**, users can modify the following line in `main.cpp`:

```cpp
static constexpr bool kUseVulkan = true;
```

After enabling Vulkan, rebuild the project:
```bash
cd build
cmake ..
make -j4
```

---
## Data Flow (Runtime)
### Overall Textual Data Flow

```text
Raspberry Pi 4
──────────────

Camera (IMX219)
   │
   ▼
GStreamer (libcamerasrc → videoconvert → appsink)
   │
   ▼
Camera Thread (C++)
   │
   ├──→ JPEG Encoder
   │        │
   │        ▼
   │   HTTP Server (MJPEG)
   │        │
   │        ▼
   │   /stream.mjpg  ────────────────┐
   │                                 │
   ▼                                 │
Shared Frame Buffer                  │
   │                                 │
   ▼                                 │
Detection Thread (YOLOFastestV2 + NCNN)
   │
   ▼
Detection Results (Bounding boxes, confidence)
   │
   ├──→ Audio Logic
   │        │
   │        ▼
   │   Bluetooth Speaker (Audio Alert)
   │
   ├──→ Performance Logger
   │        │
   │        ▼
   │   perf_log.csv
   │
   └──→ UDP JSON Sender (port 9001)
            │
            ▼
	ws_bridge.py (Python, Pi)
	        │
	        ▼
	    WebSocket
	        │
	        ▼
────────────────────────────────────────────────────────
Local Machine (UI)
────────────────────────────────────────────────────────

UDP Receiver (Python UI)
   │
   ▼
Detection Event Parser
   │
   ▼
UI State Update
   │
   ├──→ Bounding Box Overlay (SVG / Canvas)
   ├──→ FPS & Status Display
   └──→ Alert Indicator

MJPEG Stream Client
   │
   ▼
Live Video Display (Browser / UI Window)
```
