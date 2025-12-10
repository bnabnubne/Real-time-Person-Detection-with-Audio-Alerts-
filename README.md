# Real-time Person Detection with Audio Alerts (Raspberry Pi 4 + NCNN)

This project implements a **real-time person detection system** on **Raspberry Pi 4** using:

- **Raspberry Pi Camera (IMX219)** via **libcamera + GStreamer**
- **YOLOFastestV2** model converted to **NCNN**
- **C++ inference pipeline** accelerated with **Vulkan**
- **Audio alerts** when a person is detected

---

## 1. Repository Structure
Suggested layout:

```text
.
├── CMakeLists.txt
├── main.cpp                  # C++ entry: camera + detection + audio
├── yolo-fastestv2.h
├── yolo-fastestv2.cpp
├── audio_player.hpp
├── audio_player.cpp
├── models/
│   ├── yolo-fastestv2-opt.param
│   └── yolo-fastestv2-opt.bin
├── person_detected.wav
```
---

## 2. Hardware & OS

- **Board:** Raspberry Pi 4  
- **Camera:** Raspberry Pi IMX219  
- **OS:** Raspberry Pi OS (Debian 13-based)  
- **GPU:** V3D 4.2.14.0 (supports Vulkan, used by NCNN)

Camera is accessed via **libcamerasrc** (GStreamer).  
Works on both **HDMI display** and **VNC** (note: VNC may reduce FPS slightly).

---

## 3. Dependencies

### 3.1 System Packages

Install required packages:

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
### 3.2. NCNN (built from source)

NCNN must be built manually on the Pi.
After building, it installs to:
```text
~/ncnn/build/install
```

CMake must include:
```text
set(ncnn_DIR "/home/pi/ncnn/build/install/lib/cmake/ncnn")
find_package(ncnn REQUIRED)
``` 

## 4. Build Instructions
  ```bash
mkdir build
cd build
cmake ..
make -j4
```
Run:
```bash
./yolo_cam
```
## 5. How to Run
	1.	Connect the Raspberry Pi Camera.
	2.	Ensure audio output is available (speaker/headphone).
	3.	Run:
  ```bash
cd build
./yolo_cam
```
	4.	Press q or ESC in the window to quit.

# System Architecture 
## 1. Main Components

1. **Camera Capture (Raspberry Pi + libcamera + GStreamer)**
   - Captures video frames from IMX219 camera.
   - Outputs BGR frames (`352 x 352`) to OpenCV via `VideoCapture` (`appsink`).

2. **YOLOFastestV2 Detector (C++ + NCNN)**
   - Preprocesses frame → `ncnn::Mat` (resize + normalize).
   - Runs NCNN inference with Vulkan (if enabled).
   - Decodes YOLOFastestV2 output (2 feature maps, anchor-based).
   - Applies NMS and produces a list of bounding boxes.

3. **Detection Logic / Decision Layer**
   - Interprets detection results.
   - Focuses on class `0` (`person`).
   - Applies threshold (`score >= 0.5`) to declare that a person is present.
   - Uses frame skipping (detect every 3rd frame) to reduce load.

4. **Audio Alert Module**
   - Receives boolean `person_detected` events.
   - Plays a WAV file (`person_detected.wav`) via `aplay`.
   - Throttles audio (cooldown 2000 ms) to avoid spam.

5. **Visualization / Output**
   - Draws bounding boxes and class/score labels on the current frame.
   - Overlays FPS text.
   - Shows a window `YOLOFastestV2 NCNN C++` via OpenCV.

---

## 2. Data Flow (Runtime)

Textual data flow:

```text
Camera (IMX219)
   │
   ▼
libcamerasrc + GStreamer + appsink
   │
   ▼
OpenCV (BGR frame 352x352)
   │
   ├─[every 3rd frame]→ YOLOFastestV2 Detector (NCNN)
   │          │
   │          ▼
   │     Detection results (TargetBox list)
   │
   └─[other frames]───┐
                      ▼
          Decision Logic (reuse last TargetBox list)
                      │
                      ├──→ Audio Alert (if person_detected)
                      │
                      └──→ Bounding Boxes + FPS overlay
                                   │
                                   ▼
                            Display window (OpenCV)
