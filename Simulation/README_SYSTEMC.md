This document explains how to build and run the SystemC simulation for the project
Real-time Object Detection with Audio Alerts.

The SystemC model is used to analyze latency and effective FPS and to compare with Raspberry Pi measurements.

1. Purpose

The SystemC simulation models the runtime pipeline:

Camera → Preprocess → YOLO Inference → Decision → Audio Trigger


It is used to:

Measure inference latency and end-to-end latency

Estimate effective FPS

Compare SystemC results with real Raspberry Pi data

This is a high-level performance simulation, not a hardware-accurate model.

2. Requirements

Ubuntu 20.04 / 22.04

g++ (C++17)

SystemC 3.0.2

pthread

3. Install SystemC 3.0.2
wget https://www.accellera.org/images/downloads/standards/systemc/systemc-3.0.2.tar.gz
tar -xvf systemc-3.0.2.tar.gz
cd systemc-3.0.2
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/opt/systemc-3.0.2
make -j$(nproc)
make install

4. Environment Setup

Add to ~/.bashrc:

export SYSTEMC_HOME=$HOME/opt/systemc-3.0.2

for L in lib64 lib-linux64 lib-linux lib; do
  if [ -d "$SYSTEMC_HOME/$L" ]; then
    export SC_LIB_DIR="$SYSTEMC_HOME/$L"
    break
  fi
done

export LD_LIBRARY_PATH="$SC_LIB_DIR:$LD_LIBRARY_PATH"


Apply:

source ~/.bashrc

5. Build SystemC Simulation
g++ -std=c++17 main.cpp \
  -I$SYSTEMC_HOME/include \
  -L$SC_LIB_DIR \
  -lsystemc -lpthread \
  -o run_systemc

6. Run Simulation
./run_systemc


Outputs:

Console logs (audio trigger events)

CSV log file (latency and frame timestamps)

7. Post-Processing

The CSV file can be analyzed using Python / Jupyter / Google Colab to:

Plot latency CDF

Compute p50 / p95 latency

Compare SystemC vs Raspberry Pi FPS and latency

8. Notes

No camera, YOLO model, or audio files required

Timing parameters are derived from real Raspberry Pi measurements

The simulation is for design analysis and comparison