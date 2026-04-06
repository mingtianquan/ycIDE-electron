#ifndef YOLO11_H

#define YOLO11_H



#include "pch.h"

#include "layer.h"

#include "net.h"



#if defined(USE_NCNN_SIMPLEOCV)

#include "simpleocv.h"

#else

#include <opencv2/core/core.hpp>

#include <opencv2/highgui/highgui.hpp>

#include <opencv2/imgproc/imgproc.hpp>

#include <opencv2/opencv.hpp>

#endif



#include <float.h>

#include <stdio.h>

#include <vector>

#include <algorithm>

#include <string>

#include <fstream>

#include <utility>

#include <cmath>



#ifdef _WIN32

#include <direct.h>
#include <windows.h>

#define getcwd _getcwd
#define DLLAPI __declspec(dllexport)

#else

#include <unistd.h>
#define DLLAPI

#endif



// --- Structs ---



struct KeyPoint {

    cv::Point2f p;

    float prob;

};



struct Character {

    int id;

    float prob;

};



struct Object {

    cv::Rect_<float> rect;

    cv::RotatedRect rrect;

    int label;

    float prob;

    std::vector<KeyPoint> keypoints;

    cv::Mat mask;

    int gindex;



    // OCR fields (keeping these in common Object for API simplicity)

    int orientation;

    std::vector<Character> text;

};



struct Pose

{

    float confidence;

    cv::Rect_<float> rect;

    std::vector<float> kps;

};



// --- Utilities ---



static inline float sigmoid(float x) {

    return 1.0f / (1.0f + expf(-x));

}



static inline float intersection_area(const Object& a, const Object& b, bool is_obb = false) {

    if (is_obb) {

        // Prefer rotated IoU for OBB quality; fallback to axis-aligned on any�쳣.

        try {

            if (!std::isfinite(a.rrect.center.x) || !std::isfinite(a.rrect.center.y) || !std::isfinite(a.rrect.angle)) throw 0;

            if (!std::isfinite(b.rrect.center.x) || !std::isfinite(b.rrect.center.y) || !std::isfinite(b.rrect.angle)) throw 0;

            if (!std::isfinite(a.rrect.size.width) || !std::isfinite(a.rrect.size.height)) throw 0;

            if (!std::isfinite(b.rrect.size.width) || !std::isfinite(b.rrect.size.height)) throw 0;

            if (a.rrect.size.width <= 0.f || a.rrect.size.height <= 0.f) throw 0;

            if (b.rrect.size.width <= 0.f || b.rrect.size.height <= 0.f) throw 0;

            std::vector<cv::Point2f> intersection;

            cv::rotatedRectangleIntersection(a.rrect, b.rrect, intersection);

            float ia = intersection.empty() ? 0.f : cv::contourArea(intersection);

            if (std::isfinite(ia) && ia > 0.f) return ia;

            return 0.f;

        }

        catch (...) {

            cv::Rect_<float> inter = a.rect & b.rect;

            float ia = inter.area();

            return (std::isfinite(ia) && ia > 0.f) ? ia : 0.f;

        }

    }

    cv::Rect_<float> inter = a.rect & b.rect;

    return inter.area();

}



static inline void qsort_descent_inplace(std::vector<Object>& objects) {

    if (objects.empty()) return;

    std::sort(objects.begin(), objects.end(), [](const Object& a, const Object& b) {

        return a.prob > b.prob;

        });

}



static inline void nms_sorted_bboxes(const std::vector<Object>& objects, std::vector<int>& picked, float nms_threshold, bool is_obb = false) {

    picked.clear();

    const int n = (int)objects.size();

    std::vector<float> areas(n);

    for (int i = 0; i < n; i++) {

        if (is_obb) {

            float a = objects[i].rrect.size.area();

            areas[i] = std::isfinite(a) && a > 0.f ? a : 0.f;

        }

        else {

            float a = objects[i].rect.area();

            areas[i] = std::isfinite(a) && a > 0.f ? a : 0.f;

        }

    }

    for (int i = 0; i < n; i++) {

        const Object& a = objects[i];

        int keep = 1;

        for (int j = 0; j < (int)picked.size(); j++) {

            const Object& b = objects[picked[j]];

            if (a.label != b.label) continue;

            float inter_area = intersection_area(a, b, is_obb);

            float union_area = areas[i] + areas[picked[j]] - inter_area;

            if (!(union_area > 1e-6f) || !std::isfinite(union_area)) continue;

            if (inter_area / union_area > nms_threshold) keep = 0;

        }

        if (keep) picked.push_back(i);

    }

}



static inline void decode_bbox(const ncnn::Mat& pred_grid, int x, int y, int stride, float score, int label, Object& obj, int reg_max_1, bool decoded_head) {

    float pred_ltrb[4];

    if (decoded_head) {

        // decoded head outputs [cx, cy, w, h] directly in input image scale

        const float cx = pred_grid[0];

        const float cy = pred_grid[1];

        const float bw = std::max(fabsf(pred_grid[2]), 1e-3f);

        const float bh = std::max(fabsf(pred_grid[3]), 1e-3f);

        obj.rect.x = cx - bw * 0.5f;

        obj.rect.y = cy - bh * 0.5f;

        obj.rect.width = bw;

        obj.rect.height = bh;

    }

    else if (reg_max_1 > 1) {

        ncnn::Mat pred_bbox = pred_grid.range(0, reg_max_1 * 4).reshape(reg_max_1, 4).clone();

        {

            ncnn::Layer* softmax = ncnn::create_layer("Softmax");

            ncnn::ParamDict pd; pd.set(0, 1); pd.set(1, 1);

            softmax->load_param(pd);

            ncnn::Option opt; opt.num_threads = 1;

            softmax->create_pipeline(opt);

            softmax->forward_inplace(pred_bbox, opt);

            softmax->destroy_pipeline(opt);

            delete softmax;

        }

        for (int k = 0; k < 4; k++) {

            float dis = 0.f;

            const float* dis_after_sm = pred_bbox.row(k);

            for (int l = 0; l < reg_max_1; l++) dis += l * dis_after_sm[l];

            pred_ltrb[k] = dis * stride;

        }

    }

    else {

        // reg_max=1 models output 4 direct distances.

        for (int k = 0; k < 4; k++) pred_ltrb[k] = pred_grid[k] * stride;

    }

    float pb_cx = (x + 0.5f) * stride;

    float pb_cy = (y + 0.5f) * stride;

    if (!decoded_head) {

        obj.rect.x = pb_cx - pred_ltrb[0];

        obj.rect.y = pb_cy - pred_ltrb[1];

        obj.rect.width = pb_cx + pred_ltrb[2] - obj.rect.x;

        obj.rect.height = pb_cy + pred_ltrb[3] - obj.rect.y;

    }

    obj.label = label;

    obj.prob = score;

}



static inline void generate_proposals(const ncnn::Mat& pred, int stride, const ncnn::Mat& in_pad, float prob_threshold, std::vector<Object>& objects, int task_type, int reg_max_1, bool decoded_head, int index_offset, const ncnn::Mat& extra_blob = ncnn::Mat(), bool is_yolo26 = false, int seg_mask_dim = 32) {

    const int total_points = pred.h;

    if (total_points <= 0 || pred.w <= 0) return;



    int num_grid_x = in_pad.w / stride;

    int num_grid_y = in_pad.h / stride;

    if (num_grid_x <= 0 || num_grid_y <= 0 || num_grid_x * num_grid_y != total_points) {

        // Prefer deriving grid from output rows to avoid out-of-range when target_size mismatches model input.

        int g = (int)(sqrt((double)total_points) + 0.5);

        if (g > 0 && g * g == total_points) {

            num_grid_x = g;

            num_grid_y = g;

        }

        else {

            num_grid_x = total_points;

            num_grid_y = 1;

        }

    }



    for (int y = 0; y < num_grid_y; y++) {

        for (int x = 0; x < num_grid_x; x++) {

            const int idx = y * num_grid_x + x;

            if (idx >= total_points) break;

            const ncnn::Mat pred_grid = pred.row_range(idx, 1);

            int label = 0;

            float score = -FLT_MAX;



            if (task_type == 0 || task_type == 1) {

                int base = decoded_head ? 4 : reg_max_1 * 4;

                int mask_dim = (task_type == 1) ? std::max(seg_mask_dim, 0) : 0;

                int num_class = pred.w - base - mask_dim;

                if (num_class <= 0) continue;

                const ncnn::Mat pred_score = pred_grid.range(base, num_class);

                for (int k = 0; k < num_class; k++) { if (pred_score[k] > score) { label = k; score = pred_score[k]; } }

                if (!decoded_head) score = sigmoid(score);

            }

            else if (task_type == 3) {

                int score_idx = decoded_head ? 4 : reg_max_1 * 4;

                score = decoded_head ? pred_grid[score_idx] : sigmoid(pred_grid[score_idx]);

            }

            else if (task_type == 2) {

                int base = decoded_head ? 4 : reg_max_1 * 4;

                int num_class = pred.w - base - (decoded_head ? 1 : 0);

                const ncnn::Mat pred_score = pred_grid.range(base, num_class);

                for (int k = 0; k < num_class; k++) { if (pred_score[k] > score) { label = k; score = pred_score[k]; } }

                if (!decoded_head) score = sigmoid(score);

            }



            if (!std::isfinite(score)) continue;

            if (score >= prob_threshold) {

                Object obj;

                if (task_type == 2) { // OBB

                    float pred_ltrb[4];

                    if (decoded_head) {

                        const float cx = pred_grid[0];

                        const float cy = pred_grid[1];

                        const float bw = std::max(fabsf(pred_grid[2]), 1e-3f);

                        const float bh = std::max(fabsf(pred_grid[3]), 1e-3f);

                        obj.rect = cv::Rect_<float>(cx - bw * 0.5f, cy - bh * 0.5f, bw, bh);

                    }

                    else if (reg_max_1 > 1) {

                        ncnn::Mat pred_bbox = pred_grid.range(0, reg_max_1 * 4).reshape(reg_max_1, 4).clone();

                        {

                            ncnn::Layer* softmax = ncnn::create_layer("Softmax");

                            ncnn::ParamDict pd; pd.set(0, 1); pd.set(1, 1);

                            softmax->load_param(pd);

                            ncnn::Option opt; opt.num_threads = 1;

                            softmax->create_pipeline(opt);

                            softmax->forward_inplace(pred_bbox, opt);

                            softmax->destroy_pipeline(opt);

                            delete softmax;

                        }

                        for (int k = 0; k < 4; k++) {

                            float dis = 0.f;

                            const float* dis_after_sm = pred_bbox.row(k);

                            for (int l = 0; l < reg_max_1; l++) dis += l * dis_after_sm[l];

                            pred_ltrb[k] = dis * stride;

                        }

                    }

                    else {

                        for (int k = 0; k < 4; k++) pred_ltrb[k] = pred_grid[k] * stride;

                    }

                    float pb_cx = (x + 0.5f) * stride;

                    float pb_cy = (y + 0.5f) * stride;

                    const bool raw_angle = decoded_head || is_yolo26;

                    float angle_rad = 0.f;

                    if (decoded_head) {

                        pb_cx = obj.rect.x + obj.rect.width * 0.5f;

                        pb_cy = obj.rect.y + obj.rect.height * 0.5f;

                    }

                    if (raw_angle) {

                        // decoded head and OBB26 both use raw angle branch.

                        angle_rad = extra_blob.empty() ? pred_grid[pred.w - 1] : extra_blob.row(idx)[0];

                    }

                    else {

                        // v8/v11 OBB non-decoded angle branch: (sigmoid(x)-0.25)*pi

                        float angle = sigmoid(extra_blob.row(idx)[0]) - 0.25f;

                        angle_rad = angle * 3.1415926535f;

                    }

                    if (!std::isfinite(angle_rad)) angle_rad = 0.f;

                    float cos_a = cosf(angle_rad);

                    float sin_a = sinf(angle_rad);

                    float xx = decoded_head ? obj.rect.width * 0.5f : (pred_ltrb[2] - pred_ltrb[0]) * 0.5f;

                    float yy = decoded_head ? obj.rect.height * 0.5f : (pred_ltrb[3] - pred_ltrb[1]) * 0.5f;

                    float r_cx = decoded_head ? pb_cx : (pb_cx + xx * cos_a - yy * sin_a);

                    float r_cy = decoded_head ? pb_cy : (pb_cy + xx * sin_a + yy * cos_a);

                    float rw = decoded_head ? obj.rect.width : (pred_ltrb[0] + pred_ltrb[2]);

                    float rh = decoded_head ? obj.rect.height : (pred_ltrb[1] + pred_ltrb[3]);

                    rw = std::max(fabsf(rw), 1e-3f);

                    rh = std::max(fabsf(rh), 1e-3f);

                    obj.rrect = cv::RotatedRect(cv::Point2f(r_cx, r_cy), cv::Size2f(rw, rh), angle_rad * 57.2957795f);

                    obj.rect = obj.rrect.boundingRect2f();

                    if (!std::isfinite(obj.rect.x) || !std::isfinite(obj.rect.y) || !std::isfinite(obj.rect.width) || !std::isfinite(obj.rect.height)) continue;

                    if (obj.rect.width <= 0.f || obj.rect.height <= 0.f) continue;

                    obj.label = label;

                    obj.prob = score;

                }

                else {

                    decode_bbox(pred_grid, x, y, stride, score, label, obj, reg_max_1, decoded_head);

                }

                obj.gindex = index_offset + idx;

                if (task_type == 3) {

                    if (decoded_head && extra_blob.empty()) {

                        int num_points = (pred.w - 5) / 3;

                        for (int k = 0; k < num_points; k++) {

                            KeyPoint kp;

                            kp.p.x = pred_grid[5 + k * 3 + 0];

                            kp.p.y = pred_grid[5 + k * 3 + 1];

                            kp.prob = pred_grid[5 + k * 3 + 2];

                            obj.keypoints.push_back(kp);

                        }

                    }

                    else {

                        int num_points = extra_blob.w / 3;

                        const ncnn::Mat pred_points_grid = extra_blob.row_range(idx, 1).reshape(3, num_points);

                        for (int k = 0; k < num_points; k++) {

                            KeyPoint kp;

                            if (is_yolo26) {

                                kp.p.x = (x + 0.5f + pred_points_grid.row(k)[0]) * stride;

                                kp.p.y = (y + 0.5f + pred_points_grid.row(k)[1]) * stride;

                            }

                            else {

                                kp.p.x = (x + pred_points_grid.row(k)[0] * 2) * stride;

                                kp.p.y = (y + pred_points_grid.row(k)[1] * 2) * stride;

                            }

                            kp.prob = sigmoid(pred_points_grid.row(k)[2]);

                            obj.keypoints.push_back(kp);

                        }

                    }

                }

                objects.push_back(obj);

            }

        }

    }

}



class YOLO11 {

public:

    YOLO11(int picsize = 640, bool ncnngpu = true, bool use_fp16 = true, int yolo_version = 11);

    ~YOLO11();



    int load(const char* param_path, const char* model_path);

    int load_mem(const unsigned char* param_data, size_t param_len, const unsigned char* model_data, size_t model_len);

    int detect(const cv::Mat& bgr, std::vector<Object>& objects, int task_type = 0);



    int target_size;

private:

    ncnn::Net net;

    int reg_max_1_model = 16;

    bool decoded_head_model = false;

    int model_input_size_hint = 0;

    int yolo_version_model = 11; // 8/11/26

    bool use_gpu_model = false;

    bool gpu_instance_retained = false;

};



#endif // YOLO11_H



