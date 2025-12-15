// ============================================================
// SystemC model for
// "Real-time Person Detection with Audio Alerts"
// Rewritten to match Raspberry Pi behavior + perf_log format
//
// Key behavior (matches Pi code):
//   - Camera runs ~30 FPS (independent)
//   - Detector reads "latest frame" (overwrites old frames -> drop-frame)
//   - Detector loop is the bottleneck (~5–6 FPS from Pi perf_log)
//   - Logs in PerfLogger-compatible CSV columns
//
// Outputs:
//   - latency_sc.csv: frame_id,t_cam,t_pp,t_det_s,t_det_e,t_dec,t_aud,ran_infer
//   - Console: LoopFPS (camera), DetFPS (inference)
// SystemC: 3.0.2
// ============================================================

#include <systemc>
#include <iostream>
#include <fstream>
#include <random>
#include <cstdint>

using namespace sc_core;

// ============================================================
// 1) Parameters (fit to your perf_log.xlsx characteristics)
// ============================================================
namespace cfg {
    // ---- Simulation ----
    constexpr double RUN_SECONDS = 15.0;

    // ---- Camera (~30 FPS on Pi; detector only samples latest) ----
    constexpr double CAM_FPS            = 30.0;
    constexpr double CAM_JITTER_MS      = 1.0;   // small jitter around 33.3ms

    // ---- Preprocess (from perf_log: ~10ms median, p95 ~20ms, max ~30ms) ----
    // We approximate with a small discrete distribution:
    //   88% -> 10ms, 10% -> 20ms, 2% -> 30ms
    constexpr double PP_MS_P10  = 10.0;
    constexpr double PP_MS_P95  = 20.0;
    constexpr double PP_MS_MAX  = 30.0;

    // ---- Inference (from perf_log: ~170ms median, p95 ~265ms, max ~350ms) ----
    // Approx discrete distribution:
    //   94% -> 170ms, 5% -> 265ms, 1% -> 350ms
    constexpr double DET_MS_P50  = 170.0;
    constexpr double DET_MS_P95  = 265.0;
    constexpr double DET_MS_MAX  = 350.0;

    // ---- Decision (from perf_log: mostly 0ms, occasional ~10ms) ----
    constexpr double DEC_MS_FAST = 0.0;
    constexpr double DEC_MS_SLOW = 10.0;

    // ---- Control: run inference every N detector iterations ----
    // NOTE: Your perf_log has ran_infer=1 for all rows -> set this to 1 to match.
    constexpr int DET_EVERY_N = 1;

    // ---- Audio marker (your perf_log t_aud exists in ~36% rows; often ~same time as t_dec) ----
    constexpr double P_AUDIO_MARK = 0.36;  // probability we "mark" audio for a processed frame

    // ---- Person presence (for realism; audio mark only happens if person=true) ----
    constexpr double P_PERSON = 0.85;

    // ---- Output ----
    constexpr const char* CSV_PATH = "latency_sc.csv";
}

// ============================================================
// Utility: sc_time -> seconds double (similar spirit to PerfLogger now_s())
// ============================================================
static inline double t_s(const sc_time& t) { return t.to_seconds(); }

// ============================================================
// Data type: camera frame (with capture timestamp)
// ============================================================
struct Frame {
    int     id = -1;
    sc_time t_cam = SC_ZERO_TIME;
};

// ============================================================
// Shared "latest frame" buffer (overwrite semantics)
// ============================================================
struct LatestFrameBuffer {
    sc_mutex mtx;
    sc_event ev_new;

    Frame    latest;
    bool     has = false;

    void write(const Frame& f) {
        mtx.lock();
        latest = f;
        has = true;
        mtx.unlock();
        ev_new.notify(SC_ZERO_TIME);
    }

    // Wait until we have a frame with id > last_id, then return latest snapshot.
    Frame wait_next(int last_id) {
        while (true) {
            mtx.lock();
            bool ok = has && (latest.id > last_id);
            Frame f = latest;
            mtx.unlock();

            if (ok) return f;
            wait(ev_new);
        }
    }

    // Non-blocking snapshot (may return old id)
    Frame snapshot() {
        mtx.lock();
        Frame f = latest;
        mtx.unlock();
        return f;
    }
};

// ============================================================
// Shared counters for FPS monitor
// ============================================================
struct Counters {
    sc_mutex mtx;
    uint64_t cam_cnt = 0;    // increments every camera capture
    uint64_t det_cnt = 0;    // increments every detector iteration (processed frame)
    uint64_t inf_cnt = 0;    // increments only when ran_infer=1

    void inc_cam() { mtx.lock(); cam_cnt++; mtx.unlock(); }
    void inc_det(bool ran_infer) {
        mtx.lock();
        det_cnt++;
        if (ran_infer) inf_cnt++;
        mtx.unlock();
    }
    void snapshot(uint64_t& cam, uint64_t& det, uint64_t& inf) {
        mtx.lock();
        cam = cam_cnt; det = det_cnt; inf = inf_cnt;
        mtx.unlock();
    }
};

// ============================================================
// CSV Logger (PerfLogger-compatible columns)
// ============================================================
struct CsvLogger {
    std::ofstream f;

    CsvLogger() {
        f.open(cfg::CSV_PATH, std::ios::out | std::ios::trunc);
        f << "frame_id,t_cam,t_pp,t_det_s,t_det_e,t_dec,t_aud,ran_infer\n";
        f.flush();
    }

    void log(int frame_id,
             const sc_time& t_cam,
             const sc_time& t_pp,
             const sc_time& t_det_s,
             const sc_time& t_det_e,
             const sc_time& t_dec,
             const sc_time& t_aud,
             int ran_infer)
    {
        // Use seconds to resemble PerfLogger steady_clock seconds (compare by deltas).
        f << frame_id << ","
          << t_s(t_cam)   << ","
          << t_s(t_pp)    << ","
          << t_s(t_det_s) << ","
          << t_s(t_det_e) << ","
          << t_s(t_dec)   << ","
          << t_s(t_aud)   << ","
          << ran_infer
          << "\n";
        f.flush();
    }
};

// Global-ish singletons (simple for this simulation)
static LatestFrameBuffer g_latest;
static Counters          g_cnt;
static CsvLogger         g_log;

// ============================================================
// 2) Modules
// ============================================================

// ---------------- Camera (independent 30 FPS, overwrite latest) ----------------
SC_MODULE(Camera) {
    std::mt19937 rng;
    std::uniform_real_distribution<double> jitter_ms;

    SC_CTOR(Camera)
        : rng(1),
          jitter_ms(-cfg::CAM_JITTER_MS, cfg::CAM_JITTER_MS)
    {
        SC_THREAD(run);
    }

    void run() {
        int id = 0;
        const double base_ms = 1000.0 / cfg::CAM_FPS;

        while (true) {
            Frame f;
            f.id = id++;
            f.t_cam = sc_time_stamp();

            g_latest.write(f);
            g_cnt.inc_cam();

            double dt = base_ms + jitter_ms(rng);
            if (dt < 1.0) dt = 1.0;
            wait(sc_time(dt, SC_MS));
        }
    }
};

// ---------------- Detector (reads latest frame; preprocess + optional inference) ----------------
SC_MODULE(Detector) {
    std::mt19937 rng;
    std::uniform_real_distribution<double> u01;

    // last state for "reuse detection" semantics
    bool last_person = false;

    SC_CTOR(Detector)
        : rng(2),
          u01(0.0, 1.0)
    {
        SC_THREAD(run);
    }

    double sample_pp_ms() {
        double u = u01(rng);
        if (u < 0.88) return cfg::PP_MS_P10;
        if (u < 0.98) return cfg::PP_MS_P95;
        return cfg::PP_MS_MAX;
    }

    double sample_det_ms() {
        double u = u01(rng);
        if (u < 0.94) return cfg::DET_MS_P50;
        if (u < 0.99) return cfg::DET_MS_P95;
        return cfg::DET_MS_MAX;
    }

    double sample_dec_ms() {
        double u = u01(rng);
        return (u < 0.90) ? cfg::DEC_MS_FAST : cfg::DEC_MS_SLOW;
    }

    void run() {
        int last_seen_id = -1;
        int det_iter = 0;

        while (true) {
            // Wait for a new camera frame (drop-frame behavior: only take latest snapshot)
            Frame fr = g_latest.wait_next(last_seen_id);
            last_seen_id = fr.id;
            det_iter++;

            const int ran_infer = (det_iter % cfg::DET_EVERY_N == 0) ? 1 : 0;

            // --- preprocess ---
            const double pp_ms = sample_pp_ms();
            wait(sc_time(pp_ms, SC_MS));
            sc_time t_pp = sc_time_stamp();

            // --- inference or reuse ---
            sc_time t_det_s = SC_ZERO_TIME;
            sc_time t_det_e = SC_ZERO_TIME;

            bool person = last_person;
            if (ran_infer) {
                t_det_s = sc_time_stamp();
                const double det_ms = sample_det_ms();
                wait(sc_time(det_ms, SC_MS));
                t_det_e = sc_time_stamp();

                // infer result (simplified)
                person = (u01(rng) < cfg::P_PERSON);
                last_person = person;
            }

            // --- decision ---
            const double dec_ms = sample_dec_ms();
            if (dec_ms > 0.0) wait(sc_time(dec_ms, SC_MS));
            sc_time t_dec = sc_time_stamp();

            // --- audio marker (match perf_log: only appears some fraction; often same time as t_dec) ---
            sc_time t_aud = SC_ZERO_TIME;
            if (person && (u01(rng) < cfg::P_AUDIO_MARK)) {
                // In your Pi log, t_aud is essentially "trigger time" (almost no added delay)
                t_aud = sc_time_stamp();
            }

            // If we didn't run infer, keep det timestamps at 0 (clear signal in CSV)
            if (!ran_infer) {
                t_det_s = SC_ZERO_TIME;
                t_det_e = SC_ZERO_TIME;
            }

            // Log record in PerfLogger column order
            g_log.log(fr.id, fr.t_cam, t_pp, t_det_s, t_det_e, t_dec, t_aud, ran_infer);

            // Counters for FPS monitor
            g_cnt.inc_det(ran_infer);
        }
    }
};

// ---------------- FPS Monitor (prints LoopFPS & DetFPS every 1 second) ----------------
SC_MODULE(FpsMonitor) {
    SC_CTOR(FpsMonitor) { SC_THREAD(run); }

    void run() {
        uint64_t cam_prev = 0, det_prev = 0, inf_prev = 0;
        g_cnt.snapshot(cam_prev, det_prev, inf_prev);

        while (true) {
            wait(sc_time(1, SC_SEC));

            uint64_t cam_now = 0, det_now = 0, inf_now = 0;
            g_cnt.snapshot(cam_now, det_now, inf_now);

            const double loop_fps = double(cam_now - cam_prev) / 1.0;
            const double det_fps  = double(inf_now - inf_prev) / 1.0; // match "DetFPS" notion = inference rate

            std::cout << "[FPS] LoopFPS=" << loop_fps
                      << "  DetFPS=" << det_fps
                      << "  (t=" << sc_time_stamp() << ")\n";

            cam_prev = cam_now;
            det_prev = det_now;
            inf_prev = inf_now;
        }
    }
};

// ============================================================
// 3) Top
// ============================================================
int sc_main(int, char**) {
    Camera     cam("Camera");
    Detector   det("Detector");
    FpsMonitor mon("FpsMonitor");

    sc_start(sc_time(cfg::RUN_SECONDS, SC_SEC));
    sc_stop();

    std::cout << "Simulation finished. CSV saved to " << cfg::CSV_PATH << "\n";
    return 0;
}
