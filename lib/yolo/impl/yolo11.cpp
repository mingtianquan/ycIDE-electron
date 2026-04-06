#include "pch.h"
#include "yolo11.h"
#include "ncnn_gpu_policy.h"

// --- YOLO11 Class Implementation ---
static bool has_decoded_head_signature(const std::string& text) {
    return text.find("pnnx_fold_anchor_points.1") != std::string::npos
        || (text.find("chunk_0") != std::string::npos && text.find("cat_22") != std::string::npos);
}

static int normalize_yolo_version(int v) {
    if (v == 8 || v == 11 || v == 26) return v;
    return 11;
}

static int infer_input_size_from_param_text(const std::string& text) {
    // Try to read folded anchor point count in decoded-head graphs.
    // Example: MemoryData pnnx_fold_anchor_points.1 ... 0=21504 ...
    size_t p = text.find("pnnx_fold_anchor_points.1");
    if (p == std::string::npos) return 0;
    size_t z = text.find("0=", p);
    if (z == std::string::npos) return 0;
    z += 2;
    int points = 0;
    while (z < text.size() && text[z] >= '0' && text[z] <= '9') {
        points = points * 10 + (text[z] - '0');
        z++;
    }
    if (points <= 0) return 0;
    // points = (s/8)^2 + (s/16)^2 + (s/32)^2 = 21 * (s/32)^2
    double s = 32.0 * sqrt((double)points / 21.0);
    int si = (int)floor(s + 0.5);
    if (si < 32) return 0;
    si = (si / 32) * 32;
    return si;
}

static int infer_cls_input_size_from_param_text(const std::string& text) {
    // v11/v26 cls params often contain a square reshape before classifier head, e.g.:
    // Reshape ... 0=7 1=7 2=128   -> input ~= 224
    // Reshape ... 0=20 1=20 2=128 -> input ~= 640
    int best_side = 0;
    size_t pos = 0;
    while (true) {
        pos = text.find("Reshape", pos);
        if (pos == std::string::npos) break;

        size_t p0 = text.find("0=", pos);
        size_t p1 = text.find("1=", pos);
        size_t p2 = text.find("2=", pos);
        if (p0 == std::string::npos || p1 == std::string::npos || p2 == std::string::npos) {
            pos += 7;
            continue;
        }

        auto parse_int = [&](size_t p) -> int {
            p += 2;
            bool neg = false;
            if (p < text.size() && text[p] == '-') {
                neg = true;
                p++;
            }
            int v = 0;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
                v = v * 10 + (text[p] - '0');
                p++;
            }
            return neg ? -v : v;
            };

        int a = parse_int(p0);
        int b = parse_int(p1);
        int c = parse_int(p2);
        // Focus on cls-head reshape pattern and keep the largest square side.
        if (a > 0 && a == b && c == 128) {
            best_side = std::max(best_side, a);
        }

        pos += 7;
    }

    if (best_side <= 0) return 0;
    int input_size = best_side * 32;
    if (input_size >= 32 && input_size <= 2048) return input_size;
    return 0;
}

static int infer_reg_max_from_param_text(const std::string& text) {
    if (has_decoded_head_signature(text)) return 1;
    return 16;
}

static inline int expected_grid_count(int target) {
    return (target / 8) * (target / 8) + (target / 16) * (target / 16) + (target / 32) * (target / 32);
}

static void transpose_2d_if_needed(ncnn::Mat& m, int expected_rows) {
    if (m.dims != 2) return;
    if (m.h == expected_rows) return;
    if (m.w != expected_rows) return;
    ncnn::Mat t(m.h, m.w);
    for (int y = 0; y < m.h; y++) {
        const float* src = m.row(y);
        for (int x = 0; x < m.w; x++) {
            t.row(x)[y] = src[x];
        }
    }
    m = t;
}

static void split_seg_det_and_mask(const ncnn::Mat& merged, int mask_dim, ncnn::Mat& det_out, ncnn::Mat& mask_out) {
    const int det_dim = merged.w - mask_dim;
    det_out = ncnn::Mat(det_dim, merged.h);
    mask_out = ncnn::Mat(mask_dim, merged.h);
    for (int i = 0; i < merged.h; i++) {
        const float* src = merged.row(i);
        float* det_ptr = det_out.row(i);
        float* mask_ptr = mask_out.row(i);
        memcpy(det_ptr, src, det_dim * sizeof(float));
        memcpy(mask_ptr, src + det_dim, mask_dim * sizeof(float));
    }
}

static int infer_seg_mask_dim(const ncnn::Mat& mask_feat, const ncnn::Mat& mask_proto, int fallback = 32) {
    // Preferred source: mask coefficients tensor shape [num_points, mask_dim].
    if (!mask_feat.empty()) {
        if (mask_feat.dims == 2 && mask_feat.w > 0) return mask_feat.w;
        if (mask_feat.dims == 1 && mask_feat.w > 0) return mask_feat.w;
    }
    // Fallback source: proto channel count is usually mask_dim.
    if (!mask_proto.empty()) {
        if (mask_proto.dims == 3 && mask_proto.c > 0) return mask_proto.c;
        if (mask_proto.dims == 2) {
            if (mask_proto.w > 0 && mask_proto.w <= 512) return mask_proto.w;
            if (mask_proto.h > 0 && mask_proto.h <= 512) return mask_proto.h;
        }
    }
    return fallback;
}

static void flatten_cls_output(const ncnn::Mat& out, std::vector<float>& scores) {
    scores.clear();
    if (out.empty()) return;

    if (out.dims == 1) {
        scores.resize(out.w);
        for (int i = 0; i < out.w; i++) scores[i] = out[i];
        return;
    }

    if (out.dims == 2) {
        scores.resize(out.w * out.h);
        int idx = 0;
        for (int y = 0; y < out.h; y++) {
            const float* row = out.row(y);
            for (int x = 0; x < out.w; x++) scores[idx++] = row[x];
        }
        return;
    }

    if (out.dims == 3) {
        scores.resize(out.w * out.h * out.c);
        int idx = 0;
        for (int c = 0; c < out.c; c++) {
            const float* ptr = out.channel(c);
            for (int i = 0; i < out.w * out.h; i++) scores[idx++] = ptr[i];
        }
        return;
    }
}

YOLO11::YOLO11(int picsize, bool ncnngpu, bool use_fp16, int yolo_version) {
    target_size = picsize;
    const ncnn_gpu_policy::ConfigureResult cfg =
        ncnn_gpu_policy::configure_net(net, ncnngpu, use_fp16, true, true);
    use_gpu_model = cfg.use_vulkan;
    gpu_instance_retained = cfg.gpu_instance_retained;
    reg_max_1_model = 16;
    decoded_head_model = false;
    model_input_size_hint = 0;
    yolo_version_model = normalize_yolo_version(yolo_version);
}

YOLO11::~YOLO11() {
    net.clear();
    ncnn_gpu_policy::release_gpu_instance_if_retained(gpu_instance_retained);
    use_gpu_model = false;
    gpu_instance_retained = false;
}

int YOLO11::load(const char* param_path, const char* model_path) {
    if (net.load_param(param_path) != 0) return -1;
    if (net.load_model(model_path) != 0) return -1;
    reg_max_1_model = (yolo_version_model == 26) ? 1 : 16;
    decoded_head_model = false;
    model_input_size_hint = 0;
    if (param_path) {
        std::ifstream pifs(param_path);
        if (pifs.is_open()) {
            std::string text((std::istreambuf_iterator<char>(pifs)), std::istreambuf_iterator<char>());
            if (yolo_version_model != 26) {
                reg_max_1_model = infer_reg_max_from_param_text(text);
                decoded_head_model = has_decoded_head_signature(text);
            }
            model_input_size_hint = infer_input_size_from_param_text(text);
            if (model_input_size_hint <= 0) {
                model_input_size_hint = infer_cls_input_size_from_param_text(text);
            }
        }
    }
    return 0;
}

int YOLO11::load_mem(const unsigned char* param_data, size_t param_len, const unsigned char* model_data, size_t model_len) {
    if (net.load_param_mem((const char*)param_data) != 0) return -1;
    if (net.load_model(model_data) != (int)model_len) return -1;
    if (yolo_version_model == 26) {
        reg_max_1_model = 1;
        decoded_head_model = false;
    } else {
        reg_max_1_model = 16;
        decoded_head_model = false;
    }
    model_input_size_hint = 0;
    if (param_data && param_len > 0) {
        std::string text((const char*)param_data, (size_t)param_len);
        if (yolo_version_model != 26) {
            reg_max_1_model = infer_reg_max_from_param_text(text);
            decoded_head_model = has_decoded_head_signature(text);
        }
        model_input_size_hint = infer_input_size_from_param_text(text);
        if (model_input_size_hint <= 0) {
            model_input_size_hint = infer_cls_input_size_from_param_text(text);
        }
    }
    return 0;
}

int YOLO11::detect(const cv::Mat& bgr, std::vector<Object>& objects, int task_type) {
    if (bgr.empty()) return -1;
    cv::Mat bgr3;
    if (bgr.channels() == 3) bgr3 = bgr;
    else if (bgr.channels() == 4) cv::cvtColor(bgr, bgr3, cv::COLOR_BGRA2BGR);
    else if (bgr.channels() == 1) cv::cvtColor(bgr, bgr3, cv::COLOR_GRAY2BGR);
    else return -1;

    bool decoded_head_runtime = decoded_head_model;
    // cls: if model cannot provide input-size hint, default to 224 to match official v8 cls preprocessing.
    int local_target_size = (task_type == 4) ? (model_input_size_hint > 0 ? model_input_size_hint : 224) : target_size;
    // For yolo26 obb, prefer model input-size hint (usually 1024) when available.
    if (task_type != 4 && model_input_size_hint > 0 &&
        (decoded_head_runtime || (yolo_version_model == 26 && task_type == 2))) {
        local_target_size = model_input_size_hint;
    }
    int img_w = bgr3.cols, img_h = bgr3.rows;
    float scale = 1.f;
    int w = img_w, h = img_h;
    if (w > h) { scale = (float)local_target_size / w; w = local_target_size; h = h * scale; }
    else { scale = (float)local_target_size / h; h = local_target_size; w = w * scale; }
    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr3.data, ncnn::Mat::PIXEL_BGR2RGB, img_w, img_h, w, h);
    int wpad = (local_target_size + 31) / 32 * 32 - w;
    int hpad = (local_target_size + 31) / 32 * 32 - h;
    ncnn::Mat in_pad;
    ncnn::copy_make_border(in, in_pad, hpad / 2, hpad - hpad / 2, wpad / 2, wpad - wpad / 2, ncnn::BORDER_CONSTANT, 114.f);
    const float norm_vals[3] = { 1 / 255.f, 1 / 255.f, 1 / 255.f };
    in_pad.substract_mean_normalize(0, norm_vals);

    ncnn::Extractor ex = net.create_extractor();
    ex.input("in0", in_pad);

    if (task_type == 4) {
        objects.clear();
        ncnn::Mat out;
        if (ex.extract("out0", out) != 0 || out.empty()) return -1;

        std::vector<std::pair<float, int>> scores;
        if (out.dims == 1) {
            scores.reserve(out.w);
            for (int i = 0; i < out.w; i++) scores.push_back({ out[i], i });
        }
        else if (out.dims == 2 && out.h > 0) {
            // Keep the same behavior as official yolov8/yolo11 cls ncnn examples: use first row.
            const float* row0 = out.row(0);
            scores.reserve(out.w);
            for (int i = 0; i < out.w; i++) scores.push_back({ row0[i], i });
        }
        else {
            // Fallback for uncommon layouts: flatten once without extra softmax.
            std::vector<float> cls_scores;
            flatten_cls_output(out, cls_scores);
            if (cls_scores.empty()) return -1;
            scores.reserve(cls_scores.size());
            for (int i = 0; i < (int)cls_scores.size(); i++) scores.push_back({ cls_scores[i], i });
        }

        if (scores.empty()) return -1;
        const int topk = std::min(5, (int)scores.size());
        if (topk > 0) {
            std::partial_sort(scores.begin(), scores.begin() + topk, scores.end(), std::greater<std::pair<float, int>>());
            for (int i = 0; i < topk; i++) {
                Object obj;
                obj.label = scores[i].second;
                obj.prob = scores[i].first;
                objects.push_back(obj);
            }
        }
        return 0;
    }

    ncnn::Mat out, mask_feat, mask_protos, extra;
    int seg_mask_dim_runtime = 32;
    if (ex.extract("out0", out) != 0) return -1;
    if (out.empty()) return -1;

    const int exp_rows = expected_grid_count(local_target_size);
    transpose_2d_if_needed(out, exp_rows);

    if (task_type == 1) {
        if (ex.extract("out1", mask_feat) != 0) return -1;
        const int out2_status = ex.extract("out2", mask_protos);
        if (out2_status != 0) {
            // Some yolo26 seg exports provide only (merged_det_mask, proto).
            transpose_2d_if_needed(out, exp_rows);
            transpose_2d_if_needed(mask_feat, exp_rows);
            const ncnn::Mat proto_candidate = mask_feat;
            seg_mask_dim_runtime = infer_seg_mask_dim(ncnn::Mat(), proto_candidate, 32);
            if (seg_mask_dim_runtime <= 0) seg_mask_dim_runtime = 32;
            if (out.w <= seg_mask_dim_runtime) return -1;
            ncnn::Mat det_out, mask_out;
            split_seg_det_and_mask(out, seg_mask_dim_runtime, det_out, mask_out);
            out = det_out;
            mask_protos = proto_candidate;
            mask_feat = mask_out;
        }
        else {
            transpose_2d_if_needed(mask_feat, exp_rows);
            seg_mask_dim_runtime = infer_seg_mask_dim(mask_feat, mask_protos, 32);
        }
    }
    else if (task_type == 2 || task_type == 3) {
        const int out1_status = ex.extract("out1", extra);
        if (out1_status != 0) {
            // yolo26 obb may export decoded head with only out0 (no out1 angle branch).
            if (task_type == 2 && yolo_version_model == 26) {
                decoded_head_runtime = true;
                extra = ncnn::Mat();
            }
            else if (!decoded_head_runtime) {
                return -1;
            }
        }
        else {
            transpose_2d_if_needed(extra, exp_rows);
        }
    }

    std::vector<Object> proposals;
    int current_row = 0;
    std::vector<std::pair<int, int>> stride_rows;
    if (out.h > 0 && out.h % 21 == 0) {
        // For square 3-scale heads, row counts are in ratio 16:4:1 for strides 8/16/32.
        int unit = out.h / 21;
        stride_rows.push_back({ 8, unit * 16 });
        stride_rows.push_back({ 16, unit * 4 });
        stride_rows.push_back({ 32, unit });
    }
    else {
        int strides[] = { 8, 16, 32 };
        for (int stride : strides) {
            int num_grid = (local_target_size / stride) * (local_target_size / stride);
            stride_rows.push_back({ stride, num_grid });
        }
    }

    for (const auto& sr : stride_rows) {
        int stride = sr.first;
        int num_grid = sr.second;
        if (current_row + num_grid > out.h) break;
        float prob_threshold = 0.25f;
        if (task_type == 2 && decoded_head_runtime) prob_threshold = 0.7f;
        ncnn::Mat extra_rows;
        if (task_type == 2 || task_type == 3) {
            if (!extra.empty() && current_row + num_grid <= extra.h) extra_rows = extra.row_range(current_row, num_grid);
        }
        generate_proposals(
            out.row_range(current_row, num_grid),
            stride,
            in_pad,
            prob_threshold,
            proposals,
            task_type,
            reg_max_1_model,
            decoded_head_runtime,
            current_row,
            extra_rows,
            yolo_version_model == 26,
            seg_mask_dim_runtime);
        current_row += num_grid;
    }

    // Prevent pathological O(N^2) NMS on huge candidate sets.
    const size_t max_proposals = (task_type == 2) ? 3000 : 10000;
    if (proposals.size() > max_proposals) {
        std::nth_element(proposals.begin(), proposals.begin() + (ptrdiff_t)max_proposals, proposals.end(),
            [](const Object& a, const Object& b) { return a.prob > b.prob; });
        proposals.resize(max_proposals);
    }

    qsort_descent_inplace(proposals);
    std::vector<int> picked;
    float nms_threshold = 0.45f;
    if (task_type == 2) nms_threshold = 0.20f;
    nms_sorted_bboxes(proposals, picked, nms_threshold, task_type == 2);

    int count = (int)picked.size();
    if (count == 0) {
        objects.clear();
        return 0;
    }

    objects.clear();
    if (task_type == 1) {
        ncnn::Mat objects_mask_feat(mask_feat.w, 1, count);
        for (int i = 0; i < count; i++) {
            Object obj = proposals[picked[i]];
            memcpy(objects_mask_feat.channel(i), mask_feat.row(obj.gindex), mask_feat.w * sizeof(float));
            objects.push_back(obj);
        }

        ncnn::Mat objects_mask;
        {
            ncnn::Layer* gemm = ncnn::create_layer("Gemm");
            ncnn::ParamDict pd;
            pd.set(6, 1); pd.set(7, count); pd.set(8, mask_protos.w * mask_protos.h); pd.set(9, mask_feat.w);
            pd.set(10, -1); pd.set(11, 1);
            gemm->load_param(pd);
            ncnn::Option opt; opt.num_threads = 1;
            gemm->create_pipeline(opt);
            std::vector<ncnn::Mat> gemm_inputs(2);
            gemm_inputs[0] = objects_mask_feat;
            gemm_inputs[1] = mask_protos.reshape(mask_protos.w * mask_protos.h, 1, mask_protos.c);
            std::vector<ncnn::Mat> gemm_outputs(1);
            gemm->forward(gemm_inputs, gemm_outputs, opt);
            objects_mask = gemm_outputs[0].reshape(mask_protos.w, mask_protos.h, count);
            gemm->destroy_pipeline(opt);
            delete gemm;
        }

        for (int p = 0; p < objects_mask.c; p++) {
            float* ptr = objects_mask.channel(p);
            for (int j = 0; j < objects_mask.w * objects_mask.h; j++) ptr[j] = sigmoid(ptr[j]);
        }

        ncnn::Mat objects_mask_resized;
        ncnn::resize_bilinear(objects_mask, objects_mask_resized, in_pad.w / scale, in_pad.h / scale);

        for (int i = 0; i < count; i++) {
            Object& obj = objects[i];
            const ncnn::Mat mm = objects_mask_resized.channel(i);
            float x0 = (obj.rect.x - wpad / 2) / scale;
            float y0 = (obj.rect.y - hpad / 2) / scale;
            float x1 = (obj.rect.x + obj.rect.width - wpad / 2) / scale;
            float y1 = (obj.rect.y + obj.rect.height - hpad / 2) / scale;
            x0 = std::max(std::min(x0, (float)img_w - 1), 0.f);
            y0 = std::max(std::min(y0, (float)img_h - 1), 0.f);
            x1 = std::max(std::min(x1, (float)img_w - 1), 0.f);
            y1 = std::max(std::min(y1, (float)img_h - 1), 0.f);
            obj.rect.x = x0; obj.rect.y = y0; obj.rect.width = x1 - x0; obj.rect.height = y1 - y0;
            if (obj.rect.width > 0 && obj.rect.height > 0) {
                obj.mask = cv::Mat((int)obj.rect.height, (int)obj.rect.width, CV_8UC1);
                for (int y = 0; y < (int)obj.rect.height; y++) {
                    float sample_y = hpad / 2 / scale + obj.rect.y + y;
                    const float* pmm = mm.row((int)sample_y) + (int)(wpad / 2 / scale + obj.rect.x);
                    uchar* pmask = obj.mask.ptr<uchar>(y);
                    for (int x = 0; x < (int)obj.rect.width; x++) pmask[x] = pmm[x] > 0.5f ? 255 : 0;
                }
            }
        }
    }
    else {
        for (int i : picked) {
            Object obj = proposals[i];
            float rx0 = (obj.rect.x - wpad / 2) / scale, ry0 = (obj.rect.y - hpad / 2) / scale;
            obj.rect = cv::Rect_<float>(rx0, ry0, obj.rect.width / scale, obj.rect.height / scale);
            if (task_type == 2) {
                obj.rrect.center.x = (obj.rrect.center.x - wpad / 2) / scale;
                obj.rrect.center.y = (obj.rrect.center.y - hpad / 2) / scale;
                obj.rrect.size.width /= scale; obj.rrect.size.height /= scale;
            }
            if (task_type == 3) { for (auto& kp : obj.keypoints) { kp.p.x = (kp.p.x - wpad / 2) / scale; kp.p.y = (kp.p.y - hpad / 2) / scale; } }
            objects.push_back(obj);
        }
    }
    return 0;
}

static bool decode_image_from_encoded(const unsigned char* image_data, size_t image_len, cv::Mat& image) {
    if (!image_data || image_len == 0) return false;
    image = cv::imdecode(cv::Mat(1, (int)image_len, CV_8UC1, (void*)image_data), cv::IMREAD_UNCHANGED);
    return !image.empty();
}

static bool run_detect_safe(YOLO11* yolo, const cv::Mat& image, int task_type, std::vector<Object>& objects) {
    if (!yolo || image.empty()) return false;
    try {
        return yolo->detect(image, objects, task_type) == 0;
    }
    catch (...) {
        return false;
    }
}

// --- DLL Exports Implementation ---

extern "C" DLLAPI const char* yolo_version() {
    return "YOLOv11/v26 & PP-OCRv5 DLL v1.1.2 (based on ncnn)";
}

extern "C" DLLAPI int yolo_sun(int a, int b) {
    return a + b;
}

extern "C" DLLAPI int yolo_get_ncnn_gpu_count() {
#if NCNN_VULKAN
    return ncnn::get_gpu_count();
#else
    return 0;
#endif
}

extern "C" DLLAPI char* yolo_get_ncnn_runtime_info_json() {
    std::string result = "{";
#if NCNN_VULKAN
    const int gpu_count = ncnn::get_gpu_count();
    result += "\"ncnn_vulkan_compile\": true, ";
    result += "\"gpu_count\": " + std::to_string(gpu_count);
    if (gpu_count > 0) {
        result += ", \"gpus\": [";
        for (int i = 0; i < gpu_count; i++) {
            const ncnn::GpuInfo& info = ncnn::get_gpu_info(i);
            result += "{\"index\": " + std::to_string(i) + ", \"name\": \"" + std::string(info.device_name() ? info.device_name() : "") + "\"}";
            if (i + 1 < gpu_count) result += ", ";
        }
        result += "]";
    }
#else
    result += "\"ncnn_vulkan_compile\": false, \"gpu_count\": 0";
#endif
    result += "}";

    char* res_ptr = (char*)malloc(result.size() + 1);
    if (res_ptr) {
        memcpy(res_ptr, result.c_str(), result.size() + 1);
    }
    return res_ptr;
}

extern "C" DLLAPI bool yolo_generate_class_labels(const char* directory_path, const char* output_file) {
#ifdef _WIN32
    WIN32_FIND_DATAW findFileData;
    HANDLE hFind;
    wchar_t wSearchPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, (std::string(directory_path) + "\\*").c_str(), -1, wSearchPath, MAX_PATH);
    std::vector<std::string> class_labels;
    hFind = FindFirstFileW(wSearchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(findFileData.cFileName, L".") == 0 || wcscmp(findFileData.cFileName, L"..") == 0) continue;
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char mbLabel[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, findFileData.cFileName, -1, mbLabel, MAX_PATH, NULL, NULL);
            class_labels.push_back(std::string(mbLabel));
        }
    } while (FindNextFileW(hFind, &findFileData));
    FindClose(hFind);
    std::sort(class_labels.begin(), class_labels.end());
    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) return false;
    for (const auto& label : class_labels) outfile << label << std::endl;
    outfile.close();
    return true;
#else
    return false;
#endif
}

extern "C" DLLAPI void yolo_release_model(YOLO11* yolo) {
    if (yolo) { delete yolo; }
}

// Safer release API for host languages (e.g. C#): release and null the handle.
extern "C" DLLAPI void yolo_release_model_ref(YOLO11** yolo) {
    if (!yolo) return;
    if (*yolo) {
        delete* yolo;
        *yolo = nullptr;
    }
}

// Optional explicit cleanup for NCNN global Vulkan resources.
// Call this only when all model instances are already released.
extern "C" DLLAPI void yolo_release_ncnn_global_resources() {
    ncnn_gpu_policy::force_release_all_gpu_instances();
}

extern "C" DLLAPI YOLO11* yolo_load_model_from_file(const char* param_path, const char* model_path, int yolo_version, int picsize, bool ncnngpu, bool use_fp16) {
    YOLO11* yolo = new YOLO11(picsize, ncnngpu, use_fp16, yolo_version);
    if (yolo->load(param_path, model_path) != 0) { delete yolo; return nullptr; }
    return yolo;
}

extern "C" DLLAPI YOLO11* yolo_load_model_from_memory(const unsigned char* param_data, size_t param_len, const unsigned char* model_data, size_t model_len, int yolo_version, int picsize, bool ncnngpu, bool use_fp16) {
    YOLO11* yolo = new YOLO11(picsize, ncnngpu, use_fp16, yolo_version);
    if (yolo->load_mem(param_data, param_len, model_data, model_len) != 0) { delete yolo; return nullptr; }
    return yolo;
}

extern "C" DLLAPI char* yolo_detect_objects_json(YOLO11* yolo, const unsigned char* image_data, size_t image_len, int task_type) {
    if (!yolo) return nullptr;
    cv::Mat image;
    if (!decode_image_from_encoded(image_data, image_len, image)) return nullptr;
    std::vector<Object> objects;
    if (!run_detect_safe(yolo, image, task_type, objects)) return nullptr;

    std::string result = "[";
    for (size_t i = 0; i < objects.size(); i++) {
        const Object& obj = objects[i];
        result += "{";
        result += "\"label\": " + std::to_string(obj.label) + ", ";
        result += "\"prob\": " + std::to_string(obj.prob) + ", ";
        result += "\"rect\": [" + std::to_string(obj.rect.x) + ", " + std::to_string(obj.rect.y) + ", " + std::to_string(obj.rect.width) + ", " + std::to_string(obj.rect.height) + "]";
        if (task_type == 2) {
            result += ", \"rrect\": [" + std::to_string(obj.rrect.center.x) + ", " + std::to_string(obj.rrect.center.y) + ", " + std::to_string(obj.rrect.size.width) + ", " + std::to_string(obj.rrect.size.height) + ", " + std::to_string(obj.rrect.angle) + "]";
        }
        if (task_type == 3) {
            result += ", \"keypoints\": [";
            for (size_t j = 0; j < obj.keypoints.size(); j++) {
                result += "[" + std::to_string(obj.keypoints[j].p.x) + ", " + std::to_string(obj.keypoints[j].p.y) + ", " + std::to_string(obj.keypoints[j].prob) + "]";
                if (j != obj.keypoints.size() - 1) result += ", ";
            }
            result += "]";
        }
        result += "}";
        if (i != objects.size() - 1) result += ", ";
    }
    result += "]";

    char* res_ptr = (char*)malloc(result.size() + 1);
    if (res_ptr) { memcpy(res_ptr, result.c_str(), result.size() + 1); }
    return res_ptr;
}

extern "C" DLLAPI void yolo_free_json_string(char* json_str) {
    if (json_str) free(json_str);
}

extern "C" DLLAPI std::vector<Object>* yolo_detect_objects(YOLO11* yolo, const unsigned char* image_data, size_t image_len, int task_type) {
    if (!yolo) return nullptr;
    cv::Mat image;
    if (!decode_image_from_encoded(image_data, image_len, image)) return nullptr;
    std::vector<Object>* objects = new std::vector<Object>();
    if (!run_detect_safe(yolo, image, task_type, *objects)) {
        delete objects;
        return nullptr;
    }
    return objects;
}

extern "C" DLLAPI std::vector<Object>* yolo_detect_objects_raw(
    YOLO11* yolo,
    const unsigned char* bgr_data,
    int width,
    int height,
    int task_type)
{
    if (!yolo || !bgr_data || width <= 0 || height <= 0)
        return nullptr;

    cv::Mat image(height, width, CV_8UC3, (void*)bgr_data);

    if (image.empty()) return nullptr;

    std::vector<Object>* objects = new std::vector<Object>();
    if (!run_detect_safe(yolo, image, task_type, *objects)) {
        delete objects;
        return nullptr;
    }
    return objects;
}

extern "C" DLLAPI int yolo_get_objects_count(std::vector<Object>* objects) {
    return objects ? (int)objects->size() : 0;
}

extern "C" DLLAPI void yolo_get_object_info(std::vector<Object>* objects, int index, float* x, float* y, float* w, float* h, int* label, float* prob) {
    if (!objects || index < 0 || index >= (int)objects->size() || !x || !y || !w || !h || !label || !prob) return;
    const Object& obj = (*objects)[index];
    *x = obj.rect.x; *y = obj.rect.y; *w = obj.rect.width; *h = obj.rect.height; *label = obj.label; *prob = obj.prob;
}

extern "C" DLLAPI void yolo_get_obb_info(std::vector<Object>* objects, int index, float* cx, float* cy, float* w, float* h, float* angle) {
    if (!objects || index < 0 || index >= (int)objects->size() || !cx || !cy || !w || !h || !angle) return;
    const Object& obj = (*objects)[index];
    *cx = obj.rrect.center.x; *cy = obj.rrect.center.y; *w = obj.rrect.size.width; *h = obj.rrect.size.height; *angle = obj.rrect.angle;
}

extern "C" DLLAPI void yolo_get_mask_info(std::vector<Object>* objects, int index, int* width, int* height) {
    if (!objects || index < 0 || index >= (int)objects->size()) { if (width) *width = 0; if (height) *height = 0; return; }
    const Object& obj = (*objects)[index];
    if (width) *width = obj.mask.cols; if (height) *height = obj.mask.rows;
}

extern "C" DLLAPI void yolo_get_mask_data(std::vector<Object>* objects, int index, unsigned char* mask_data) {
    if (!objects || index < 0 || index >= (int)objects->size() || !mask_data) return;
    const Object& obj = (*objects)[index];
    if (obj.mask.empty()) return;
    memcpy(mask_data, obj.mask.data, obj.mask.total());
}

extern "C" DLLAPI void yolo_get_keypoints_count(std::vector<Object>* objects, int index, int* count) {
    if (!objects || index < 0 || index >= (int)objects->size()) { if (count) *count = 0; return; }
    const Object& obj = (*objects)[index];
    if (count) *count = (int)obj.keypoints.size();
}

extern "C" DLLAPI void yolo_get_keypoints_data(std::vector<Object>* objects, int index, float* kps_data) {
    if (!objects || index < 0 || index >= (int)objects->size() || !kps_data) return;
    const Object& obj = (*objects)[index];
    for (size_t i = 0; i < obj.keypoints.size(); i++) {
        kps_data[i * 3 + 0] = obj.keypoints[i].p.x;
        kps_data[i * 3 + 1] = obj.keypoints[i].p.y;
        kps_data[i * 3 + 2] = obj.keypoints[i].prob;
    }
}

extern "C" DLLAPI void yolo_free_objects_vector(std::vector<Object>* objects) {
    if (objects) delete objects;
}

extern "C" DLLAPI void yolo_free_objects_vector_ref(std::vector<Object>** objects) {
    if (!objects) return;
    if (*objects) {
        delete* objects;
        *objects = nullptr;
    }
}




