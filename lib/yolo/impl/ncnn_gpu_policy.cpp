#include "pch.h"
#include "ncnn_gpu_policy.h"

#include <atomic>
#include <mutex>

namespace ncnn_gpu_policy {
namespace {
    std::mutex g_gpu_instance_mutex;
    std::atomic<int> g_gpu_model_instance_count{ 0 };

    void retain_gpu_instance() {
        std::lock_guard<std::mutex> lock(g_gpu_instance_mutex);
        if (g_gpu_model_instance_count.fetch_add(1, std::memory_order_acq_rel) == 0) {
            ncnn::create_gpu_instance();
        }
    }

    void release_gpu_instance() {
        std::lock_guard<std::mutex> lock(g_gpu_instance_mutex);
        const int prev = g_gpu_model_instance_count.fetch_sub(1, std::memory_order_acq_rel);
        if (prev <= 1) {
            g_gpu_model_instance_count.store(0, std::memory_order_release);
            ncnn::destroy_gpu_instance();
        }
    }
}

ConfigureResult configure_net(
    ncnn::Net& net,
    bool request_vulkan,
    bool request_fp16,
    bool disable_integrated_gpu,
    bool prefer_discrete_gpu) {
    ConfigureResult result;

    net.opt.use_vulkan_compute = false;
    net.opt.use_fp16_packed = false;
    net.opt.use_fp16_storage = false;
    net.opt.use_fp16_arithmetic = false;

    if (!request_vulkan) {
        return result;
    }

    retain_gpu_instance();
    result.gpu_instance_retained = true;

    int selected_gpu_index = -1;
    int fallback_non_igpu_index = -1;
    int fallback_any_index = -1;
    const int gpu_count = ncnn::get_gpu_count();

    for (int i = 0; i < gpu_count; i++) {
        const ncnn::GpuInfo& gi = ncnn::get_gpu_info(i);
        const bool is_integrated = gi.type() == 1;

        if (fallback_any_index < 0) {
            fallback_any_index = i;
        }
        if (!is_integrated && fallback_non_igpu_index < 0) {
            fallback_non_igpu_index = i;
        }
        if (prefer_discrete_gpu && gi.type() == 0) {
            selected_gpu_index = i;
            break;
        }
    }

    if (selected_gpu_index < 0) {
        if (disable_integrated_gpu) {
            selected_gpu_index = fallback_non_igpu_index;
        }
        else {
            selected_gpu_index = (fallback_non_igpu_index >= 0) ? fallback_non_igpu_index : fallback_any_index;
        }
    }

    if (selected_gpu_index < 0) {
        release_gpu_instance();
        result.gpu_instance_retained = false;
        return result;
    }

    net.set_vulkan_device(selected_gpu_index);
    net.opt.use_vulkan_compute = true;
    result.use_vulkan = true;
    result.device_index = selected_gpu_index;

    if (request_fp16) {
        const ncnn::GpuInfo& gi = ncnn::get_gpu_info(selected_gpu_index);
        const bool allow_fp16 = gi.support_fp16_storage()
            && gi.support_fp16_arithmetic()
            && !gi.bug_implicit_fp16_arithmetic();
        result.use_fp16 = allow_fp16;
        if (allow_fp16) {
            net.opt.use_fp16_packed = true;
            net.opt.use_fp16_storage = true;
            net.opt.use_fp16_arithmetic = true;
        }
    }

    return result;
}

void release_gpu_instance_if_retained(bool retained) {
    if (!retained) return;
    release_gpu_instance();
}

void force_release_all_gpu_instances() {
    std::lock_guard<std::mutex> lock(g_gpu_instance_mutex);
    g_gpu_model_instance_count.store(0, std::memory_order_release);
    ncnn::destroy_gpu_instance();
}

} // namespace ncnn_gpu_policy

