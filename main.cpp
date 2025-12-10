#include <iostream>
#include <vector>
#include <chrono>

#include <opencv2/opencv.hpp>
#include <ncnn/net.h>
#include "yolo-fastestv2.h"
#include "audio_player.hpp"

int main()
{
    // Khởi động Vulkan cho ncnn
    ncnn::create_gpu_instance();

    // ---- Init detector ----
    yoloFastestv2 detector;
    detector.init(true);  // true => dùng Vulkan GPU
    if (detector.loadModel("/home/pi/models/yolo-fastestv2-opt.param",
                           "/home/pi/models/yolo-fastestv2-opt.bin") != 0)
    {
        std::cerr << "Failed to load YOLOFastestV2 model\n";
        return -1;
    }

    // ---- Audio player ----
    AudioPlayer player("/home/pi/person_detected.wav", 2000);

    // ---- Open camera qua GStreamer + libcamera ----
    std::string pipeline =
        "libcamerasrc ! "
        "video/x-raw,width=352,height=352,framerate=30/1 ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink drop=1 max-buffers=1";

    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened())
    {
        std::cerr << "Failed to open camera via GStreamer pipeline\n";
        ncnn::destroy_gpu_instance();
        return -1;
    }

    std::cout << "[INFO] Camera started. Press 'q' to quit.\n";

    cv::Mat frame;

    int frame_id = 0;            // tổng số frame xử lý (loop)
    int det_frame_count = 0;     // số frame thực sự chạy YOLO
    double loop_fps = 0.0;
    double det_fps  = 0.0;

    bool have_last_boxes = false;
    std::vector<TargetBox> last_boxes;

    // mốc thời gian bắt đầu
    auto start_time = std::chrono::high_resolution_clock::now();

    while (true)
    {
        if (!cap.read(frame) || frame.empty())
        {
            std::cerr << "Failed to grab frame\n";
            break;
        }

        frame_id++;

        bool run_detection = (frame_id % 3 == 0);  // detect 1/3 số frame
        std::vector<TargetBox> boxes;

        if (run_detection)
        {
            detector.detection(frame, boxes, 0.3f);
            last_boxes = boxes;
            have_last_boxes = true;
            det_frame_count++;    // đếm 1 lần YOLO
        }
        else if (have_last_boxes)
        {
            boxes = last_boxes; // dùng lại bbox frame trước
        }

        bool person_found = false;
        for (const auto& box : boxes)
        {
            if (box.cate == 0 && box.score >= 0.5f)
                person_found = true;

            cv::rectangle(frame,
                          cv::Point(box.x1, box.y1),
                          cv::Point(box.x2, box.y2),
                          cv::Scalar(0, 255, 0),
                          2);
            char text[64];
            std::snprintf(text, sizeof(text), "id:%d %.2f", box.cate, box.score);
            cv::putText(frame, text,
                        cv::Point(box.x1, std::max(0, box.y1 - 5)),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.5,
                        cv::Scalar(0, 255, 0),
                        1);
        }

        if (person_found)
            player.play();

        // ======= TÍNH FPS THEO TỔNG THỜI GIAN =======
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        double sec = std::max(elapsed.count(), 1e-6);

        loop_fps = frame_id      / sec;  // loop FPS
        det_fps  = det_frame_count / sec; // YOLO FPS thực

        // ======= VẼ FPS LÊN MÀN HÌNH =======
        char line1[64], line2[64];
        std::snprintf(line1, sizeof(line1), "Loop FPS: %.1f", loop_fps);
        std::snprintf(line2, sizeof(line2), "Det FPS:  %.1f", det_fps);

        cv::putText(frame, line1,
                    cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.7,
                    cv::Scalar(0, 255, 255),
                    2);
        cv::putText(frame, line2,
                    cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.7,
                    cv::Scalar(0, 255, 255),
                    2);

        cv::imshow("YOLOFastestV2 NCNN C++", frame);
        char key = (char)cv::waitKey(1);
        if (key == 'q' || key == 27)
            break;
    }

    cap.release();
    cv::destroyAllWindows();
    ncnn::destroy_gpu_instance();
    return 0;
}