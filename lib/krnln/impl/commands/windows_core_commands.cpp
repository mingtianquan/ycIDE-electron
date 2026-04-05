#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <direct.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using YC_BIN = std::vector<unsigned char>;
namespace ycfs = std::filesystem;

namespace {

// --- 命令类别：数组操作 ---
// 调用格式：〈无返回值〉 重定义数组（通用型变量数组 欲重定义的数组变量，逻辑型 是否保留以前的内容，整数型 数组对应维的上限值，...）
// 英文名称：ReDim
// 本命令可以重新定义指定数组的维数及各维的上限值。本命令为初级命令。命令参数表中最后一个参数可以被重复添加。
// 参数<1>“欲重定义的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“是否保留以前的内容”类型为逻辑型（bool），初始值为假。
// 参数<3>“数组对应维的上限值”类型为整数型（int）。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈整数型〉 取数组成员数（通用型变量/变量数组 欲检查的变量）
// 英文名称：GetAryElementCount
// 取指定数组变量的全部成员数目，如果该变量不为数组，返回 -1，本命令也可用于检查变量是否为数组变量。
// 参数<1>“欲检查的变量”类型为通用型（all），提供参数数据时只能提供变量及变量数组。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈整数型〉 取数组下标（通用型变量/变量数组 欲取某维最大下标的数组变量，［整数型 欲取其最大下标的维］）
// 英文名称：UBound
// 返回指定数组维可用的最大下标（最小下标固定为 1）。若给定变量不为数组变量或指定维不存在，返回 0。
// 参数<1>“欲取某维最大下标的数组变量”类型为通用型（all），提供参数数据时只能提供变量及变量数组。
// 参数<2>“欲取其最大下标的维”类型为整数型（int），可省略，默认值为 1。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈无返回值〉 复制数组（通用型变量数组 复制到的数组变量，通用型数组 待复制的数组数据）
// 英文名称：CopyAry
// 将数组数据复制到指定数组变量，该数组变量内的所有数据和数组维定义信息将被全部覆盖。
// 参数<1>“复制到的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“待复制的数组数据”类型为通用型（all），提供参数数据时只能提供数组数据。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈无返回值〉 加入成员（通用型变量数组 欲加入成员的数组变量，通用型数组/非数组 欲加入的成员数据）
// 英文名称：AddElement
// 将数据加入到指定数组变量尾部，并通过重定义数组维数自动增加成员数。多维数组加入后将转换为单维数组。
// 参数<1>“欲加入成员的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“欲加入的成员数据”类型为通用型（all），可提供数组或非数组数据，且类型需与目标数组变量相匹配。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈无返回值〉 插入成员（通用型变量数组 欲插入成员的数组变量，整数型 欲插入的位置，通用型数组/非数组 欲插入的成员数据）
// 英文名称：InsElement
// 将数据插入到指定数组变量的指定位置，并通过重定义数组维数自动增加成员数。多维数组插入后将转换为单维数组。
// 参数<1>“欲插入成员的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“欲插入的位置”类型为整数型（int），位置值从 1 开始，越界时不插入数据。
// 参数<3>“欲插入的成员数据”类型为通用型（all），可提供数组或非数组数据，且类型需与目标数组变量相匹配。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈整数型〉 删除成员（通用型变量数组 欲删除成员的数组变量，整数型 欲删除的位置，［整数型 欲删除的成员数目］）
// 英文名称：RemoveElement
// 删除指定数组变量中的成员，并通过重定义数组维数自动减少成员数。多维数组删除后将转换为单维数组。返回实际删除成员数。
// 参数<1>“欲删除成员的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“欲删除的位置”类型为整数型（int），位置值从 1 开始，越界时不删除数据。
// 参数<3>“欲删除的成员数目”类型为整数型（int），可省略，默认值为 1。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈无返回值〉 清除数组（通用型变量数组 欲删除成员的数组变量）
// 英文名称：RemoveAll
// 删除指定数组变量中的全部成员，释放存储空间，并重定义为单维 0 成员数组。
// 参数<1>“欲删除成员的数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 操作系统需求：Windows、Linux、Unix。
//
// 调用格式：〈无返回值〉 数组排序（通用型变量数组 数值数组变量，［逻辑型 排序方向是否为从小到大］）
// 英文名称：SortAry
// 对指定数值数组变量全部成员执行快速排序，不影响数组维定义信息，结果写回原数组变量。
// 参数<1>“数值数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 参数<2>“排序方向是否为从小到大”类型为逻辑型（bool），可省略；真为升序，假为降序，默认值为真。
// 操作系统需求：Windows、Linux。
//
// 调用格式：〈无返回值〉 数组清零（通用型变量数组 数值数组变量）
// 英文名称：ZeroAry
// 将指定数值数组变量内全部成员设置为 0，不影响数组维定义信息。
// 参数<1>“数值数组变量”类型为通用型（all），提供参数数据时只能提供变量数组。
// 操作系统需求：Windows、Linux。

static wchar_t* krnln_store_text(const std::wstring& text) {
  static std::wstring slots[8];
  static int slotIndex = 0;
  slotIndex = (slotIndex + 1) % 8;
  slots[slotIndex] = text;
  return slots[slotIndex].empty() ? const_cast<wchar_t*>(L"") : slots[slotIndex].data();
}

static void krnln_fs_build_root(const wchar_t* driveText, wchar_t outRoot[4]) {
  wchar_t drive = 0;
  if (driveText && driveText[0]) drive = static_cast<wchar_t>(towupper(driveText[0]));
  if (!drive) {
    int currentDrive = _getdrive();
    if (currentDrive >= 1 && currentDrive <= 26) drive = static_cast<wchar_t>(L'A' + currentDrive - 1);
  }
  if (!drive) drive = L'C';
  outRoot[0] = drive;
  outRoot[1] = L':';
  outRoot[2] = L'\\';
  outRoot[3] = L'\0';
}

static int krnln_fs_clamp_kb(unsigned long long value) {
  return value > 2147483647ULL ? 2147483647 : static_cast<int>(value);
}

static size_t krnln_bin_clamp_count(int count) {
  return count < 0 ? 0u : static_cast<size_t>(count);
}

static size_t krnln_bin_pos_to_index(int pos, size_t size) {
  if (pos <= 1) return 0u;
  size_t index = static_cast<size_t>(pos - 1);
  return index > size ? size : index;
}

static YC_BIN krnln_bin_from_ptr(const void* ptr, size_t len) {
  if (!ptr || len == 0) return YC_BIN();
  const unsigned char* p = static_cast<const unsigned char*>(ptr);
  return YC_BIN(p, p + len);
}

template <typename T>
static YC_BIN krnln_bin_from_scalar(const T& value) {
  return krnln_bin_from_ptr(&value, sizeof(T));
}

static int krnln_byteswap_i32(int value) {
  unsigned int v = static_cast<unsigned int>(value);
  v = ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
  return static_cast<int>(v);
}

static HANDLE g_krnln_find_handle = INVALID_HANDLE_VALUE;
static WIN32_FIND_DATAW g_krnln_find_data;
static int g_krnln_find_attr = 0;

static int krnln_fs_find_match(const WIN32_FIND_DATAW* data, int attr) {
  int required = attr & ~FILE_ATTRIBUTE_DIRECTORY;
  if (!data) return 0;
  if (wcscmp(data->cFileName, L".") == 0 || wcscmp(data->cFileName, L"..") == 0) return 0;
  int isDir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
  if (attr == 0) return isDir ? 0 : 1;
  if (isDir && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 0;
  if (!isDir && (attr & FILE_ATTRIBUTE_DIRECTORY) && required == 0) return 0;
  return (static_cast<int>(data->dwFileAttributes) & required) == required ? 1 : 0;
}

} // namespace

extern "C" int krnln_message_box(const char* text, const char* title) {
  const char* safeText = (text && text[0]) ? text : "";
  const char* safeTitle = (title && title[0]) ? title : "krnln";
  return MessageBoxA(nullptr, safeText, safeTitle, MB_OK | MB_ICONINFORMATION);
}

extern "C" int krnln_text_length(const char* text) {
  if (!text) return 0;
  return static_cast<int>(std::strlen(text));
}

long long krnln_fs_disk_total_kb(const wchar_t* driveText) {
  wchar_t root[4];
  ULARGE_INTEGER freeBytesAvailable{}, totalBytes{}, totalFreeBytes{};
  krnln_fs_build_root(driveText, root);
  if (!GetDiskFreeSpaceExW(root, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) return -1;
  return krnln_fs_clamp_kb(totalBytes.QuadPart / 1024ULL);
}

long long krnln_fs_disk_free_kb(const wchar_t* driveText) {
  wchar_t root[4];
  ULARGE_INTEGER freeBytesAvailable{}, totalBytes{}, totalFreeBytes{};
  krnln_fs_build_root(driveText, root);
  if (!GetDiskFreeSpaceExW(root, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) return -1;
  return krnln_fs_clamp_kb(totalFreeBytes.QuadPart / 1024ULL);
}

wchar_t* krnln_fs_get_disk_label(const wchar_t* driveText) {
  wchar_t root[4];
  wchar_t volumeName[MAX_PATH] = {};
  DWORD serialNumber = 0, maxComponentLen = 0, fileSystemFlags = 0;
  wchar_t fileSystemName[MAX_PATH] = {};
  krnln_fs_build_root(driveText, root);
  if (!GetVolumeInformationW(root, volumeName, MAX_PATH, &serialNumber, &maxComponentLen, &fileSystemFlags, fileSystemName, MAX_PATH)) {
    return krnln_store_text(L"");
  }
  return krnln_store_text(volumeName);
}

int krnln_fs_set_disk_label(const wchar_t* driveText, const wchar_t* label) {
  wchar_t root[4];
  krnln_fs_build_root(driveText, root);
  return SetVolumeLabelW(root, label ? label : L"") ? 1 : 0;
}

int krnln_fs_change_drive(const wchar_t* driveText) {
  if (!driveText || !driveText[0]) return 1;
  wchar_t root[4];
  krnln_fs_build_root(driveText, root);
  return SetCurrentDirectoryW(root) ? 1 : 0;
}

int krnln_fs_change_dir(const wchar_t* path) {
  if (!path || !path[0]) return 0;
  return _wchdir(path) == 0 ? 1 : 0;
}

wchar_t* krnln_fs_get_current_dir(void) {
  wchar_t* cwd = _wgetcwd(nullptr, 0);
  if (!cwd) return krnln_store_text(L"");
  std::wstring out(cwd);
  free(cwd);
  return krnln_store_text(out);
}

int krnln_fs_create_dir(const wchar_t* path) {
  if (!path || !path[0]) return 0;
  std::error_code ec;
  if (ycfs::exists(ycfs::path(path), ec)) return 1;
  return ycfs::create_directories(ycfs::path(path), ec) ? 1 : 0;
}

int krnln_fs_remove_dir_all(const wchar_t* path) {
  if (!path || !path[0]) return 0;
  std::error_code ec;
  return ycfs::remove_all(ycfs::path(path), ec) > 0 ? 1 : 0;
}

int krnln_fs_copy_file(const wchar_t* src, const wchar_t* dst) {
  if (!src || !src[0] || !dst || !dst[0]) return 0;
  return CopyFileW(src, dst, FALSE) ? 1 : 0;
}

int krnln_fs_move_file(const wchar_t* src, const wchar_t* dst) {
  if (!src || !src[0] || !dst || !dst[0]) return 0;
  return MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 1 : 0;
}

int krnln_fs_delete_file(const wchar_t* path) {
  if (!path || !path[0]) return 0;
  return DeleteFileW(path) ? 1 : 0;
}

int krnln_fs_rename_path(const wchar_t* src, const wchar_t* dst) {
  if (!src || !src[0] || !dst || !dst[0]) return 0;
  return MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
}

int krnln_fs_file_exists(const wchar_t* path) {
  if (!path || !path[0]) return 0;
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

wchar_t* krnln_fs_dir(const wchar_t* pattern, int attr) {
  int firstCall = pattern && pattern[0];
  if (firstCall) {
    if (g_krnln_find_handle != INVALID_HANDLE_VALUE) {
      FindClose(g_krnln_find_handle);
      g_krnln_find_handle = INVALID_HANDLE_VALUE;
    }
    g_krnln_find_attr = attr;
    g_krnln_find_handle = FindFirstFileW(pattern, &g_krnln_find_data);
    if (g_krnln_find_handle == INVALID_HANDLE_VALUE) return krnln_store_text(L"");

    do {
      if (krnln_fs_find_match(&g_krnln_find_data, g_krnln_find_attr)) {
        return krnln_store_text(g_krnln_find_data.cFileName);
      }
    } while (FindNextFileW(g_krnln_find_handle, &g_krnln_find_data));

    FindClose(g_krnln_find_handle);
    g_krnln_find_handle = INVALID_HANDLE_VALUE;
    return krnln_store_text(L"");
  }

  if (g_krnln_find_handle == INVALID_HANDLE_VALUE) return krnln_store_text(L"");
  while (FindNextFileW(g_krnln_find_handle, &g_krnln_find_data)) {
    if (krnln_fs_find_match(&g_krnln_find_data, g_krnln_find_attr)) {
      return krnln_store_text(g_krnln_find_data.cFileName);
    }
  }

  FindClose(g_krnln_find_handle);
  g_krnln_find_handle = INVALID_HANDLE_VALUE;
  return krnln_store_text(L"");
}

int krnln_fs_file_len(const wchar_t* path) {
  if (!path || !path[0]) return -1;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return -1;
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
  ULARGE_INTEGER size{};
  size.LowPart = data.nFileSizeLow;
  size.HighPart = data.nFileSizeHigh;
  return size.QuadPart > 2147483647ULL ? 2147483647 : static_cast<int>(size.QuadPart);
}

int krnln_fs_get_attr(const wchar_t* path) {
  if (!path || !path[0]) return -1;
  DWORD attr = GetFileAttributesW(path);
  return attr == INVALID_FILE_ATTRIBUTES ? -1 : static_cast<int>(attr);
}

int krnln_fs_set_attr(const wchar_t* path, int attr) {
  if (!path || !path[0]) return 0;
  return SetFileAttributesW(path, static_cast<DWORD>(attr)) ? 1 : 0;
}

wchar_t* krnln_fs_get_temp_file_name(const wchar_t* dir) {
  wchar_t tempPath[MAX_PATH] = {};
  wchar_t tempFile[MAX_PATH] = {};

  if (dir && dir[0]) {
    wcsncpy(tempPath, dir, MAX_PATH - 1);
    tempPath[MAX_PATH - 1] = L'\0';
  } else {
    DWORD pathLen = GetTempPathW(MAX_PATH, tempPath);
    if (pathLen == 0 || pathLen >= MAX_PATH) return krnln_store_text(L"");
  }

  if (!GetTempFileNameW(tempPath, L"YCD", 0, tempFile)) return krnln_store_text(L"");
  DeleteFileW(tempFile);
  return krnln_store_text(tempFile);
}

YC_BIN krnln_fs_read_file_bin(const wchar_t* path) {
  YC_BIN out;
  if (!path || !path[0]) return out;

  std::ifstream in(ycfs::path(path), std::ios::binary);
  if (!in) return out;
  in.seekg(0, std::ios::end);
  std::streamoff size = in.tellg();
  if (size < 0) return out;
  in.seekg(0, std::ios::beg);

  out.resize(static_cast<size_t>(size));
  if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
  if (!in && size > 0) out.clear();
  return out;
}

int krnln_fs_write_file_bins(const wchar_t* path, const std::vector<YC_BIN>& parts) {
  if (!path || !path[0]) return 0;
  std::ofstream out(ycfs::path(path), std::ios::binary | std::ios::trunc);
  if (!out) return 0;
  for (const YC_BIN& part : parts) {
    if (!part.empty()) out.write(reinterpret_cast<const char*>(part.data()), static_cast<std::streamsize>(part.size()));
    if (!out) return 0;
  }
  return 1;
}

int krnln_bin_len(const YC_BIN& value) {
  return value.size() > 2147483647u ? 2147483647 : static_cast<int>(value.size());
}

YC_BIN krnln_to_bin(const YC_BIN& value) { return value; }
YC_BIN krnln_to_bin(const wchar_t* text) { return text ? krnln_bin_from_ptr(text, wcslen(text) * sizeof(wchar_t)) : YC_BIN(); }
YC_BIN krnln_to_bin(wchar_t* text) { return krnln_to_bin(static_cast<const wchar_t*>(text)); }
YC_BIN krnln_to_bin(const char* text) { return text ? krnln_bin_from_ptr(text, strlen(text)) : YC_BIN(); }
YC_BIN krnln_to_bin(char* text) { return krnln_to_bin(static_cast<const char*>(text)); }
YC_BIN krnln_to_bin(bool value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(short value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(unsigned short value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(int value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(unsigned int value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(long value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(unsigned long value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(long long value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(unsigned long long value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(float value) { return krnln_bin_from_scalar(value); }
YC_BIN krnln_to_bin(double value) { return krnln_bin_from_scalar(value); }

YC_BIN krnln_bin_left(const YC_BIN& value, int count) {
  size_t n = krnln_bin_clamp_count(count);
  if (n > value.size()) n = value.size();
  return YC_BIN(value.begin(), value.begin() + n);
}

YC_BIN krnln_bin_right(const YC_BIN& value, int count) {
  size_t n = krnln_bin_clamp_count(count);
  if (n > value.size()) n = value.size();
  return YC_BIN(value.end() - n, value.end());
}

YC_BIN krnln_bin_mid(const YC_BIN& value, int startPos, int count) {
  size_t start = krnln_bin_pos_to_index(startPos, value.size());
  size_t n = krnln_bin_clamp_count(count);
  if (start >= value.size() || n == 0) return YC_BIN();
  if (start + n > value.size()) n = value.size() - start;
  return YC_BIN(value.begin() + start, value.begin() + start + n);
}

int krnln_bin_find(const YC_BIN& haystack, const YC_BIN& needle, int startPos) {
  size_t start = krnln_bin_pos_to_index(startPos <= 0 ? 1 : startPos, haystack.size());
  if (needle.empty()) return start < haystack.size() ? static_cast<int>(start) + 1 : 1;
  if (start >= haystack.size() || needle.size() > haystack.size()) return -1;
  auto it = std::search(haystack.begin() + start, haystack.end(), needle.begin(), needle.end());
  return it == haystack.end() ? -1 : static_cast<int>(it - haystack.begin()) + 1;
}

int krnln_bin_rfind(const YC_BIN& haystack, const YC_BIN& needle, int startPos) {
  if (needle.empty()) return haystack.empty() ? 1 : (startPos > 0 ? startPos : static_cast<int>(haystack.size()));
  if (needle.size() > haystack.size()) return -1;

  size_t limit = haystack.size() - needle.size();
  if (startPos > 0) {
    size_t requested = krnln_bin_pos_to_index(startPos, haystack.size());
    if (requested < limit) limit = requested;
  }

  for (size_t i = limit + 1; i-- > 0;) {
    if (memcmp(haystack.data() + i, needle.data(), needle.size()) == 0) return static_cast<int>(i) + 1;
    if (i == 0) break;
  }
  return -1;
}

YC_BIN krnln_bin_replace(const YC_BIN& value, int startPos, int replaceLen, const YC_BIN& repl) {
  YC_BIN out = value;
  size_t start = krnln_bin_pos_to_index(startPos, out.size());
  size_t len = krnln_bin_clamp_count(replaceLen);
  if (start > out.size()) start = out.size();
  if (start + len > out.size()) len = out.size() - start;
  out.erase(out.begin() + start, out.begin() + start + len);
  out.insert(out.begin() + start, repl.begin(), repl.end());
  return out;
}

YC_BIN krnln_bin_replace_sub(const YC_BIN& value, const YC_BIN& from, const YC_BIN& to, int startPos, int replaceCount) {
  YC_BIN out = value;
  if (from.empty()) return out;
  size_t pos = krnln_bin_pos_to_index(startPos <= 0 ? 1 : startPos, out.size());
  int done = 0;

  while (pos <= out.size()) {
    auto it = std::search(out.begin() + pos, out.end(), from.begin(), from.end());
    if (it == out.end()) break;
    size_t idx = static_cast<size_t>(it - out.begin());
    out.erase(out.begin() + idx, out.begin() + idx + from.size());
    out.insert(out.begin() + idx, to.begin(), to.end());
    pos = idx + to.size();
    done++;
    if (replaceCount > 0 && done >= replaceCount) break;
  }

  return out;
}

YC_BIN krnln_bin_space(int count) {
  return YC_BIN(krnln_bin_clamp_count(count), 0);
}

YC_BIN krnln_bin_repeat(int count, const YC_BIN& value) {
  YC_BIN out;
  int times = count < 0 ? 0 : count;
  if (times == 0 || value.empty()) return out;
  out.reserve(static_cast<size_t>(times) * value.size());
  for (int i = 0; i < times; i++) out.insert(out.end(), value.begin(), value.end());
  return out;
}

YC_BIN krnln_bin_from_address(long long ptrValue, int len) {
  size_t n = krnln_bin_clamp_count(len);
  if (ptrValue == 0 || n == 0) return YC_BIN();
  const unsigned char* p = reinterpret_cast<const unsigned char*>(static_cast<intptr_t>(ptrValue));
  return YC_BIN(p, p + n);
}

int krnln_ptr_to_int(long long ptrValue) {
  const int* p = reinterpret_cast<const int*>(static_cast<intptr_t>(ptrValue));
  return p ? *p : 0;
}

float krnln_ptr_to_float(long long ptrValue) {
  const float* p = reinterpret_cast<const float*>(static_cast<intptr_t>(ptrValue));
  return p ? *p : 0.0f;
}

double krnln_ptr_to_double(long long ptrValue) {
  const double* p = reinterpret_cast<const double*>(static_cast<intptr_t>(ptrValue));
  return p ? *p : 0.0;
}

int krnln_bin_get_int(const YC_BIN& value, int offset, int reverseBytes) {
  size_t pos = offset < 0 ? 0u : static_cast<size_t>(offset);
  int out = 0;
  if (pos + sizeof(int) > value.size()) return 0;
  memcpy(&out, value.data() + pos, sizeof(int));
  return reverseBytes ? krnln_byteswap_i32(out) : out;
}

int krnln_bin_set_int(YC_BIN& value, int offset, int data, int reverseBytes) {
  size_t pos = offset < 0 ? 0u : static_cast<size_t>(offset);
  int out = reverseBytes ? krnln_byteswap_i32(data) : data;
  if (value.size() < pos + sizeof(int)) value.resize(pos + sizeof(int), 0);
  memcpy(value.data() + pos, &out, sizeof(int));
  return 1;
}

// Forward declarations for text functions
wchar_t* krnln_text_from_utf8(const YC_BIN& utf8Data);

// Text operation functions
static int krnln_text_char_count(const wchar_t* text) {
  if (!text) return 0;
  int count = 0;
  for (int i = 0; text[i]; i++) count++;
  return count;
}

static wchar_t krnln_text_char_at(const wchar_t* text, int pos) {
  if (!text || pos < 1) return L'\0';
  return text[pos - 1];
}

extern "C" int krnln_text_len(const wchar_t* text) {
  if (!text) return 0;
  return krnln_text_char_count(text);
}

int krnln_text_len(int value) {
  std::wstring text = std::to_wstring(value);
  return static_cast<int>(text.length());
}

int krnln_text_len(double value) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return static_cast<int>(wcslen(buf));
}

int krnln_text_len(bool value) {
  return value ? 2 : 2;  // "即真" 和 "即假" 都是 2 个字符
}

int krnln_text_len(const YC_BIN& value) {
  if (value.empty()) return 0;
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_char_count(text);
}

extern "C" wchar_t* krnln_text_left(const wchar_t* text, int count) {
  if (!text || count <= 0) return krnln_store_text(L"");
  std::wstring result = text;
  if (count < static_cast<int>(result.length())) {
    result = result.substr(0, count);
  }
  return krnln_store_text(result);
}

wchar_t* krnln_text_left(int value, int count) {
  std::wstring text = std::to_wstring(value);
  return krnln_text_left(text.c_str(), count);
}

wchar_t* krnln_text_left(double value, int count) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_text_left(buf, count);
}

wchar_t* krnln_text_left(bool value, int count) {
  std::wstring text = value ? L"即真" : L"即假";
  return krnln_text_left(text.c_str(), count);
}

wchar_t* krnln_text_left(const YC_BIN& value, int count) {
  if (value.empty()) return krnln_store_text(L"");
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_left(text, count);
}

extern "C" wchar_t* krnln_text_right(const wchar_t* text, int count) {
  if (!text || count <= 0) return krnln_store_text(L"");
  std::wstring result = text;
  if (count >= static_cast<int>(result.length())) {
    return krnln_store_text(result);
  }
  return krnln_store_text(result.substr(result.length() - count));
}

wchar_t* krnln_text_right(int value, int count) {
  std::wstring text = std::to_wstring(value);
  return krnln_text_right(text.c_str(), count);
}

wchar_t* krnln_text_right(double value, int count) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_text_right(buf, count);
}

wchar_t* krnln_text_right(bool value, int count) {
  std::wstring text = value ? L"即真" : L"即假";
  return krnln_text_right(text.c_str(), count);
}

wchar_t* krnln_text_right(const YC_BIN& value, int count) {
  if (value.empty()) return krnln_store_text(L"");
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_right(text, count);
}

extern "C" wchar_t* krnln_text_mid(const wchar_t* text, int startPos, int count) {
  if (!text || startPos < 1 || count <= 0) return krnln_store_text(L"");
  std::wstring result = text;
  size_t start = static_cast<size_t>(startPos - 1);
  if (start >= result.length()) return krnln_store_text(L"");
  return krnln_store_text(result.substr(start, count));
}

wchar_t* krnln_text_mid(int value, int startPos, int count) {
  std::wstring text = std::to_wstring(value);
  return krnln_text_mid(text.c_str(), startPos, count);
}

wchar_t* krnln_text_mid(double value, int startPos, int count) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_text_mid(buf, startPos, count);
}

wchar_t* krnln_text_mid(bool value, int startPos, int count) {
  std::wstring text = value ? L"即真" : L"即假";
  return krnln_text_mid(text.c_str(), startPos, count);
}

wchar_t* krnln_text_mid(const YC_BIN& value, int startPos, int count) {
  if (value.empty()) return krnln_store_text(L"");
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_mid(text, startPos, count);
}

extern "C" wchar_t* krnln_text_chr(int code) {
  wchar_t ch = static_cast<wchar_t>(code & 0xFF);
  wchar_t buf[2] = {ch, L'\0'};
  return krnln_store_text(buf);
}

extern "C" int krnln_text_asc(const wchar_t* text, int pos) {
  if (!text) return 0;
  if (pos <= 0) pos = 1;
  wchar_t ch = krnln_text_char_at(text, pos);
  return ch ? static_cast<int>(static_cast<unsigned char>(ch)) : 0;
}

extern "C" int krnln_text_instr(const wchar_t* haystack, const wchar_t* needle, int startPos, int ignoreCase) {
  if (!haystack || !needle || startPos < 1) return -1;
  std::wstring hay = haystack;
  std::wstring need = needle;
  if (need.empty()) return startPos;
  
  size_t start = static_cast<size_t>(startPos - 1);
  if (start >= hay.length()) return -1;
  
  if (ignoreCase) {
    std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
    std::transform(need.begin(), need.end(), need.begin(), ::towlower);
  }
  
  size_t pos = hay.find(need, start);
  return pos == std::string::npos ? -1 : static_cast<int>(pos) + 1;
}

extern "C" int krnln_text_instrrev(const wchar_t* haystack, const wchar_t* needle, int startPos, int ignoreCase) {
  if (!haystack || !needle) return -1;
  std::wstring hay = haystack;
  std::wstring need = needle;
  if (need.empty()) return hay.empty() ? 1 : (startPos > 0 ? startPos : static_cast<int>(hay.length()));
  
  size_t start = startPos > 0 ? static_cast<size_t>(startPos - 1) : hay.length() - 1;
  if (start >= hay.length() && startPos > 0) start = hay.length();
  
  if (ignoreCase) {
    std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
    std::transform(need.begin(), need.end(), need.begin(), ::towlower);
  }
  
  size_t pos = hay.rfind(need, start);
  return pos == std::string::npos ? -1 : static_cast<int>(pos) + 1;
}

extern "C" wchar_t* krnln_text_ucase(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  std::transform(result.begin(), result.end(), result.begin(), ::towupper);
  return krnln_store_text(result);
}

wchar_t* krnln_text_ucase(int value) {
  std::wstring text = std::to_wstring(value);
  return krnln_text_ucase(text.c_str());
}

wchar_t* krnln_text_ucase(double value) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_text_ucase(buf);
}

wchar_t* krnln_text_ucase(const YC_BIN& value) {
  if (value.empty()) return krnln_store_text(L"");
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_ucase(text);
}

extern "C" wchar_t* krnln_text_lcase(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  std::transform(result.begin(), result.end(), result.begin(), ::towlower);
  return krnln_store_text(result);
}

wchar_t* krnln_text_lcase(int value) {
  std::wstring text = std::to_wstring(value);
  return krnln_text_lcase(text.c_str());
}

wchar_t* krnln_text_lcase(double value) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_text_lcase(buf);
}

wchar_t* krnln_text_lcase(const YC_BIN& value) {
  if (value.empty()) return krnln_store_text(L"");
  wchar_t* text = krnln_text_from_utf8(value);
  return krnln_text_lcase(text);
}

extern "C" wchar_t* krnln_text_qjcase(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  for (auto& ch : result) {
    if (ch >= L'A' && ch <= L'Z') ch = ch + 0xFEE0;
    else if (ch >= L'a' && ch <= L'z') ch = ch + 0xFEE0;
    else if (ch >= L'0' && ch <= L'9') ch = ch + 0xFEE0;
    else if (ch == L' ') ch = 0x3000;
  }
  return krnln_store_text(result);
}

extern "C" wchar_t* krnln_text_bjcase(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  for (auto& ch : result) {
    if (ch >= 0xFF21 && ch <= 0xFF3A) ch = ch - 0xFEE0;
    else if (ch >= 0xFF41 && ch <= 0xFF5A) ch = ch - 0xFEE0;
    else if (ch >= 0xFF10 && ch <= 0xFF19) ch = ch - 0xFEE0;
    else if (ch == 0x3000) ch = L' ';
  }
  return krnln_store_text(result);
}

extern "C" wchar_t* krnln_text_ltrim(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  size_t start = 0;
  while (start < result.length() && (result[start] == L' ' || result[start] == L'\t' || result[start] == 0x3000)) {
    start++;
  }
  return krnln_store_text(result.substr(start));
}

extern "C" wchar_t* krnln_text_rtrim(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  size_t end = result.length();
  while (end > 0 && (result[end - 1] == L' ' || result[end - 1] == L'\t' || result[end - 1] == 0x3000)) {
    end--;
  }
  return krnln_store_text(result.substr(0, end));
}

extern "C" wchar_t* krnln_text_trim(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring temp = text;
  std::wstring result = temp;
  
  size_t start = 0;
  while (start < result.length() && (result[start] == L' ' || result[start] == L'\t' || result[start] == 0x3000)) {
    start++;
  }
  result = result.substr(start);
  
  size_t end = result.length();
  while (end > 0 && (result[end - 1] == L' ' || result[end - 1] == L'\t' || result[end - 1] == 0x3000)) {
    end--;
  }
  return krnln_store_text(result.substr(0, end));
}

extern "C" wchar_t* krnln_text_trimall(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  std::wstring result = text;
  std::wstring out;
  for (auto ch : result) {
    if (ch != L' ' && ch != L'\t' && ch != 0x3000) {
      out += ch;
    }
  }
  return krnln_store_text(out);
}

extern "C" wchar_t* krnln_text_replace(const wchar_t* text, int startPos, int length, const wchar_t* replacement) {
  if (!text || startPos < 1 || length < 0) return krnln_store_text(text ? text : L"");
  
  std::wstring result = text;
  std::wstring repl = replacement ? replacement : L"";
  
  size_t start = static_cast<size_t>(startPos - 1);
  if (start >= result.length()) return krnln_store_text(result);
  
  size_t len = static_cast<size_t>(length);
  if (start + len > result.length()) len = result.length() - start;
  
  result.replace(start, len, repl);
  return krnln_store_text(result);
}

extern "C" wchar_t* krnln_text_rpsubtext(const wchar_t* text, const wchar_t* subText, const wchar_t* replacement, int startPos, int count, int caseSensitive) {
  if (!text || !subText) return krnln_store_text(text ? text : L"");
  
  std::wstring result = text;
  std::wstring sub = subText;
  std::wstring repl = replacement ? replacement : L"";
  
  if (sub.empty()) return krnln_store_text(result);
  
  if (startPos <= 0) startPos = 1;
  size_t start = static_cast<size_t>(startPos - 1);
  if (start >= result.length()) return krnln_store_text(result);
  
  if (count <= 0) count = -1;
  
  std::wstring hay = result;
  std::wstring need = sub;
  
  if (!caseSensitive) {
    std::transform(hay.begin(), hay.end(), hay.begin(), ::towlower);
    std::transform(need.begin(), need.end(), need.begin(), ::towlower);
  }
  
  int replaced = 0;
  size_t pos = hay.find(need, start);
  while (pos != std::string::npos && (count < 0 || replaced < count)) {
    result.replace(pos, need.length(), repl);
    hay.replace(pos, need.length(), repl);
    if (!caseSensitive) {
      std::wstring tempRepl = repl;
      std::transform(tempRepl.begin(), tempRepl.end(), tempRepl.begin(), ::towlower);
      hay.replace(pos, repl.length(), tempRepl);
    }
    pos = hay.find(need, pos + repl.length());
    replaced++;
  }
  
  return krnln_store_text(result);
}

extern "C" wchar_t* krnln_text_space(int count) {
  if (count <= 0) return krnln_store_text(L"");
  std::wstring result(count, L' ');
  return krnln_store_text(result);
}

extern "C" wchar_t* krnln_text_string(int count, const wchar_t* text) {
  if (count <= 0 || !text) return krnln_store_text(L"");
  std::wstring result;
  for (int i = 0; i < count; i++) {
    result += text;
  }
  return krnln_store_text(result);
}

extern "C" int krnln_text_strcomp(const wchar_t* text1, const wchar_t* text2, int caseSensitive) {
  const wchar_t* s1 = text1 ? text1 : L"";
  const wchar_t* s2 = text2 ? text2 : L"";
  
  if (caseSensitive) {
    int cmp = wcscmp(s1, s2);
    return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
  } else {
    std::wstring t1 = s1, t2 = s2;
    std::transform(t1.begin(), t1.end(), t1.begin(), ::towlower);
    std::transform(t2.begin(), t2.end(), t2.begin(), ::towlower);
    int cmp = wcscmp(t1.c_str(), t2.c_str());
    return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
  }
}

extern "C" wchar_t* krnln_text_pstr(intptr_t ptr) {
  if (!ptr) return krnln_store_text(L"");
  return krnln_store_text(reinterpret_cast<const wchar_t*>(ptr));
}

YC_BIN krnln_text_to_utf8(const wchar_t* text) {
  if (!text) return YC_BIN();
  
  int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return YC_BIN();
  
  std::vector<char> buffer(len);
  WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.data(), len, nullptr, nullptr);
  
  YC_BIN result(buffer.begin(), buffer.end());
  return result;
}

wchar_t* krnln_text_from_utf8(const YC_BIN& utf8Data) {
  if (utf8Data.empty()) return krnln_store_text(L"");
  
  int len = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(utf8Data.data()), static_cast<int>(utf8Data.size()), nullptr, 0);
  if (len <= 0) return krnln_store_text(L"");
  
  std::vector<wchar_t> buffer(len);
  MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(utf8Data.data()), static_cast<int>(utf8Data.size()), buffer.data(), len);
  
  return krnln_store_text(std::wstring(buffer.data()));
}

YC_BIN krnln_text_to_utf16(const wchar_t* text) {
  if (!text) return YC_BIN();
  
  int len = (wcslen(text) + 1) * sizeof(wchar_t);
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  return YC_BIN(ptr, ptr + len);
}

wchar_t* krnln_text_from_utf16(const YC_BIN& utf16Data) {
  if (utf16Data.empty() || utf16Data.size() < sizeof(wchar_t)) {
    return krnln_store_text(L"");
  }

  // 确保数据以 null 结尾，并复制到 std::wstring
  // 假设 utf16Data 包含 null 终止符，因为 krnln_text_to_utf16 包含了它
  const wchar_t* utf16Ptr = reinterpret_cast<const wchar_t*>(utf16Data.data());
  return krnln_store_text(std::wstring(utf16Ptr));
}

extern "C" wchar_t* krnln_text_str(const wchar_t* text) {
  if (!text) return krnln_store_text(L"");
  return krnln_store_text(text);
}

wchar_t* krnln_text_str(int value) {
  std::wstring result = std::to_wstring(value);
  return krnln_store_text(result);
}

wchar_t* krnln_text_str(double value) {
  wchar_t buf[64];
  swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.15g", value);
  return krnln_store_text(buf);
}

wchar_t* krnln_text_str(bool value) {
  return krnln_store_text(value ? L"即真" : L"即假");
}

wchar_t* krnln_text_str(const YC_BIN& value) {
  if (value.empty()) return krnln_store_text(L"");
  // 尝试作为 UTF-8 解析
  return krnln_text_from_utf8(value);
}
