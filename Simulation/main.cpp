// ============================================================
// SystemC model for
// "Real-time Person Detection with Audio Alerts"
// Calibrated from Raspberry Pi measurements
//
// Pipeline:
//   Camera -> Preprocess -> Detector (every 3 frames)
//          -> Reuse last detection -> Decision -> Audio start
//
// Outputs:
//   - latency_sc.csv
//   - Console: FPS (Loop / Detector)
//
// SystemC version: 3.0.2
// ============================================================

#include <systemc>
#include <iostream>
#include <fstream>
#include <random>

using namespace sc_core;

// ============================================================
// 1. Parameters (CALIBRATED FROM PI MEASUREMENTS)
// ============================================================
namespace cfg {

    // ---- Simulation ----
    constexpr double RUN_SECONDS = 15.0;

    // ---- Camera (measured ~4–4.5 FPS) ----
    constexpr double T_CAM_P50_MS = 230;
    constexpr double T_CAM_P95_MS = 260;

    // ---- Preprocess ----
    constexpr double T_PP_MS = 1;

    // ---- Detector (YOLOFastestV2 – NCNN) ----
    constexpr double T_DET_P50_MS = 290;
    constexpr double T_DET_P95_MS = 305;
    constexpr double P_TAIL       = 0.05;

    // ---- Decision logic ----
    constexpr double T_DEC_MS = 1;

    // ---- Audio (modeled) ----
    constexpr double T_AUD_START_MS = 30;

    // ---- Control ----
    constexpr int DET_EVERY_N = 3;

    // ---- Output ----
    constexpr const char* CSV_PATH = "latency_sc.csv";
}

// ============================================================
// Utility
// ============================================================
inline double to_ms(const sc_time& t) {
    return t.to_seconds() * 1000.0;
}

// ============================================================
// Data types
// ============================================================
struct Frame {
    int id;
    sc_time t_cam;
};
inline std::ostream& operator<<(std::ostream& os, const Frame& f) {
    os << "Frame{"
       << "id=" << f.id
       << "}";
    return os;
}

struct DetResult {
    int id;
    bool person;
    sc_time t_cam;
    sc_time t_det;
};
inline std::ostream& operator<<(std::ostream& os, const DetResult& d) {
    os << "DetResult{"
       << "id=" << d.id
       << ", person=" << d.person
       << "}";
    return os;
}


struct Event {
    int id;
    bool play_audio;
    sc_time t_cam;
    sc_time t_evt;
};
inline std::ostream& operator<<(std::ostream& os, const Event& e) {
    os << "Event{"
       << "id=" << e.id
       << ", play=" << e.play_audio
       << "}";
    return os;
}

// ============================================================
// CSV Logger
// ============================================================
struct CsvLogger {
    std::ofstream f;
    CsvLogger() {
        f.open(cfg::CSV_PATH);
        f << "frame_id,cam_ms,det_ms,evt_ms,aud_ms,e2e_ms,ran_infer\n";
    }
    void log(int id,
             const sc_time& t_cam,
             const sc_time& t_det,
             const sc_time& t_evt,
             const sc_time& t_aud,
             bool ran_infer)
    {
        double cam  = to_ms(t_cam);
        double det  = to_ms(t_det);
        double evt  = to_ms(t_evt);
        double aud  = to_ms(t_aud);
        double e2e  = aud - cam;
        f << id << "," << cam << "," << det << ","
          << evt << "," << aud << "," << e2e << ","
          << (ran_infer?1:0) << "\n";
    }
} g_logger;

// ============================================================
// 2. Modules
// ============================================================

// ---------------- Camera ----------------
SC_MODULE(Camera) {
    sc_fifo_out<Frame> out;
    std::mt19937 rng;
    std::uniform_real_distribution<> jitter;

    SC_CTOR(Camera)
        : rng(1),
          jitter(0.0, cfg::T_CAM_P95_MS - cfg::T_CAM_P50_MS)
    {
        SC_THREAD(run);
    }

    void run() {
        int id = 0;
        while (true) {
            Frame f{ id++, sc_time_stamp() };
            out.write(f);
            double dt = cfg::T_CAM_P50_MS + jitter(rng);
            wait(sc_time(dt, SC_MS));
        }
    }
};

// ---------------- Preprocess ----------------
SC_MODULE(Preprocess) {
    sc_fifo_in<Frame> in;
    sc_fifo_out<Frame> out;

    SC_CTOR(Preprocess) {
        SC_THREAD(run);
    }

    void run() {
        while (true) {
            Frame f = in.read();
            wait(sc_time(cfg::T_PP_MS, SC_MS));
            out.write(f);
        }
    }
};

// ---------------- Detector ----------------
SC_MODULE(Detector) {
    sc_fifo_in<Frame> in;
    sc_fifo_out<DetResult> out;

    std::mt19937 rng;
    std::bernoulli_distribution tail;
    bool last_person = false;
    int frame_cnt = 0;

    SC_CTOR(Detector)
        : rng(2),
          tail(cfg::P_TAIL)
    {
        SC_THREAD(run);
    }

    void run() {
        while (true) {
            Frame f = in.read();
            frame_cnt++;

            bool ran_infer = (frame_cnt % cfg::DET_EVERY_N == 0);
            bool person = last_person;

            if (ran_infer) {
                double tdet = tail(rng) ? cfg::T_DET_P95_MS
                                        : cfg::T_DET_P50_MS;
                wait(sc_time(tdet, SC_MS));
                person = true; // assume person present
                last_person = person;
            }

            out.write({ f.id, person, f.t_cam, sc_time_stamp() });
        }
    }
};

// ---------------- Decision ----------------
SC_MODULE(Decision) {
    sc_fifo_in<DetResult> in;
    sc_fifo_out<Event> out;

    SC_CTOR(Decision) {
        SC_THREAD(run);
    }

    void run() {
        while (true) {
            DetResult d = in.read();
            wait(sc_time(cfg::T_DEC_MS, SC_MS));
            out.write({ d.id, d.person, d.t_cam, sc_time_stamp() });
        }
    }
};

// ---------------- Audio ----------------
SC_MODULE(Audio) {
    sc_fifo_in<Event> in;

    SC_CTOR(Audio) {
        SC_THREAD(run);
    }

    void run() {
        while (true) {
            Event e = in.read();
            wait(sc_time(cfg::T_AUD_START_MS, SC_MS));

            sc_time t_aud = sc_time_stamp();
            g_logger.log(e.id, e.t_cam, e.t_evt, e.t_evt, t_aud, true);

            std::cout << "[AUDIO] frame=" << e.id
                      << " start@" << t_aud << std::endl;
        }
    }
};

// ============================================================
// 3. Top
// ============================================================
int sc_main(int, char**) {

    sc_fifo<Frame>     q1(8), q2(8);
    sc_fifo<DetResult> q3(8);
    sc_fifo<Event>     q4(8);

    Camera     cam("Camera");
    Preprocess pp("Preprocess");
    Detector   det("Detector");
    Decision   dec("Decision");
    Audio      aud("Audio");

    cam.out(q1);
    pp.in(q1);  pp.out(q2);
    det.in(q2); det.out(q3);
    dec.in(q3); dec.out(q4);
    aud.in(q4);

    sc_start(sc_time(cfg::RUN_SECONDS, SC_SEC));
    sc_stop();

    std::cout << "Simulation finished. CSV saved to "
              << cfg::CSV_PATH << std::endl;
    return 0;
}
