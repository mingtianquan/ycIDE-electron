#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

// 共享的HWND类型转换辅助函数
inline HWND as_hwnd(long long panelHandle) {
  return reinterpret_cast<HWND>(static_cast<intptr_t>(panelHandle));
}

// 反向转换：HWND转为long long
inline long long from_hwnd(HWND hwnd) {
  return static_cast<long long>(reinterpret_cast<intptr_t>(hwnd));
}
