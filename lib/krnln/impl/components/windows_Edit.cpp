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

const wchar_t* kEditClassName = L"EDIT";

// 编辑框属性索引定义
enum EditPropertyIndex {
  PROP_EDIT_LEFT = 0,
  PROP_EDIT_TOP = 1,
  PROP_EDIT_WIDTH = 2,
  PROP_EDIT_HEIGHT = 3,
  PROP_EDIT_TAG = 4,
  PROP_EDIT_VISIBLE = 5,
  PROP_EDIT_DISABLE = 6,
  PROP_EDIT_MOUSEPOINTER = 7,
  PROP_EDIT_CONTENT = 8,
  PROP_EDIT_BORDER = 9,         // 0=无, 1=凹, 2=凸, 3=浅凹, 4=镜框, 5=单线
  PROP_EDIT_TEXTCOLOR = 10,
  PROP_EDIT_BACKCOLOR = 11,
  PROP_EDIT_FONT = 12,
  PROP_EDIT_HIDESEL = 13,
  PROP_EDIT_MAXALLOWLENGTH = 14,
  PROP_EDIT_ALLOWMULTILINES = 15,
  PROP_EDIT_SCROLLBAR = 16,      // 0=无, 1=横, 2=纵, 3=两者
  PROP_EDIT_ALIGNMODE = 17,      // 0=左, 1=居中, 2=右
  PROP_EDIT_INPUTMODE = 18,      // 0=通常, 1=只读, 2=密码, 3=整数, 4=小数...
  PROP_EDIT_PASSWORDCHAR = 19,
  PROP_EDIT_CONVERTMODE = 20,    // 0=无, 1=大->小, 2=小->大
  PROP_EDIT_SPIN = 21,           // 0=无, 1=自动, 2=手动
  PROP_EDIT_SPINMIN = 22,
  PROP_EDIT_SPINMAX = 23,
  PROP_EDIT_SELSTART = 24,
  PROP_EDIT_SELLENGTH = 25,
  PROP_EDIT_SELTEXT = 26,
  PROP_EDIT_DATASOURCE = 27,
  PROP_EDIT_DATACOL = 28,
};

// 编辑框事件索引
enum EditEvent {
  EVENT_EDIT_CHANGE = 0,      // 内容被改变
  EVENT_EDIT_SPIN = 1,        // 调节钮被按下
};

struct EditState {
  std::wstring content;
  std::string tag;
  int left = 0;
  int top = 0;
  int width = 120;
  int height = 23;
  bool visible = true;
  bool disabled = false;
  int border = 5;            // 5=单线边框
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF backColor = RGB(255, 255, 255);
  HFONT hFont = nullptr;
  HBRUSH hBackBrush = nullptr;
  bool hideSel = false;
  int maxAllowLength = 0;     // 0=无限
  bool allowMultiLines = false;
  int scrollBar = 0;          // 0=无
  int alignMode = 0;         // 0=左
  int inputMode = 0;         // 0=通常
  wchar_t passwordChar = L'*';
  int convertMode = 0;       // 0=无
  int spin = 0;              // 0=无
  int spinMin = 0;
  int spinMax = 100;
  std::string dataSource;
  std::string dataCol;
};

static HWND as_hwnd(long long handle) {
  return reinterpret_cast<HWND>(static_cast<intptr_t>(handle));
}

static EditState* get_edit_state(HWND hwnd) {
  return reinterpret_cast<EditState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static void set_edit_state(HWND hwnd, EditState* state) {
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  auto state = get_edit_state(hwnd);
  if (!state) return DefSubclassProc(hwnd, msg, wParam, lParam);

  switch (msg) {
    case WM_DESTROY: {
      if (state->hFont) DeleteObject(state->hFont);
      if (state->hBackBrush) DeleteObject(state->hBackBrush);
      delete state;
      set_edit_state(hwnd, nullptr);
      break;
    }
    
    case WM_CHANGE: {
      // 触发内容被改变事件
      SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_CHANGE), (LPARAM)hwnd);
      return 0;
    }
    
    case WM_VSCROLL: {
      if (state->spin == 2) {  // 手动调节器
        int scrollCode = LOWORD(wParam);
        if (scrollCode == SB_LINEUP || scrollCode == SB_LINEDOWN) {
          SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_VSCROLL), (LPARAM)hwnd);
        }
      }
      break;
    }
  }
  
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

// ===== 公开 API =====

extern "C" {

int KrnlnEdit_Create(char* unitName, long long parentHandle, int left, int top, int width, int height, int* outUnitID) {
  if (!unitName || !outUnitID || parentHandle == 0) return 0;
  
  HWND hParent = as_hwnd(parentHandle);
  auto state = new EditState();
  state->left = left;
  state->top = top;
  state->width = width;
  state->height = height;
  
  // 转换单位名到宽字符
  wchar_t wUnitName[256] = {0};
  MultiByteToWideChar(CP_UTF8, 0, unitName, -1, wUnitName, 256);
  
  DWORD dwStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER;
  if (state->allowMultiLines) {
    dwStyle |= ES_MULTILINE;
    if (state->scrollBar & 2) dwStyle |= WS_VSCROLL;
    if (state->scrollBar & 1) dwStyle |= WS_HSCROLL;
  }
  if (state->inputMode == 1) dwStyle |= ES_READONLY;
  if (state->inputMode == 2) dwStyle |= ES_PASSWORD;
  
  HWND hEdit = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    dwStyle,
    left, top, width, height,
    hParent,
    nullptr,
    nullptr,
    nullptr
  );
  
  if (!hEdit) {
    delete state;
    return 0;
  }
  
  set_edit_state(hEdit, state);
  
  if (!SetWindowSubclass(hEdit, EditSubclassProc, 0, 0)) {
    delete state;
    DestroyWindow(hEdit);
    return 0;
  }
  
  *outUnitID = GetDlgCtrlID(hEdit);
  return 1;
}

int KrnlnEdit_GetProperty(long long editHandle, int propIndex, char* outBuffer, int bufferSize) {
  HWND hwnd = as_hwnd(editHandle);
  auto state = get_edit_state(hwnd);
  if (!state || !outBuffer || bufferSize <= 0) return 0;
  
  memset(outBuffer, 0, bufferSize);
  
  switch (propIndex) {
    case PROP_EDIT_LEFT:
      snprintf(outBuffer, bufferSize, "%d", state->left);
      break;
    case PROP_EDIT_TOP:
      snprintf(outBuffer, bufferSize, "%d", state->top);
      break;
    case PROP_EDIT_WIDTH:
      snprintf(outBuffer, bufferSize, "%d", state->width);
      break;
    case PROP_EDIT_HEIGHT:
      snprintf(outBuffer, bufferSize, "%d", state->height);
      break;
    case PROP_EDIT_TAG:
      strncpy_s(outBuffer, bufferSize, state->tag.c_str(), bufferSize - 1);
      break;
    case PROP_EDIT_VISIBLE:
      snprintf(outBuffer, bufferSize, "%d", state->visible ? 1 : 0);
      break;
    case PROP_EDIT_DISABLE:
      snprintf(outBuffer, bufferSize, "%d", state->disabled ? 1 : 0);
      break;
    case PROP_EDIT_CONTENT: {
      wchar_t buf[4096] = {0};
      GetWindowTextW(hwnd, buf, 4096);
      WideCharToMultiByte(CP_UTF8, 0, buf, -1, outBuffer, bufferSize, nullptr, nullptr);
      break;
    }
    case PROP_EDIT_BORDER:
      snprintf(outBuffer, bufferSize, "%d", state->border);
      break;
    case PROP_EDIT_TEXTCOLOR:
      snprintf(outBuffer, bufferSize, "%d", state->textColor);
      break;
    case PROP_EDIT_BACKCOLOR:
      snprintf(outBuffer, bufferSize, "%d", state->backColor);
      break;
    case PROP_EDIT_HIDESEL:
      snprintf(outBuffer, bufferSize, "%d", state->hideSel ? 1 : 0);
      break;
    case PROP_EDIT_MAXALLOWLENGTH:
      snprintf(outBuffer, bufferSize, "%d", state->maxAllowLength);
      break;
    case PROP_EDIT_ALLOWMULTILINES:
      snprintf(outBuffer, bufferSize, "%d", state->allowMultiLines ? 1 : 0);
      break;
    case PROP_EDIT_SCROLLBAR:
      snprintf(outBuffer, bufferSize, "%d", state->scrollBar);
      break;
    case PROP_EDIT_ALIGNMODE:
      snprintf(outBuffer, bufferSize, "%d", state->alignMode);
      break;
    case PROP_EDIT_INPUTMODE:
      snprintf(outBuffer, bufferSize, "%d", state->inputMode);
      break;
    case PROP_EDIT_CONVERTMODE:
      snprintf(outBuffer, bufferSize, "%d", state->convertMode);
      break;
    case PROP_EDIT_SPIN:
      snprintf(outBuffer, bufferSize, "%d", state->spin);
      break;
    case PROP_EDIT_SPINMIN:
      snprintf(outBuffer, bufferSize, "%d", state->spinMin);
      break;
    case PROP_EDIT_SPINMAX:
      snprintf(outBuffer, bufferSize, "%d", state->spinMax);
      break;
    case PROP_EDIT_SELSTART: {
      DWORD dwStart = 0, dwEnd = 0;
      SendMessageW(hwnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
      snprintf(outBuffer, bufferSize, "%d", static_cast<int>(dwStart));
      break;
    }
    case PROP_EDIT_SELLENGTH: {
      DWORD dwStart = 0, dwEnd = 0;
      SendMessageW(hwnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
      snprintf(outBuffer, bufferSize, "%d", static_cast<int>(dwEnd - dwStart));
      break;
    }
    case PROP_EDIT_DATASOURCE:
      strncpy_s(outBuffer, bufferSize, state->dataSource.c_str(), bufferSize - 1);
      break;
    case PROP_EDIT_DATACOL:
      strncpy_s(outBuffer, bufferSize, state->dataCol.c_str(), bufferSize - 1);
      break;
    default:
      return 0;
  }
  return 1;
}

int KrnlnEdit_SetProperty(long long editHandle, int propIndex, const char* value) {
  HWND hwnd = as_hwnd(editHandle);
  auto state = get_edit_state(hwnd);
  if (!state || !value) return 0;
  
  switch (propIndex) {
    case PROP_EDIT_LEFT:
      state->left = atoi(value);
      SetWindowPos(hwnd, nullptr, state->left, -1, -1, -1, SWP_NOZORDER | SWP_NOSIZE);
      break;
    case PROP_EDIT_TOP:
      state->top = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, state->top, -1, -1, SWP_NOZORDER | SWP_NOSIZE);
      break;
    case PROP_EDIT_WIDTH:
      state->width = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, -1, state->width, -1, SWP_NOZORDER | SWP_NOMOVE);
      break;
    case PROP_EDIT_HEIGHT:
      state->height = atoi(value);
      SetWindowPos(hwnd, nullptr, -1, -1, -1, state->height, SWP_NOZORDER | SWP_NOMOVE);
      break;
    case PROP_EDIT_TAG:
      state->tag = value;
      break;
    case PROP_EDIT_VISIBLE: {
      state->visible = (atoi(value) != 0);
      ShowWindow(hwnd, state->visible ? SW_SHOW : SW_HIDE);
      break;
    }
    case PROP_EDIT_DISABLE: {
      state->disabled = (atoi(value) != 0);
      EnableWindow(hwnd, !state->disabled);
      break;
    }
    case PROP_EDIT_CONTENT: {
      wchar_t wContent[4096] = {0};
      MultiByteToWideChar(CP_UTF8, 0, value, -1, wContent, 4096);
      SetWindowTextW(hwnd, wContent);
      break;
    }
    case PROP_EDIT_BORDER:
      state->border = atoi(value);
      break;
    case PROP_EDIT_TEXTCOLOR:
      state->textColor = atoi(value);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    case PROP_EDIT_BACKCOLOR: {
      state->backColor = atoi(value);
      if (state->hBackBrush) DeleteObject(state->hBackBrush);
      state->hBackBrush = CreateSolidBrush(state->backColor);
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    }
    case PROP_EDIT_HIDESEL:
      state->hideSel = (atoi(value) != 0);
      break;
    case PROP_EDIT_MAXALLOWLENGTH:
      state->maxAllowLength = atoi(value);
      SendMessageW(hwnd, EM_LIMITTEXT, state->maxAllowLength, 0);
      break;
    case PROP_EDIT_ALLOWMULTILINES:
      state->allowMultiLines = (atoi(value) != 0);
      break;
    case PROP_EDIT_SCROLLBAR:
      state->scrollBar = atoi(value);
      break;
    case PROP_EDIT_ALIGNMODE:
      state->alignMode = atoi(value);
      break;
    case PROP_EDIT_INPUTMODE:
      state->inputMode = atoi(value);
      break;
    case PROP_EDIT_CONVERTMODE:
      state->convertMode = atoi(value);
      break;
    case PROP_EDIT_SPIN:
      state->spin = atoi(value);
      break;
    case PROP_EDIT_SPINMIN:
      state->spinMin = atoi(value);
      break;
    case PROP_EDIT_SPINMAX:
      state->spinMax = atoi(value);
      break;
    case PROP_EDIT_SELSTART: {
      int start = atoi(value);
      if (start == -1) {
        SendMessageW(hwnd, EM_SETSEL, -1L, 0);
      } else {
        DWORD dwStart = 0, dwEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
        SendMessageW(hwnd, EM_SETSEL, start, dwEnd);
      }
      break;
    }
    case PROP_EDIT_SELLENGTH: {
      int len = atoi(value);
      DWORD dwStart = 0, dwEnd = 0;
      SendMessageW(hwnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
      if (len == -1) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
      } else {
        SendMessageW(hwnd, EM_SETSEL, dwStart, dwStart + len);
      }
      break;
    }
    case PROP_EDIT_DATASOURCE:
      state->dataSource = value;
      break;
    case PROP_EDIT_DATACOL:
      state->dataCol = value;
      break;
    default:
      return 0;
  }
  return 1;
}

int KrnlnEdit_AddText(long long editHandle, const char* text) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !text) return 0;
  
  wchar_t wText[4096] = {0};
  MultiByteToWideChar(CP_UTF8, 0, text, -1, wText, 4096);
  
  // 获取现有文本长度
  int len = GetWindowTextLengthW(hwnd);
  SendMessageW(hwnd, EM_SETSEL, len, len);
  SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)wText);
  
  return 1;
}

}  // extern "C"
