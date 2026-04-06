#pragma once

#include "net.h"

namespace ncnn_gpu_policy {

struct ConfigureResult {
    bool use_vulkan = false;
    bool use_fp16 = false;
    bool gpu_instance_retained = false;
    int device_index = -1;
};

ConfigureResult configure_net(
    ncnn::Net& net,
    bool request_vulkan,
    bool request_fp16,
    bool disable_integrated_gpu = true,
    bool prefer_discrete_gpu = true);

void release_gpu_instance_if_retained(bool retained);
void force_release_all_gpu_instances();

} // namespace ncnn_gpu_policy

