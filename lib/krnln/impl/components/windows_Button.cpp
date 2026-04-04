#include "krnln_api.h"
#include "shared_helpers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

struct ButtonState {
  std::wstring caption;
  int horizAlign = 0;
  int vertAlign = 1;
  int style = 0;
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF backColor = RGB(240, 240, 240);
  bool disabled = false;
  HFONT hFont = nullptr;
};

static ButtonState* get_button_state(HWND hwnd) {
  return reinterpret_cast<ButtonState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

}  // namespace

// Create Button Control - follows krnln_* naming convention
int krnln_button_create(long long parentHandle, int x, int y, int w, int h) {
  HWND hwndParent = as_hwnd(parentHandle);
  if (!IsWindow(hwndParent)) return 0;

  HWND hwnd = CreateWindowExW(
    0,
    L"BUTTON",
    L"Button",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    x, y, w, h,
    hwndParent,
    nullptr,
    nullptr,
    nullptr
  );

  if (!hwnd) return 0;

  ButtonState* state = new ButtonState();
  state->caption = L"Button";
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

  return static_cast<int>(reinterpret_cast<intptr_t>(hwnd));
}

// Get Button Property
int krnln_button_get_property(long long buttonHandle, const char* propName) {
  HWND hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(buttonHandle));
  if (!IsWindow(hwnd)) return 0;

  ButtonState* state = get_button_state(hwnd);
  if (!state) return 0;

  if (strcmp(propName, "left") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    HWND parent = GetParent(hwnd);
    if (parent) {
      ScreenToClient(parent, reinterpret_cast<POINT*>(&rect));
    }
    return rect.left;
  }
  else if (strcmp(propName, "top") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    HWND parent = GetParent(hwnd);
    if (parent) {
      ScreenToClient(parent, reinterpret_cast<POINT*>(&rect));
    }
    return rect.top;
  }
  else if (strcmp(propName, "width") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    return rect.right - rect.left;
  }
  else if (strcmp(propName, "height") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    return rect.bottom - rect.top;
  }
  else if (strcmp(propName, "visible") == 0) {
    return IsWindowVisible(hwnd) ? 1 : 0;
  }
  else if (strcmp(propName, "disabled") == 0) {
    return !IsWindowEnabled(hwnd) ? 1 : 0;
  }

  return 0;
}

// Set Button Property
void krnln_button_set_property(long long buttonHandle, const char* propName, long long value) {
  HWND hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(buttonHandle));
  if (!IsWindow(hwnd)) return;

  ButtonState* state = get_button_state(hwnd);
  if (!state) return;

  if (strcmp(propName, "left") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    HWND parent = GetParent(hwnd);
    POINT pt = { rect.left, rect.top };
    if (parent) ScreenToClient(parent, &pt);
    SetWindowPos(hwnd, nullptr, (int)value, pt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  }
  else if (strcmp(propName, "top") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    HWND parent = GetParent(hwnd);
    POINT pt = { rect.left, rect.top };
    if (parent) ScreenToClient(parent, &pt);
    SetWindowPos(hwnd, nullptr, pt.x, (int)value, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  }
  else if (strcmp(propName, "width") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int width = (int)value;
    SetWindowPos(hwnd, nullptr, 0, 0, width, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
  }
  else if (strcmp(propName, "height") == 0) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int height = (int)value;
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, height, SWP_NOMOVE | SWP_NOZORDER);
  }
  else if (strcmp(propName, "visible") == 0) {
    ShowWindow(hwnd, value ? SW_SHOW : SW_HIDE);
  }
  else if (strcmp(propName, "disabled") == 0) {
    EnableWindow(hwnd, value ? FALSE : TRUE);
    state->disabled = (value != 0);
  }
}

// Cleanup Button (called on destroy)
void krnln_button_destroy(long long buttonHandle) {
  HWND hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(buttonHandle));
  if (!IsWindow(hwnd)) return;

  ButtonState* state = get_button_state(hwnd);
  if (state) {
    if (state->hFont) DeleteObject(state->hFont);
    delete state;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
  }
}
