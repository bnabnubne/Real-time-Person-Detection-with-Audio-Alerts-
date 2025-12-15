#ifndef YOLO_FASTESTV2_H
#define YOLO_FASTESTV2_H

#include <vector>
#include <opencv2/opencv.hpp>
#include <net.h>   // từ ncnn

class TargetBox
{
private:
    float getWidth()  const { return (float)(x2 - x1); }
    float getHeight() const { return (float)(y2 - y1); }

public:
    int   x1;
    int   y1;
    int   x2;
    int   y2;
    int   cate;   // class id
    float score;  // confidence

    float area() const { return getWidth() * getHeight(); }
};
 
//  yoloFastestv2 

class yoloFastestv2
{
private:
    ncnn::Net          net;
    std::vector<float> anchor;

    int   numAnchor;
    int   numOutput;
    int   numThreads;
    int   numCategory;
    int   inputWidth;
    int   inputHeight;
    float nmsThresh;

    int nmsHandle(std::vector<TargetBox>& tmpBoxes,
                  std::vector<TargetBox>& dstBoxes);
    int getCategory(const float* values, int index,
                    int& category, float& score);
    int predHandle(const ncnn::Mat* out,
                   std::vector<TargetBox>& dstBoxes,
                   float scaleW, float scaleH,
                   float thresh);

public:
    yoloFastestv2();
    ~yoloFastestv2();

    int init(bool use_vulkan_compute = false);
    int loadModel(const char* paramPath, const char* binPath);
    int detection(const cv::Mat& srcImg,
                  std::vector<TargetBox>& dstBoxes,
                  float thresh = 0.3f);
};

#endif  