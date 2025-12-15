#ifndef PERF_LOGGER_HPP
#define PERF_LOGGER_HPP

#include <string>
#include <fstream>
#include <chrono>

namespace perf_detail {
    using clk = std::chrono::steady_clock;
    inline double now_s() {
        return std::chrono::duration<double>(clk::now().time_since_epoch()).count();
    }
    struct FrameRec {
        int    id = -1;
        double t_cam = 0, t_pp = 0, t_det_s = 0, t_det_e = 0, t_dec = 0, t_aud = 0;
        int    ran_infer = 0;
    };
    class Logger {
    public:
        Logger() = default;
        void init(const std::string& path) {
            if (ofs_.is_open()) return;

            ofs_.open(path, std::ios::out | std::ios::app);
            if (!ofs_) return;

            // chỉ ghi header nếu file đang rỗng
            if (ofs_.tellp() == 0) {
                ofs_ << "frame_id,t_cam,t_pp,t_det_s,t_det_e,t_dec,t_aud,ran_infer\n";
                ofs_.flush();
            }
        }
        void begin(int frame_id) { cur_ = {}; cur_.id = frame_id; }
        void mark_cam()   { cur_.t_cam   = now_s(); }
        void mark_pp()    { cur_.t_pp    = now_s(); }
        void mark_det_s() { cur_.t_det_s = now_s(); }
        void mark_det_e() { cur_.t_det_e = now_s(); }
        void mark_dec()   { cur_.t_dec   = now_s(); }
        void mark_aud()   { cur_.t_aud   = now_s(); }
        void set_ran_infer(bool ran) { cur_.ran_infer = ran ? 1 : 0; }
        void commit() {
            if (!ofs_) return;
            ofs_ << cur_.id << "," << cur_.t_cam << "," << cur_.t_pp << ","
                 << cur_.t_det_s << "," << cur_.t_det_e << ","
                 << cur_.t_dec   << "," << cur_.t_aud  << ","
                 << cur_.ran_infer << "\n";
            ofs_.flush();
        }
    private:
        std::ofstream ofs_;
        FrameRec cur_;
    };

    inline Logger& singleton() { static Logger L; return L; }
} // namespace perf_detail

#ifdef PERF_ENABLE
    #define PERF_INIT(path)           do{ perf_detail::singleton().init(path); }while(0)
    #define PERF_FRAME_BEGIN(id)      do{ perf_detail::singleton().begin((id)); }while(0)
    #define PERF_MARK_CAM()           do{ perf_detail::singleton().mark_cam(); }while(0)
    #define PERF_MARK_PP()            do{ perf_detail::singleton().mark_pp(); }while(0)
    #define PERF_MARK_DET_S()         do{ perf_detail::singleton().mark_det_s(); }while(0)
    #define PERF_MARK_DET_E()         do{ perf_detail::singleton().mark_det_e(); }while(0)
    #define PERF_MARK_DEC()           do{ perf_detail::singleton().mark_dec(); }while(0)
    #define PERF_MARK_AUD()           do{ perf_detail::singleton().mark_aud(); }while(0)
    #define PERF_SET_RAN_INFER(b)     do{ perf_detail::singleton().set_ran_infer((b)); }while(0)
    #define PERF_FRAME_COMMIT()       do{ perf_detail::singleton().commit(); }while(0)
#else
    // no-op macros when PERF_ENABLE not defined
    #define PERF_INIT(path)           do{}while(0)
    #define PERF_FRAME_BEGIN(id)      do{}while(0)
    #define PERF_MARK_CAM()           do{}while(0)
    #define PERF_MARK_PP()            do{}while(0)
    #define PERF_MARK_DET_S()         do{}while(0)
    #define PERF_MARK_DET_E()         do{}while(0)
    #define PERF_MARK_DEC()           do{}while(0)
    #define PERF_MARK_AUD()           do{}while(0)
    #define PERF_SET_RAN_INFER(b)     do{}while(0)
    #define PERF_FRAME_COMMIT()       do{}while(0)
#endif

#endif  