#include "krnln_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

const wchar_t* kButtonClassName = L"BUTTON";

// 按钮属性索引定义
enum ButtonPropertyIndex {
  PROP_BUTTON_LEFT = 0,
  PROP_BUTTON_TOP = 1,
  PROP_BUTTON_WIDTH = 2,
  PROP_BUTTON_HEIGHT = 3,
  PROP_BUTTON_TAG = 4,
  PROP_BUTTON_VISIBLE = 5,
  PROP_BUTTON_DISABLE = 6,
  PROP_BUTTON_MOUSEPOINTER = 7,
  PROP_BUTTON_PIC = 8,
  PROP_BUTTON_STYLE = 9,      // 0=通常, 1=默认
  PROP_BUTTON_CAPTION = 10,
  PROP_BUTTON_HORZALIGN = 11,  // 0=左, 1=居中, 2=右
  PROP_BUTTON_VERTALIGN = 12,  // 0=顶, 1=居中, 2=底
  PROP_BUTTON_FONT = 13,
};

// 按钮事件索引
enum ButtonEvent {
  EVENT_BUTTON_CLICK = 0,  // 被单击
};

struct ButtonState {
  std::wstring caption;
  std::string tag;
  int left = 0;
  int top = 0;
  int width = 80;
  int height = 23;
  bool visible = true;
  bool disabled = false;
  int style = 0;           // 0=通常, 1=默认
  int horzAlign = 1;       // 0=左, 1=居中, 2=右
  int vertAlign = 1;       // 0=顶, 1=居中, 2=底
  HFONT hFont = nullptr;
  HBITMAP hPic = nullptr;
  COLORREF bgColor = 0;
  bool mouseDown = false;
  bool mouseHover = false;
};

static HWND as_hwnd(long long handle) {
  return reinterpret_cast<HWND>(static_cast<intptr_t>(handle));
}

static ButtonState* get_button_state(HWND hwnd) {
  return reinterpret_cast<ButtonState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static void set_button_state(HWND hwnd, ButtonState* state) {
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
}

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto state = get_button_state(hwnd);
  if (!state) return DefSubclassProc(hwnd, msg, wParam, lParam);

  switch (msg) {
    case WM_DESTROY: {
      if (state->hFont) DeleteObject(state->hFont);
      if (state->hPic) DeleteObject(state->hPic);
      delete state;
      set_button_state(hwnd, nullptr);
      break;
    }
    
    case WM_SETFOCUS: {
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    
    case WM_KILLFOCUS: {
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    
    case WM_LBUTTONDOWN: {
      state->mouseDown = true;
      SetCapture(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    
    case WM_LBUTTONUP: {
      if (state->mouseDown) {
        state->mouseDown = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        if (PtInRect(&rc, pt)) {
          SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
        }
      }
      return 0;
    }
    
    case WM_MOUSEMOVE: {
      POINT pt;
      GetCursorPos(&pt);
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);
      bool newHover = PtInRect(&rc, pt);
      if (newHover != state->mouseHover) {
        state->mouseHover = newHover;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    
    case WM_MOUSELEAVE: {
      state->mouseHover = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) break;
      
      RECT rc;
      GetClientRect(hwnd, &rc);
      
      // 绘制背景
      if (state->disabled) {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
      } else if (state->mouseDown) {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
      } else if (state->mouseHover) {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
      } else {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
      }
      
      // 绘制边框
      DrawEdge(hdc, &rc, state->mouseDown ? BDR_SUNKEN : BDR_RAISED, BF_RECT | (state->mouseDown ? BF_FLAT : 0));
      
      // 绘制标题
      if (!state->caption.empty()) {
        HFONT oldFont = nullptr;
        if (state->hFont) {
          oldFont = static_cast<HFONT>(SelectObject(hdc, state->hFont));
        }
        
        int oldBkMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF oldColor = SetTextColor(hdc, state->disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_BTNTEXT));
        
        RECT textRc = rc;
        textRc.left += 4;
        textRc.right -= 4;
        textRc.top += 3;
        textRc.bottom -= 3;
        
        UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        DrawTextW(hdc, state->caption.c_str(), static_cast<int>(state->caption.length()), &textRc, format);
        
        SetTextColor(hdc, oldColor);
        SetBkMode(hdc, oldBkMode);
        if (oldFont) SelectObject(hdc, oldFont);
      }
      
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

// ===== 公开 API =====

extern "C" {

int KrnlnButton_Create(char* unitName, long long parentHandle, int left, int top, int width, int height, int* outUnitID) {
  if (!unitName || !outUnitID || parentHandle == 0) return 0;
  
  HWND hParent = as_hwnd(parentHandle);
  auto state = new ButtonState();
  state->left = left;
  state->top = top;
  state->width = width;
  state->height = height;
  
  // 转换单位名到宽字符
  wchar_t wUnitName[256] = {0};
  MultiByteToWideChar(CP_UTF8, 0, unitName, -1, wUnitName, 256);
  
  HWND hButton = CreateWindowExW(
    0,
    L"BUTTON",
    L"",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    left, top, width, height,
    hParent,
    nullptr,
    nullptr,
    nullptr
  );
  
  if (!hButton) {
    delete state;
    return 0;
  }
  
  set_button_state(hButton, state);
  
  if (!SetWindowSubclass(hButton, ButtonSubclassProc, 0, 0)) {
    delete state;
    DestroyWindow(hButton);
    return 0;
  }
  
  *outUnitID = GetDlgCtrlID(hButton);
  return 1;
}

int KrnlnButton_GetProperty(long long buttonHandle, int propIndex, char* outBuffer, int bufferSize) {
  HWND hwnd = as_hwnd(buttonHandle);
  auto state = get_button_state(hwnd);
  if (!state || !outBuffer || bufferSize <= 0) return 0;
  
  memset(outBuffer, 0, bufferSize);
  
  switch (propIndex) {
    case PROP_BUTTON_LEFT:
      snprintf(outBuffer, bufferSize, "%d", state->left);
      break;
    case PROP_BUTTON_TOP:
      snprintf(outBuffer, bufferSize, "%d", state->top);
      break;
    case PROP_BUTTON_WIDTH:
      snprintf(outBuffer, bufferSize, "%d", state->width);
      break;
    case PROP_BUTTON_HEIGHT:
      snprintf(outBuffer, bufferSize, "%d", state->height);
      break;
    case PROP_BUTTON_TAG:
      strncpy_s(outBuffer, bufferSize, state->tag.c_str(), bufferSize - 1);
      break;
    case PROP_BUTTON_VISIBLE:
      snprintf(outBuffer, bufferSize, "%d", state->visible ? 1 : 0);
      break;
    case PROP_BUTTON_DISABLE:
      snprintf(outBuffer, bufferSize, "%d", state->disabled ? 1 : 0);
      break;
    case PROP_BUTTON_STYLE:
      snprintf(outBuffer, bufferSize, "%d", state->style);
      break;
    case PROP_BUTTON_CAPTION: {
      WideCharToMultiByte(CP_UTF8, 0, state->caption.c_str(), -1, outBuffer, bufferSize, nullptr, nullptr);
      break;
    }
    case PROP_BUTTON_HORZALIGN:
      snprintf(outBuffer, bufferSize, "%d", state->horzAlign);
      break;
    case PROP_BUTTON_VERTALIGN:
      snprintf(outBuffer, bufferSize, "%d", state->vertAlign);
      break;
    default:
      return 0;
  }
  return 1;
}

int KrnlnButton_SetProperty(long long buttonHandle, int propIndex, const char* value) {
  HWND hwnd = as_hwnd(buttonHandle);
  auto state = get_button_state(hwnd);
  if (!state || !value) return 0;
  
  switch (propIndex) {
    case PROP_BUTTON_LEFT:
      state->left = atoi(value);
      SetWindowPos(hwnd, nullptr, state->left, -1, -1, -1, SWP_NOZORDER | SWP_NOSIZE);
      break;
    case PROP_BUTTON_TOP:
      state->top = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, state->top, -1, -1, SWP_NOZORDER | SWP_NOSIZE);
      break;
    case PROP_BUTTON_WIDTH:
      state->width = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, -1, state->width, -1, SWP_NOZORDER | SWP_NOMOVE);
      break;
    case PROP_BUTTON_HEIGHT:
      state->height = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, -1, -1, state->height, SWP_NOZORDER | SWP_NOMOVE);
      break;
    case PROP_BUTTON_TAG:
      state->tag = value;
      break;
    case PROP_BUTTON_VISIBLE: {
      state->visible = (atoi(value) != 0);
      ShowWindow(hwnd, state->visible ? SW_SHOW : SW_HIDE);
      break;
    }
    case PROP_BUTTON_DISABLE: {
      state->disabled = (atoi(value) != 0);
      EnableWindow(hwnd, !state->disabled);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    }
    case PROP_BUTTON_STYLE:
      state->style = atoi(value);
      if (state->style == 1) {
        // 标注为默认按钮
        SendMessageW(GetParent(hwnd), DM_SETDEFID, GetDlgCtrlID(hwnd), 0);
      }
      break;
    case PROP_BUTTON_CAPTION: {
      wchar_t wCaption[512] = {0};
      MultiByteToWideChar(CP_UTF8, 0, value, -1, wCaption, 512);
      state->caption = wCaption;
      SetWindowTextW(hwnd, wCaption);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    }
    case PROP_BUTTON_HORZALIGN:
      state->horzAlign = atoi(value);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    case PROP_BUTTON_VERTALIGN:
      state->vertAlign = atoi(value);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    default:
      return 0;
  }
  return 1;
}

}  // extern "C"
