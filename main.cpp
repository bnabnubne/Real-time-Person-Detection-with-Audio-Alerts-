#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

#include <opencv2/opencv.hpp>
#include <ncnn/net.h>

#include "yolo-fastestv2.h"
#include "audio_player.hpp"

#define PERF_ENABLE
#include "PerfLogger.hpp"         

#include "third_party/httplib.h"  

// CONFIG  
static constexpr bool   kUseVulkan      = false;
static constexpr int    kDetectEveryN   = 1;      // detect mỗi N frame mới
static constexpr float  kDetThresh      = 0.30f;
static constexpr float  kPersonConf     = 0.50f;
static constexpr int    kLogEveryMs     = 1000;

static constexpr int    kHttpPort       = 8080;
static constexpr int    kJpegQuality    = 75;     
constexpr float SCALE_X = 640.0f / 352.0f;
constexpr float SCALE_Y = 480.0f / 352.0f;

// GStreamer pipeline (Pi camera)
static const std::string kPipeline =
    "libcamerasrc ! "
    "video/x-raw,width=640,height=480,framerate=30/1 ! "
    "videoconvert ! "
    "video/x-raw,format=BGR ! "
    "appsink drop=1 max-buffers=1 sync=false";

// DATA STRUCT 
struct FramePacket {
    cv::Mat frame;  
    uint64_t id = 0;
    

struct DetPacket {
    uint64_t frame_id = 0;
    std::vector<TargetBox> boxes;
    std::chrono::steady_clock::time_point t_done;
};

// SHARED STATE  
// Camera -> Detect
static std::mutex g_mtx_frame;
static std::condition_variable g_cv_frame;
static FramePacket g_latest_frame;
static bool g_have_frame = false;

// Detect -> Logic
static std::mutex g_mtx_det;
static std::condition_variable g_cv_det;
static DetPacket g_latest_det;
static bool g_have_det = false;

// Latest JPEG for HTTP
static std::mutex g_mtx_jpeg;
static std::vector<uchar> g_latest_jpeg;
static uint64_t g_latest_jpeg_id = 0;

// Counters
static std::atomic<uint64_t> g_cap_cnt{0};
static std::atomic<uint64_t> g_det_cnt{0};

// Quit flag
static std::atomic<bool> g_run{true};

// UTILS 
static inline double sec_since(const std::chrono::steady_clock::time_point& t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// UDP sender  
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

class UdpSender {
public:
    UdpSender(const char* ip, int port) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) {
            std::perror("socket");
            ok_ = false;
            return;
        }
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port   = htons(port);
        if (::inet_pton(AF_INET, ip, &addr_.sin_addr) != 1) {
            std::perror("inet_pton");
            ok_ = false;
            return;
        }
        ok_ = true;
    }

    ~UdpSender() {
        if (fd_ >= 0) ::close(fd_);
    }

    bool send_str(const std::string& s) {
        if (!ok_) return false;
        ssize_t n = ::sendto(fd_, s.data(), s.size(), 0,
                             (struct sockaddr*)&addr_, sizeof(addr_));
        return (n == (ssize_t)s.size());
    }

private:
    int fd_{-1};
    bool ok_{false};
    struct sockaddr_in addr_;
};

// HTTP MJPEG SERVER 
static void http_server_thread()
{
    httplib::Server svr;

    // Snapshot
    svr.Get("/snapshot.jpg", [](const httplib::Request&, httplib::Response& res) {
        std::vector<uchar> jpg;
        {
            std::lock_guard<std::mutex> lk(g_mtx_jpeg);
            jpg = g_latest_jpeg;
        }
        if (jpg.empty()) {
            res.status = 503;
            res.set_content("no frame yet\n", "text/plain");
            return;
        }
        res.set_content(reinterpret_cast<const char*>(jpg.data()), jpg.size(), "image/jpeg");
        res.set_header("Cache-Control", "no-store");
    });

    // MJPEG stream
    svr.Get("/stream.mjpg", [](const httplib::Request&, httplib::Response& res) {
    const std::string boundary = "frame";

    // Header chuẩn MJPEG
    res.status = 200;
    res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    res.set_header("Pragma", "no-cache");
    res.set_header("Expires", "0");

    // Content-Type đúng format
    const std::string ctype = "multipart/x-mixed-replace; boundary=" + boundary;

    res.set_chunked_content_provider(
        ctype,
        [boundary](size_t, httplib::DataSink& sink) {
            uint64_t last_id = 0;

            while (g_run.load()) {
                if (!sink.is_writable()) break;

                std::vector<uchar> jpg;
                uint64_t jpg_id = 0;

                {
                    std::lock_guard<std::mutex> lk(g_mtx_jpeg);
                    jpg    = g_latest_jpeg;
                    jpg_id = g_latest_jpeg_id;
                }

                if (!jpg.empty() && jpg_id != last_id) {
                    last_id = jpg_id;

                    std::ostringstream ss;
                    ss << "--" << boundary << "\r\n";
                    ss << "Content-Type: image/jpeg\r\n";
                    ss << "Content-Length: " << jpg.size() << "\r\n\r\n";

                    const std::string head = ss.str();
                    sink.write(head.data(), head.size());
                    sink.write(reinterpret_cast<const char*>(jpg.data()), jpg.size());
                    sink.write("\r\n", 2);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            // kết thúc stream
            std::string tail = "--" + boundary + "--\r\n";
            sink.write(tail.data(), tail.size());
            sink.done();
            return true;
        }
    );
});

    std::cout << "[HTTP] MJPEG server on 0.0.0.0:" << kHttpPort
              << "  /stream.mjpg  /snapshot.jpg\n";

    // blocking
    svr.listen("0.0.0.0", kHttpPort);
}

// THREADS  
static void camera_thread()
{
    cv::setNumThreads(1);

    cv::VideoCapture cap(kPipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened()) {
        std::cerr << "[CAM] Failed to open camera pipeline\n";
        g_run = false;
        return;
    }

    cv::Mat frame;
    uint64_t frame_id = 0;

    std::vector<int> enc_params = { cv::IMWRITE_JPEG_QUALITY, kJpegQuality };

    while (g_run.load()) {
         

        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "[CAM] Failed to grab frame\n";
            g_run = false;
            break;
        }
         

        frame_id++;
        g_cap_cnt.fetch_add(1, std::memory_order_relaxed);

        // publish latest frame for detector (clone)
        {
            FramePacket pkt;
            pkt.id = frame_id;
            pkt.frame = frame.clone();

            std::lock_guard<std::mutex> lk(g_mtx_frame);
            g_latest_frame = std::move(pkt);
            g_have_frame = true;
        }
        g_cv_frame.notify_one();

        // encode JPEG for MJPEG server
        std::vector<uchar> jpg;
        cv::imencode(".jpg", frame, jpg, enc_params);

        {
            std::lock_guard<std::mutex> lk(g_mtx_jpeg);
            g_latest_jpeg = std::move(jpg);
            g_latest_jpeg_id = frame_id;
        }
 
    }

    cap.release();
}

static void detect_thread(yoloFastestv2* detector)
{
    UdpSender udp("127.0.0.1", 9001);

    uint64_t last_seen_id = 0;
    int skip_counter = 0;

    // FPS window
    auto t_fps_last = std::chrono::steady_clock::now();
    uint64_t det_cnt_window = 0;
    uint64_t cap_cnt_prev   = 0;
    double loop_fps = 0.0;
    double det_fps  = 0.0;

    while (g_run.load()) {
        FramePacket pkt;

        // wait for latest frame
        {
            std::unique_lock<std::mutex> lk(g_mtx_frame);
            g_cv_frame.wait(lk, [] { return !g_run.load() || g_have_frame; });
            if (!g_run.load()) break;

            pkt = g_latest_frame;
            g_have_frame = false;
        }

        if (pkt.id == last_seen_id) continue;
        last_seen_id = pkt.id;

        // PERF begin per-frame  
        PERF_FRAME_BEGIN((int)pkt.id);
        PERF_MARK_CAM(); // "frame arrived to detect thread"

        skip_counter++;
        bool run_det = (kDetectEveryN <= 1) ? true : (skip_counter % kDetectEveryN == 0);
        PERF_SET_RAN_INFER(run_det);

        std::vector<TargetBox> boxes;

        // preprocess -> 352x352 for YOLO
        cv::Mat yolo_in;
        cv::resize(pkt.frame, yolo_in, cv::Size(352, 352));
        PERF_MARK_PP();

        if (run_det) {
            PERF_MARK_DET_S();
            detector->detection(yolo_in, boxes, kDetThresh);
            PERF_MARK_DET_E();

            det_cnt_window++;
        }

        // FPS update every ~1s
        {
            auto t_now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(t_now - t_fps_last).count();
            if (sec >= 1.0) {
                uint64_t cap_now   = g_cap_cnt.load(std::memory_order_relaxed);
                uint64_t cap_delta = cap_now - cap_cnt_prev;

                loop_fps = cap_delta / sec;
                det_fps  = det_cnt_window / sec;

                cap_cnt_prev    = cap_now;
                det_cnt_window  = 0;
                t_fps_last      = t_now;
            }
        }

        // person detect
        bool person = false; 
        for (auto &b : boxes) {
            if (b.cate == 0) {
                person = true;
                break;
            }                 
        }

        // timestamp epoch seconds (float)
        double ts = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0;

        // build JSON
        std::ostringstream ss;
        ss << "{";
        ss << "\"ts\":" << std::fixed << std::setprecision(3) << ts;
        ss << ",\"frame_id\":" << pkt.id;
        ss << ",\"loop_fps\":" << std::fixed << std::setprecision(2) << loop_fps;
        ss << ",\"det_fps\":"  << std::fixed << std::setprecision(2) << det_fps;
        ss << ",\"person\":" << (person ? "true" : "false");
        ss << ",\"detections\":[";

        for (size_t i=0; i<boxes.size(); i++) {
            const auto& b = boxes[i];

            // bbox from 352-space -> scale back to 640x480
            float x1 = b.x1 * SCALE_X;
            float y1 = b.y1 * SCALE_Y;
            float x2 = b.x2 * SCALE_X;
            float y2 = b.y2 * SCALE_Y;

            ss << "{"
               << "\"cls\":\"" << (b.cate==0 ? "person" : "other") << "\""
               << ",\"conf\":" << std::fixed << std::setprecision(3) << b.score
               << ",\"bbox\":[" << x1 << "," << y1 << "," << x2 << "," << y2 << "]"
               << "}";
            if (i+1<boxes.size()) ss << ",";
        }
        ss << "]}";
        
        bool will_beep = false;
        for (const auto& b : boxes) {
            if (b.cate == 0 && b.score >= kPersonConf) { will_beep = true; break; }
        }
        if (will_beep) PERF_MARK_AUD();

        udp.send_str(ss.str());
        PERF_MARK_DEC();        // after "decision/send"
        PERF_FRAME_COMMIT();    // commit per frame

        g_det_cnt.fetch_add(1, std::memory_order_relaxed);

        // publish for logic/audio (optional)
        {
            DetPacket det;
            det.frame_id = pkt.id;
            det.boxes = boxes;
            det.t_done = std::chrono::steady_clock::now();

            std::lock_guard<std::mutex> lk(g_mtx_det);
            g_latest_det = std::move(det);
            g_have_det = true;
        }
        g_cv_det.notify_one();
    }
}

static void logic_thread()
{
    AudioPlayer player("/home/pi/person_detected.wav", 2000);

    DetPacket last_det;
    bool have_last = false;

    auto t_start = std::chrono::steady_clock::now();
    auto t_log0  = t_start;

    uint64_t cap_prev = 0;
    uint64_t det_prev = 0;

    while (g_run.load()) {
        // wait detection (timeout to allow log)
        {
            std::unique_lock<std::mutex> lk(g_mtx_det);
            g_cv_det.wait_for(lk, std::chrono::milliseconds(200), [] {
                return !g_run.load() || g_have_det;
            });
            if (!g_run.load()) break;

            if (g_have_det) {
                last_det = g_latest_det;
                g_have_det = false;
                have_last = true;
            }
        }

        if (have_last) {
            bool person_found = false;
            for (const auto& b : last_det.boxes) {
                if (b.cate == 0 && b.score >= kPersonConf) { person_found = true; break; }
            }
            if (person_found) player.play();
        }

        // log fps
        auto now = std::chrono::steady_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_log0).count();
        if (ms >= kLogEveryMs) {
            double sec = ms / 1000.0;

            uint64_t cap_now = g_cap_cnt.load(std::memory_order_relaxed);
            uint64_t det_now = g_det_cnt.load(std::memory_order_relaxed);

            double loop_fps = (cap_now - cap_prev) / std::max(sec, 1e-6);
            double det_fps  = (det_now - det_prev) / std::max(sec, 1e-6);

            cap_prev = cap_now;
            det_prev = det_now;

            std::cout
                << "[t=" << sec_since(t_start) << "s] "
                << "LoopFPS=" << loop_fps
                << "  DetFPS="  << det_fps
                << "  total_loop=" << cap_now
                << "  total_det="  << det_now
                << "\n";

            t_log0 = now;
        }
    }
}

// main
int main()
{
    if (kUseVulkan) ncnn::create_gpu_instance();

    // PERF
    PERF_INIT("perf_log.csv");

    yoloFastestv2 detector;
    detector.init(kUseVulkan);

    if (detector.loadModel("/home/pi/models/yolo-fastestv2-opt.param",
                           "/home/pi/models/yolo-fastestv2-opt.bin") != 0)
    {
        std::cerr << "Failed to load YOLOFastestV2 model\n";
        if (kUseVulkan) ncnn::destroy_gpu_instance();
        return -1;
    }

    std::cout << "[INFO] Start. Vulkan=" << (kUseVulkan ? "ON":"OFF")
              << " detect_every=" << kDetectEveryN
              << " headless=ON\n";

    std::thread th_http(http_server_thread);
    std::thread th_cam(camera_thread);
    std::thread th_det(detect_thread, &detector);
    std::thread th_log(logic_thread);

    // nếu cam chết -> stop all
    th_cam.join();
    g_run = false;
    g_cv_frame.notify_all();
    g_cv_det.notify_all();

    th_det.join();
    th_log.join();

    // http listen blocking -> detach là ok demo
    th_http.detach();

    if (kUseVulkan) ncnn::destroy_gpu_instance();
    std::cout << "[INFO] Exit.\n";
    return 0;
}