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
