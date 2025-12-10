#include "yolo-fastestv2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cassert>

// ---------------------
//  Constructor
// ---------------------
yoloFastestv2::yoloFastestv2()
{
    numOutput   = 2;
    numThreads  = 4;
    numAnchor   = 3;
    numCategory = 80;
    nmsThresh   = 0.25f;
    inputWidth  = 352;
    inputHeight = 352;

    // anchors giống file gốc
    std::vector<float> bias{
        12.64f, 19.39f,
        37.88f, 51.48f,
        55.71f, 138.31f,
        126.91f, 78.23f,
        131.57f, 214.55f,
        279.92f, 258.87f
    };
    anchor.assign(bias.begin(), bias.end());
}

yoloFastestv2::~yoloFastestv2()
{
}

// ---------------------
//  init() – set NCNN option
// ---------------------
int yoloFastestv2::init(bool use_vulkan_compute)
{
    ncnn::Option& opt = net.opt;

    opt.num_threads              = numThreads;
    opt.use_winograd_convolution = true;
    opt.use_sgemm_convolution    = true;
    opt.use_int8_inference       = true;
    opt.use_vulkan_compute       = use_vulkan_compute;
    opt.use_fp16_packed          = true;
    opt.use_fp16_storage         = true;
    opt.use_fp16_arithmetic      = true;
    opt.use_int8_storage         = true;
    opt.use_int8_arithmetic      = true;
    opt.use_packing_layout       = true;

    return 0;
}

// ---------------------
//  load model
// ---------------------
int yoloFastestv2::loadModel(const char* paramPath, const char* binPath)
{
    if (net.load_param(paramPath) != 0)
    {
        std::fprintf(stderr, "load_param failed: %s\n", paramPath);
        return -1;
    }
    if (net.load_model(binPath) != 0)
    {
        std::fprintf(stderr, "load_model failed: %s\n", binPath);
        return -1;
    }

    std::printf("NCNN model init success...\n");
    return 0;
}

// ---------------------
//  IoU helper
// ---------------------
static float intersection_area(const TargetBox& a, const TargetBox& b)
{
    if (a.x1 > b.x2 || a.x2 < b.x1 || a.y1 > b.y2 || a.y2 < b.y1)
        return 0.f;

    float inter_width  = (float)(std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
    float inter_height = (float)(std::min(a.y2, b.y2) - std::max(a.y1, b.y1));

    return inter_width * inter_height;
}

static bool scoreSort(const TargetBox& a, const TargetBox& b)
{
    return (a.score > b.score);
}

// ---------------------
//  NMS
// ---------------------
int yoloFastestv2::nmsHandle(std::vector<TargetBox>& tmpBoxes,
                             std::vector<TargetBox>& dstBoxes)
{
    dstBoxes.clear();
    if (tmpBoxes.empty())
        return 0;

    std::sort(tmpBoxes.begin(), tmpBoxes.end(), scoreSort);

    std::vector<int> picked;
    picked.reserve(tmpBoxes.size());

    for (size_t i = 0; i < tmpBoxes.size(); i++)
    {
        int keep = 1;
        for (size_t j = 0; j < picked.size(); j++)
        {
            float inter_area = intersection_area(tmpBoxes[i], tmpBoxes[picked[j]]);
            float union_area = tmpBoxes[i].area() + tmpBoxes[picked[j]].area() - inter_area;
            if (union_area <= 0.f)
                continue;
            float IoU = inter_area / union_area;

            if (IoU > nmsThresh && tmpBoxes[i].cate == tmpBoxes[picked[j]].cate)
            {
                keep = 0;
                break;
            }
        }
        if (keep)
            picked.push_back((int)i);
    }

    dstBoxes.reserve(picked.size());
    for (size_t i = 0; i < picked.size(); i++)
        dstBoxes.push_back(tmpBoxes[picked[i]]);

    return 0;
}

// ---------------------
//  getCategory()
// ---------------------
int yoloFastestv2::getCategory(const float* values, int index,
                               int& category, float& score)
{
    float objScore = values[4 * numAnchor + index];
    float tmp      = 0.f;
    category       = -1;
    score          = 0.f;

    int base = 4 * numAnchor + numAnchor;
    for (int i = 0; i < numCategory; i++)
    {
        float clsScore = values[base + i] * objScore;
        if (clsScore > tmp)
        {
            tmp      = clsScore;
            score    = clsScore;
            category = i;
        }
    }
    return 0;
}

// ---------------------
//  post-process feature maps
// ---------------------
int yoloFastestv2::predHandle(const ncnn::Mat* out,
                              std::vector<TargetBox>& dstBoxes,
                              float scaleW, float scaleH,
                              float thresh)
{
    std::vector<TargetBox> tmpBoxes;

    for (int i = 0; i < numOutput; i++)
    {
        const ncnn::Mat& feat = out[i];

        int outH = feat.c;
        int outW = feat.h;
        int outC = feat.w;

        assert(inputHeight / outH == inputWidth / outW);
        int stride = inputHeight / outH;

        for (int h = 0; h < outH; h++)
        {
            const float* values = feat.channel(h);
            for (int w = 0; w < outW; w++)
            {
                for (int b = 0; b < numAnchor; b++)
                {
                    int   cate = -1;
                    float sc   = -1.f;
                    getCategory(values, b, cate, sc);

                    if (cate < 0 || sc <= thresh)
                    {
                        continue;
                    }

                    float bcx = ((values[b * 4 + 0] * 2.f - 0.5f) + w) * stride;
                    float bcy = ((values[b * 4 + 1] * 2.f - 0.5f) + h) * stride;
                    float bw  = std::pow(values[b * 4 + 2] * 2.f, 2.f) *
                                anchor[i * numAnchor * 2 + b * 2 + 0];
                    float bh  = std::pow(values[b * 4 + 3] * 2.f, 2.f) *
                                anchor[i * numAnchor * 2 + b * 2 + 1];

                    TargetBox box;
                    box.x1   = (int)((bcx - 0.5f * bw) * scaleW);
                    box.y1   = (int)((bcy - 0.5f * bh) * scaleH);
                    box.x2   = (int)((bcx + 0.5f * bw) * scaleW);
                    box.y2   = (int)((bcy + 0.5f * bh) * scaleH);
                    box.score = sc;
                    box.cate  = cate;

                    tmpBoxes.push_back(box);
                }
                values += outC;
            }
        }
    }

    return nmsHandle(tmpBoxes, dstBoxes);
}

// ---------------------
//  detection()
// ---------------------
int yoloFastestv2::detection(const cv::Mat& srcImg,
                             std::vector<TargetBox>& dstBoxes,
                             float thresh)
{
    dstBoxes.clear();
    if (srcImg.empty())
        return -1;

    float scaleW = (float)srcImg.cols / (float)inputWidth;
    float scaleH = (float)srcImg.rows / (float)inputHeight;

    ncnn::Mat inputImg = ncnn::Mat::from_pixels_resize(
        srcImg.data,
        ncnn::Mat::PIXEL_BGR,
        srcImg.cols,
        srcImg.rows,
        inputWidth,
        inputHeight);

    const float mean_vals[3] = {0.f, 0.f, 0.f};
    const float norm_vals[3] = {1.f / 255.f, 1.f / 255.f, 1.f / 255.f};
    inputImg.substract_mean_normalize(mean_vals, norm_vals);

    ncnn::Extractor ex = net.create_extractor();
    // trong bản ncnn hiện tại, số thread lấy từ net.opt.num_threads

    ex.input("input.1", inputImg);

    ncnn::Mat out[2];
    ex.extract("794", out[0]); // 22x22
    ex.extract("796", out[1]); // 11x11

    predHandle(out, dstBoxes, scaleW, scaleH, thresh);
    return 0;
}