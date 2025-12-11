// FILE: SystemC_Logger.cpp
// Mô tả: Module nhận tín hiệu AI và ghi log hệ thống
// Tác giả: Nhom_KC4.0 - Member 3

#include <systemc.h>
#include <iostream>
#include <fstream>
#include <iomanip>

// --- MODULE LOGGER ---
SC_MODULE (DashboardLogger) {
    // 1. Cổng giao tiếp (Ports)
    sc_in_clk     clk;          // Clock hệ thống
    sc_in<bool>   ai_trigger;   // Tín hiệu từ AI (1 = Có vật thể)
    sc_in<int>    object_id;    // ID vật thể (1=Person, 0=Background)
    
    // 2. Logic chính
    void log_process() {
        // Mở file log mô phỏng
        std::ofstream logFile;
        logFile.open("dashboard_log.txt");

        while (true) {
            wait(); // Chờ sườn dương của clock

            if (ai_trigger.read() == true) {
                // Lấy thời gian hiện tại
                sc_time now = sc_time_stamp();
                
                // In ra màn hình console (Simulation Console)
                std::cout << "[SYSTEMC] @" << now << " | ALERT: Object ID " << object_id.read() << " Detected!" << std::endl;
                
                // Ghi vào file text (để Python đọc - giả lập)
                logFile << now << ",DETECTED," << object_id.read() << std::endl;
            }
        }
        logFile.close();
    }

    // 3. Constructor
    SC_CTOR (DashboardLogger) {
        SC_THREAD(log_process);
        sensitive << clk.pos(); // Nhạy với cạnh lên clock
        dont_initialize();
    }
};

// --- TESTBENCH (CHƯƠNG TRÌNH CHẠY THỬ) ---
int sc_main(int argc, char* argv[]) {
    // Tín hiệu kết nối
    sc_clock clk("SystemClock", 10, SC_NS); // Chu kỳ 10ns
    sc_signal<bool> ai_valid;
    sc_signal<int>  obj_type;

    // Khởi tạo Module
    DashboardLogger logger("MyLogger");
    logger.clk(clk);
    logger.ai_trigger(ai_valid);
    logger.object_id(obj_type);

    // Bắt đầu mô phỏng (Trace file)
    sc_trace_file *wf = sc_create_vcd_trace_file("waveforms");
    sc_trace(wf, clk, "clock");
    sc_trace(wf, ai_valid, "ai_trigger");

    std::cout << "--- STARTING SIMULATION ---" << std::endl;

    // Giả lập tình huống: 
    // 0-20ns: Không có gì
    ai_valid = 0; obj_type = 0;
    sc_start(20, SC_NS);

    // 20-40ns: AI phát hiện người (ID=1)
    ai_valid = 1; obj_type = 1;
    sc_start(20, SC_NS);

    // 40-60ns: AI mất dấu
    ai_valid = 0;
    sc_start(20, SC_NS);

    std::cout << "--- SIMULATION FINISHED ---" << std::endl;
    sc_close_vcd_trace_file(wf);
    return 0;
}
