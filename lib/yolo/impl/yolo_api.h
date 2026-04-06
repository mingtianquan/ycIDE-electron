#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct YOLO11;

const char* yolo_version(void);
int yolo_sun(int a, int b);
int yolo_get_ncnn_gpu_count(void);
char* yolo_get_ncnn_runtime_info_json(void);
void yolo_release_ncnn_global_resources(void);

int yolo_generate_class_labels(const char* directory_path, const char* output_file);

struct YOLO11* yolo_load_model_from_file(
    const char* param_path,
    const char* model_path,
    int yolo_version,
    int picsize,
    int ncnngpu,
    int use_fp16);

struct YOLO11* yolo_load_model_from_memory(
    const unsigned char* param_data,
    size_t param_len,
    const unsigned char* model_data,
    size_t model_len,
    int yolo_version,
    int picsize,
    int ncnngpu,
    int use_fp16);

void yolo_release_model(struct YOLO11* yolo);
void yolo_release_model_ref(struct YOLO11** yolo);

void* yolo_detect_objects_raw(
    struct YOLO11* yolo,
    const unsigned char* bgr_data,
    int width,
    int height,
    int task_type);

int yolo_get_objects_count(void* objects);

void yolo_get_object_info(
    void* objects,
    int index,
    float* x,
    float* y,
    float* w,
    float* h,
    int* label,
    float* prob);

void yolo_free_objects_vector(void* objects);

#ifdef __cplusplus
}
#endif
