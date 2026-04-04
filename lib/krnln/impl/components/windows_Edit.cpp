#include "krnln_api.h"
#include "shared_helpers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifndef EM_HIDESELECTION
#define EM_HIDESELECTION 0x00B1
#endif

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kEditStateProp[] = L"KRNLN_EDIT_STATE_V2";
constexpr wchar_t kEditParentHookProp[] = L"KRNLN_EDIT_PARENT_HOOK_V2";

struct EditBinView {
  const unsigned char* data;
  int size;
};

struct ParentHookState {
  WNDPROC oldWndProc = nullptr;
  int refCount = 0;
};

struct EditRuntimeState {
  WNDPROC oldWndProc = nullptr;
  int borderMode = 5;
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF backColor = RGB(255, 255, 255);
  bool hasTextColor = false;
  bool hasBackColor = false;
  HBRUSH backBrush = nullptr;
  bool hideSel = false;
  int maxAllowLength = 0;
  bool allowMultiLines = false;
  int scrollBarMode = 0;
  int alignMode = 0;
  int inputMode = 0;
  wchar_t passwordChar = L'*';
  int convertMode = 0;
  int spinMode = 0;
  int spinMin = -32767;
  int spinMax = 32767;
  HWND spinHwnd = nullptr;
  std::wstring tag;
  std::wstring dataSource;
  std::wstring dataCol;
  HCURSOR customCursor = nullptr;
};

static bool g_updownClassInitialized = false;

static LRESULT CALLBACK krnln_parent_edit_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK krnln_edit_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static EditRuntimeState* get_edit_state(HWND hwnd) {
  return reinterpret_cast<EditRuntimeState*>(GetPropW(hwnd, kEditStateProp));
}

static ParentHookState* get_parent_hook_state(HWND hwnd) {
  return reinterpret_cast<ParentHookState*>(GetPropW(hwnd, kEditParentHookProp));
}

static const wchar_t* store_text_slot(const std::wstring& value) {
  static std::wstring slots[8];
  static int index = 0;
  index = (index + 1) & 7;
  slots[index] = value;
  return slots[index].c_str();
}

static void ensure_updown_common_control() {
  if (g_updownClassInitialized) return;
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_UPDOWN_CLASS;
  InitCommonControlsEx(&icc);
  g_updownClassInitialized = true;
}

static std::wstring get_window_text_copy(HWND hwnd) {
  const int len = GetWindowTextLengthW(hwnd);
  if (len <= 0) return std::wstring();
  std::vector<wchar_t> buffer(static_cast<size_t>(len) + 1u, L'\0');
  GetWindowTextW(hwnd, buffer.data(), len + 1);
  return std::wstring(buffer.data());
}

static void get_selection_range(HWND hwnd, int* start, int* end) {
  int s = 0;
  int e = 0;
  SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&s), reinterpret_cast<LPARAM>(&e));
  if (s < 0) s = 0;
  if (e < s) e = s;
  if (start) *start = s;
  if (end) *end = e;
}

static int clamp_int(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(maxValue, value));
}

static bool is_control_char(wchar_t ch) {
  return ch < 0x20;
}

static bool is_integer_candidate(const std::wstring& text, bool allowSign) {
  if (text.empty()) return true;
  size_t i = 0;
  if (allowSign && text[0] == L'-') {
    if (text.size() == 1) return true;
    i = 1;
  }
  for (; i < text.size(); ++i) {
    if (!iswdigit(text[i])) return false;
  }
  return true;
}

static bool is_decimal_candidate(const std::wstring& text, bool allowSign) {
  if (text.empty()) return true;
  size_t i = 0;
  if (allowSign && text[0] == L'-') {
    if (text.size() == 1) return true;
    i = 1;
  }
  bool hasDot = false;
  for (; i < text.size(); ++i) {
    wchar_t ch = text[i];
    if (ch == L'.') {
      if (hasDot) return false;
      hasDot = true;
      continue;
    }
    if (!iswdigit(ch)) return false;
  }
  if (text == L"." || text == L"-.") return true;
  return true;
}

static bool parse_long_long(const std::wstring& text, long long* out) {
  if (!out) return false;
  if (text.empty() || text == L"-") return false;
  const wchar_t* begin = text.c_str();
  wchar_t* end = nullptr;
  errno = 0;
  const long long v = _wcstoi64(begin, &end, 10);
  if (!end || *end != 0 || errno == ERANGE) return false;
  *out = v;
  return true;
}

static bool parse_double_value(const std::wstring& text, double* out) {
  if (!out) return false;
  if (text.empty() || text == L"-" || text == L"." || text == L"-.") return false;
  const wchar_t* begin = text.c_str();
  wchar_t* end = nullptr;
  errno = 0;
  const double v = wcstod(begin, &end);
  if (!end || *end != 0 || errno == ERANGE || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

static std::wstring build_text_after_char(HWND hwnd, wchar_t ch) {
  std::wstring text = get_window_text_copy(hwnd);
  int selStart = 0;
  int selEnd = 0;
  get_selection_range(hwnd, &selStart, &selEnd);
  if (selStart > static_cast<int>(text.size())) selStart = static_cast<int>(text.size());
  if (selEnd > static_cast<int>(text.size())) selEnd = static_cast<int>(text.size());

  if (ch == L'\b') {
    if (selStart != selEnd) {
      text.erase(static_cast<size_t>(selStart), static_cast<size_t>(selEnd - selStart));
      return text;
    }
    if (selStart > 0) {
      text.erase(static_cast<size_t>(selStart - 1), 1u);
    }
    return text;
  }

  text.replace(static_cast<size_t>(selStart), static_cast<size_t>(selEnd - selStart), 1u, ch);
  return text;
}

static bool validate_input_mode_candidate(int inputMode, const std::wstring& candidate) {
  switch (inputMode) {
    case 0:
    case 1:
    case 2:
      return true;
    case 3:
      return is_integer_candidate(candidate, true);
    case 4:
      return is_decimal_candidate(candidate, true);
    case 5: {
      if (!is_integer_candidate(candidate, false)) return false;
      long long value = 0;
      if (!parse_long_long(candidate, &value)) return true;
      return value >= 0 && value <= 255;
    }
    case 6: {
      if (!is_integer_candidate(candidate, true)) return false;
      long long value = 0;
      if (!parse_long_long(candidate, &value)) return true;
      return value >= -32768 && value <= 32767;
    }
    case 7: {
      if (!is_integer_candidate(candidate, true)) return false;
      long long value = 0;
      if (!parse_long_long(candidate, &value)) return true;
      return value >= static_cast<long long>(INT_MIN) && value <= static_cast<long long>(INT_MAX);
    }
    case 8:
      return is_integer_candidate(candidate, true);
    case 9: {
      if (!is_decimal_candidate(candidate, true)) return false;
      double value = 0.0;
      if (!parse_double_value(candidate, &value)) return true;
      const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
      return value >= -maxFloat && value <= maxFloat;
    }
    case 10:
      return is_decimal_candidate(candidate, true);
    case 11: {
      for (wchar_t ch : candidate) {
        if (iswdigit(ch)) continue;
        if (ch == L'-' || ch == L'/' || ch == L':' || ch == L' ' || ch == L'.') continue;
        return false;
      }
      return true;
    }
    default:
      return true;
  }
}

static int normalize_border_mode(int mode) {
  return clamp_int(mode, 0, 5);
}

static int normalize_scroll_mode(int mode) {
  return clamp_int(mode, 0, 3);
}

static int normalize_align_mode(int mode) {
  return clamp_int(mode, 0, 2);
}

static int normalize_input_mode(int mode) {
  return clamp_int(mode, 0, 11);
}

static int normalize_convert_mode(int mode) {
  return clamp_int(mode, 0, 2);
}

static int normalize_spin_mode(int mode) {
  return clamp_int(mode, 0, 2);
}

static void refresh_back_brush(EditRuntimeState* state) {
  if (!state) return;
  if (state->backBrush) {
    DeleteObject(state->backBrush);
    state->backBrush = nullptr;
  }
  if (state->hasBackColor) {
    state->backBrush = CreateSolidBrush(state->backColor);
  }
}

static void apply_edit_border_style(HWND hwnd, EditRuntimeState* state) {
  if (!hwnd || !state) return;
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

  style &= ~WS_BORDER;
  exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);

  switch (state->borderMode) {
    case 0:
      break;
    case 1:
      exStyle |= WS_EX_CLIENTEDGE;
      break;
    case 2:
      exStyle |= WS_EX_WINDOWEDGE;
      break;
    case 3:
      exStyle |= WS_EX_STATICEDGE;
      break;
    case 4:
      exStyle |= (WS_EX_WINDOWEDGE | WS_EX_STATICEDGE);
      break;
    case 5:
    default:
      style |= WS_BORDER;
      break;
  }

  SetWindowLongPtrW(hwnd, GWL_STYLE, style);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
}

static void destroy_spin_control(EditRuntimeState* state) {
  if (!state || !state->spinHwnd) return;
  if (IsWindow(state->spinHwnd)) {
    DestroyWindow(state->spinHwnd);
  }
  state->spinHwnd = nullptr;
}

static void ensure_spin_control(HWND hwnd, EditRuntimeState* state) {
  if (!hwnd || !state) return;

  const bool needSpin = (state->spinMode != 0) && !state->allowMultiLines;
  if (!needSpin) {
    destroy_spin_control(state);
    return;
  }

  HWND parent = GetParent(hwnd);
  if (!parent || !IsWindow(parent)) {
    destroy_spin_control(state);
    return;
  }

  ensure_updown_common_control();

  if (!state->spinHwnd || !IsWindow(state->spinHwnd)) {
    DWORD spinStyle = WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS;
    if (state->spinMode == 1) {
      spinStyle |= UDS_SETBUDDYINT;
    }
    state->spinHwnd = CreateWindowExW(
      0,
      UPDOWN_CLASSW,
      L"",
      spinStyle,
      0,
      0,
      0,
      0,
      parent,
      nullptr,
      GetModuleHandleW(nullptr),
      nullptr
    );
    if (!state->spinHwnd) return;
    SendMessageW(state->spinHwnd, UDM_SETBUDDY, reinterpret_cast<WPARAM>(hwnd), 0);
    SendMessageW(state->spinHwnd, UDM_SETBASE, 10, 0);
  }

  const int minValue = std::min(state->spinMin, state->spinMax);
  const int maxValue = std::max(state->spinMin, state->spinMax);
  SendMessageW(state->spinHwnd, UDM_SETRANGE32, static_cast<WPARAM>(minValue), static_cast<LPARAM>(maxValue));
}

static void apply_edit_runtime(HWND hwnd, EditRuntimeState* state) {
  if (!hwnd || !state) return;

  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  style &= ~(ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_HSCROLL | WS_VSCROLL |
             ES_LEFT | ES_CENTER | ES_RIGHT | ES_PASSWORD | ES_NUMBER | ES_UPPERCASE | ES_LOWERCASE);

  if (state->allowMultiLines) {
    style |= ES_MULTILINE;
    if (state->scrollBarMode == 1 || state->scrollBarMode == 3) {
      style |= (WS_HSCROLL | ES_AUTOHSCROLL);
    }
    if (state->scrollBarMode == 2 || state->scrollBarMode == 3) {
      style |= (WS_VSCROLL | ES_AUTOVSCROLL);
    }
  } else {
    style |= ES_AUTOHSCROLL;
  }

  if (state->alignMode == 1) style |= ES_CENTER;
  else if (state->alignMode == 2) style |= ES_RIGHT;
  else style |= ES_LEFT;

  if (state->inputMode == 2) style |= ES_PASSWORD;
  if (state->inputMode == 3 || state->inputMode == 5 || state->inputMode == 6 || state->inputMode == 7 || state->inputMode == 8) {
    style |= ES_NUMBER;
  }

  if (state->convertMode == 1) style |= ES_LOWERCASE;
  else if (state->convertMode == 2) style |= ES_UPPERCASE;

  SetWindowLongPtrW(hwnd, GWL_STYLE, style);
  apply_edit_border_style(hwnd, state);
  SetWindowPos(
    hwnd,
    nullptr,
    0,
    0,
    0,
    0,
    SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED
  );

  SendMessageW(hwnd, EM_SETREADONLY, static_cast<WPARAM>(state->inputMode == 1 ? TRUE : FALSE), 0);
  SendMessageW(hwnd, EM_SETLIMITTEXT, static_cast<WPARAM>(state->maxAllowLength), 0);
  SendMessageW(hwnd, EM_HIDESELECTION, static_cast<WPARAM>(state->hideSel ? TRUE : FALSE), 0);

  if (state->inputMode == 2) {
    SendMessageW(hwnd, EM_SETPASSWORDCHAR, static_cast<WPARAM>(state->passwordChar), 0);
  } else {
    SendMessageW(hwnd, EM_SETPASSWORDCHAR, 0, 0);
  }

  ensure_spin_control(hwnd, state);
  InvalidateRect(hwnd, nullptr, TRUE);
}

static void retain_parent_hook(HWND parent) {
  if (!parent || !IsWindow(parent)) return;
  ParentHookState* hook = get_parent_hook_state(parent);
  if (!hook) {
    hook = new ParentHookState();
    hook->oldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      parent,
      GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(krnln_parent_edit_wnd_proc)
    ));
    SetPropW(parent, kEditParentHookProp, reinterpret_cast<HANDLE>(hook));
  }
  ++hook->refCount;
}

static void release_parent_hook(HWND parent) {
  if (!parent || !IsWindow(parent)) return;
  ParentHookState* hook = get_parent_hook_state(parent);
  if (!hook) return;
  --hook->refCount;
  if (hook->refCount > 0) return;
  RemovePropW(parent, kEditParentHookProp);
  if (hook->oldWndProc) {
    SetWindowLongPtrW(parent, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hook->oldWndProc));
  }
  delete hook;
}

static EditRuntimeState* ensure_edit_state(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return nullptr;
  EditRuntimeState* state = get_edit_state(hwnd);
  if (state) return state;

  state = new EditRuntimeState();
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  state->allowMultiLines = (style & ES_MULTILINE) != 0;
  if (style & ES_CENTER) state->alignMode = 1;
  else if (style & ES_RIGHT) state->alignMode = 2;
  else state->alignMode = 0;
  if (style & ES_PASSWORD) state->inputMode = 2;
  if (style & ES_LOWERCASE) state->convertMode = 1;
  if (style & ES_UPPERCASE) state->convertMode = 2;
  if (style & WS_VSCROLL) {
    state->scrollBarMode = (style & WS_HSCROLL) ? 3 : 2;
  } else if (style & WS_HSCROLL) {
    state->scrollBarMode = 1;
  }
  if (style & WS_BORDER) state->borderMode = 5;
  else if (exStyle & WS_EX_CLIENTEDGE) state->borderMode = 1;
  else if (exStyle & WS_EX_WINDOWEDGE) state->borderMode = 2;
  else if (exStyle & WS_EX_STATICEDGE) state->borderMode = 3;
  else state->borderMode = 0;

  SetPropW(hwnd, kEditStateProp, reinterpret_cast<HANDLE>(state));
  retain_parent_hook(GetParent(hwnd));
  state->oldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
    hwnd,
    GWLP_WNDPROC,
    reinterpret_cast<LONG_PTR>(krnln_edit_wnd_proc)
  ));
  apply_edit_runtime(hwnd, state);
  return state;
}

static void destroy_edit_state(HWND hwnd, EditRuntimeState* state) {
  if (!state) return;
  destroy_spin_control(state);
  if (state->backBrush) {
    DeleteObject(state->backBrush);
    state->backBrush = nullptr;
  }
  release_parent_hook(GetParent(hwnd));
  RemovePropW(hwnd, kEditStateProp);
  delete state;
}

static const wchar_t* get_edit_sel_text(HWND hwnd) {
  int selStart = 0;
  int selEnd = 0;
  get_selection_range(hwnd, &selStart, &selEnd);
  std::wstring text = get_window_text_copy(hwnd);
  if (selStart < 0) selStart = 0;
  if (selEnd < selStart) selEnd = selStart;
  if (selStart > static_cast<int>(text.size())) selStart = static_cast<int>(text.size());
  if (selEnd > static_cast<int>(text.size())) selEnd = static_cast<int>(text.size());
  return store_text_slot(text.substr(static_cast<size_t>(selStart), static_cast<size_t>(selEnd - selStart)));
}

static void set_edit_sel_start(HWND hwnd, int value) {
  const int len = GetWindowTextLengthW(hwnd);
  int selStart = value;
  if (selStart < -1) selStart = 0;
  if (selStart == -1) selStart = len;
  if (selStart > len) selStart = len;
  int curStart = 0;
  int curEnd = 0;
  get_selection_range(hwnd, &curStart, &curEnd);
  int newEnd = curEnd;
  if (newEnd < selStart) newEnd = selStart;
  SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(selStart), static_cast<LPARAM>(newEnd));
}

static void set_edit_sel_length(HWND hwnd, int value) {
  if (value == -1) {
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return;
  }
  int selStart = 0;
  int selEnd = 0;
  get_selection_range(hwnd, &selStart, &selEnd);
  int selLength = value;
  if (selLength < 0) selLength = 0;
  const int len = GetWindowTextLengthW(hwnd);
  int newEnd = selStart + selLength;
  if (newEnd > len) newEnd = len;
  SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(selStart), static_cast<LPARAM>(newEnd));
}

static void notify_manual_spin(HWND parent, HWND editHwnd, int direction) {
  if (!parent || !editHwnd) return;
  const int ctrlId = GetDlgCtrlID(editHwnd);
  const int delta = direction >= 0 ? 1 : -1;
  SendMessageW(
    parent,
    WM_COMMAND,
    MAKEWPARAM(ctrlId, EN_VSCROLL),
    static_cast<LPARAM>(delta)
  );
}

static LRESULT CALLBACK krnln_parent_edit_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  ParentHookState* hook = get_parent_hook_state(hwnd);
  if (!hook || !hook->oldWndProc) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC) {
    HWND editHwnd = reinterpret_cast<HWND>(lParam);
    EditRuntimeState* state = get_edit_state(editHwnd);
    if (state && (state->hasTextColor || state->hasBackColor)) {
      HDC dc = reinterpret_cast<HDC>(wParam);
      if (state->hasTextColor) {
        SetTextColor(dc, state->textColor);
      }
      if (state->hasBackColor) {
        SetBkColor(dc, state->backColor);
      }
      SetBkMode(dc, OPAQUE);
      if (state->backBrush) {
        return reinterpret_cast<LRESULT>(state->backBrush);
      }
    }
  } else if (msg == WM_NOTIFY) {
    const LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
    if (hdr && hdr->code == UDN_DELTAPOS) {
      HWND spinHwnd = hdr->hwndFrom;
      HWND buddy = reinterpret_cast<HWND>(SendMessageW(spinHwnd, UDM_GETBUDDY, 0, 0));
      EditRuntimeState* state = get_edit_state(buddy);
      if (state && state->spinHwnd == spinHwnd && state->spinMode == 2) {
        const NMUPDOWN* upd = reinterpret_cast<const NMUPDOWN*>(lParam);
        const int direction = (upd && upd->iDelta > 0) ? 1 : -1;
        notify_manual_spin(hwnd, buddy, direction);
        return TRUE;
      }
    }
  } else if (msg == WM_NCDESTROY) {
    RemovePropW(hwnd, kEditParentHookProp);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hook->oldWndProc));
    WNDPROC oldProc = hook->oldWndProc;
    delete hook;
    return CallWindowProcW(oldProc, hwnd, msg, wParam, lParam);
  }

  return CallWindowProcW(hook->oldWndProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK krnln_edit_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  EditRuntimeState* state = get_edit_state(hwnd);
  if (!state || !state->oldWndProc) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  if (msg == WM_CHAR) {
    wchar_t ch = static_cast<wchar_t>(wParam);
    if (!is_control_char(ch)) {
      if (state->convertMode == 1) {
        ch = static_cast<wchar_t>(towlower(ch));
      } else if (state->convertMode == 2) {
        ch = static_cast<wchar_t>(towupper(ch));
      }
      const std::wstring next = build_text_after_char(hwnd, ch);
      if (!validate_input_mode_candidate(state->inputMode, next)) {
        return 0;
      }
      wParam = static_cast<WPARAM>(ch);
    }
  } else if (msg == WM_SETCURSOR) {
    if (state->customCursor) {
      SetCursor(state->customCursor);
      return TRUE;
    }
  } else if (msg == WM_NCDESTROY) {
    WNDPROC oldProc = state->oldWndProc;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oldProc));
    destroy_edit_state(hwnd, state);
    return CallWindowProcW(oldProc, hwnd, msg, wParam, lParam);
  }

  return CallWindowProcW(state->oldWndProc, hwnd, msg, wParam, lParam);
}

static int map_prop_name_to_index(const char* propName) {
  if (!propName) return -1;
  if (strcmp(propName, "left") == 0) return 0;
  if (strcmp(propName, "top") == 0) return 1;
  if (strcmp(propName, "width") == 0) return 2;
  if (strcmp(propName, "height") == 0) return 3;
  if (strcmp(propName, "tag") == 0) return 4;
  if (strcmp(propName, "visible") == 0) return 5;
  if (strcmp(propName, "disabled") == 0 || strcmp(propName, "disable") == 0) return 6;
  if (strcmp(propName, "context") == 0 || strcmp(propName, "text") == 0) return 8;
  if (strcmp(propName, "border") == 0) return 9;
  if (strcmp(propName, "TextColor") == 0) return 10;
  if (strcmp(propName, "BackColor") == 0) return 11;
  if (strcmp(propName, "HideSel") == 0) return 13;
  if (strcmp(propName, "MaxAllowLength") == 0 || strcmp(propName, "maxLength") == 0) return 14;
  if (strcmp(propName, "AllowMultiLines") == 0 || strcmp(propName, "multiLine") == 0) return 15;
  if (strcmp(propName, "ScrollBar") == 0) return 16;
  if (strcmp(propName, "AlignMode") == 0) return 17;
  if (strcmp(propName, "InputMode") == 0 || strcmp(propName, "readOnly") == 0) return 18;
  if (strcmp(propName, "PasswordChar") == 0) return 19;
  if (strcmp(propName, "ConvertMode") == 0) return 20;
  if (strcmp(propName, "spin") == 0) return 21;
  if (strcmp(propName, "SpinMin") == 0) return 22;
  if (strcmp(propName, "SpinMax") == 0) return 23;
  if (strcmp(propName, "SelStart") == 0 || strcmp(propName, "selStart") == 0) return 24;
  if (strcmp(propName, "SelLength") == 0 || strcmp(propName, "selLength") == 0) return 25;
  if (strcmp(propName, "SelText") == 0) return 26;
  if (strcmp(propName, "DataSource") == 0) return 27;
  if (strcmp(propName, "DataCol") == 0) return 28;
  return -1;
}

}  // namespace

extern "C" int krnln_edit_create(long long parentHandle, int x, int y, int w, int h) {
  HWND hwndParent = as_hwnd(parentHandle);
  if (!hwndParent || !IsWindow(hwndParent)) return 0;

  HWND hwnd = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    L"",
    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
    x,
    y,
    w,
    h,
    hwndParent,
    nullptr,
    nullptr,
    nullptr
  );
  if (!hwnd) return 0;
  ensure_edit_state(hwnd);
  return static_cast<int>(reinterpret_cast<intptr_t>(hwnd));
}

extern "C" int krnln_edit_set_prop_num_runtime(long long editHandle, int propIndex, intptr_t value) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  EditRuntimeState* state = ensure_edit_state(hwnd);
  if (!state) return 0;

  switch (propIndex) {
    case 9:
      state->borderMode = normalize_border_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 10:
      state->textColor = static_cast<COLORREF>(static_cast<uint32_t>(value));
      state->hasTextColor = true;
      InvalidateRect(GetParent(hwnd), nullptr, TRUE);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;
    case 11:
      state->backColor = static_cast<COLORREF>(static_cast<uint32_t>(value));
      state->hasBackColor = true;
      refresh_back_brush(state);
      InvalidateRect(GetParent(hwnd), nullptr, TRUE);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;
    case 12: {
      HFONT font = reinterpret_cast<HFONT>(value);
      if (font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      }
      return 1;
    }
    case 13:
      state->hideSel = (value != 0);
      apply_edit_runtime(hwnd, state);
      return 1;
    case 14:
      state->maxAllowLength = value < 0 ? 0 : static_cast<int>(value);
      apply_edit_runtime(hwnd, state);
      return 1;
    case 15:
      state->allowMultiLines = (value != 0);
      apply_edit_runtime(hwnd, state);
      return 1;
    case 16:
      state->scrollBarMode = normalize_scroll_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 17:
      state->alignMode = normalize_align_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 18:
      state->inputMode = normalize_input_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 20:
      state->convertMode = normalize_convert_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 21:
      state->spinMode = normalize_spin_mode(static_cast<int>(value));
      apply_edit_runtime(hwnd, state);
      return 1;
    case 22:
      state->spinMin = clamp_int(static_cast<int>(value), -32767, 32767);
      if (state->spinMin > state->spinMax) state->spinMax = state->spinMin;
      ensure_spin_control(hwnd, state);
      return 1;
    case 23:
      state->spinMax = clamp_int(static_cast<int>(value), -32767, 32767);
      if (state->spinMax < state->spinMin) state->spinMin = state->spinMax;
      ensure_spin_control(hwnd, state);
      return 1;
    case 24:
      set_edit_sel_start(hwnd, static_cast<int>(value));
      return 1;
    case 25:
      set_edit_sel_length(hwnd, static_cast<int>(value));
      return 1;
    default:
      break;
  }
  return 0;
}

extern "C" int krnln_edit_set_prop_text_runtime(long long editHandle, int propIndex, const wchar_t* value) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  EditRuntimeState* state = ensure_edit_state(hwnd);
  if (!state) return 0;
  const wchar_t* safeValue = value ? value : L"";

  switch (propIndex) {
    case 4:
      state->tag = safeValue;
      return 1;
    case 8:
      SetWindowTextW(hwnd, safeValue);
      return 1;
    case 19:
      state->passwordChar = safeValue[0] ? safeValue[0] : L'*';
      if (state->inputMode == 2) {
        SendMessageW(hwnd, EM_SETPASSWORDCHAR, static_cast<WPARAM>(state->passwordChar), 0);
        InvalidateRect(hwnd, nullptr, TRUE);
      }
      return 1;
    case 26:
      SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(safeValue));
      return 1;
    case 27:
      state->dataSource = safeValue;
      return 1;
    case 28:
      state->dataCol = safeValue;
      return 1;
    default:
      break;
  }
  return 0;
}

extern "C" int krnln_edit_set_prop_bin_runtime(long long editHandle, int propIndex, const void* value) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  EditRuntimeState* state = ensure_edit_state(hwnd);
  if (!state) return 0;

  if (propIndex == 7) {
    const EditBinView* bin = reinterpret_cast<const EditBinView*>(value);
    if (!bin || !bin->data || bin->size <= 0) {
      state->customCursor = nullptr;
      return 1;
    }
    if (bin->size >= static_cast<int>(sizeof(intptr_t))) {
      intptr_t raw = 0;
      std::memcpy(&raw, bin->data, sizeof(intptr_t));
      state->customCursor = reinterpret_cast<HCURSOR>(raw);
    }
    return 1;
  }

  return (propIndex == 27 || propIndex == 28) ? 1 : 0;
}

extern "C" intptr_t krnln_edit_get_prop_num_runtime(long long editHandle, int propIndex) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  EditRuntimeState* state = ensure_edit_state(hwnd);

  int selStart = 0;
  int selEnd = 0;
  switch (propIndex) {
    case 9: return state ? state->borderMode : 0;
    case 10: return state ? static_cast<intptr_t>(static_cast<uint32_t>(state->textColor)) : 0;
    case 11: return state ? static_cast<intptr_t>(static_cast<uint32_t>(state->backColor)) : 0;
    case 13: return state ? (state->hideSel ? 1 : 0) : 0;
    case 14: return state ? state->maxAllowLength : 0;
    case 15: return state ? (state->allowMultiLines ? 1 : 0) : 0;
    case 16: return state ? state->scrollBarMode : 0;
    case 17: return state ? state->alignMode : 0;
    case 18: return state ? state->inputMode : 0;
    case 20: return state ? state->convertMode : 0;
    case 21: return state ? state->spinMode : 0;
    case 22: return state ? state->spinMin : -32767;
    case 23: return state ? state->spinMax : 32767;
    case 24:
      get_selection_range(hwnd, &selStart, &selEnd);
      return selStart;
    case 25:
      get_selection_range(hwnd, &selStart, &selEnd);
      return std::max(0, selEnd - selStart);
    default:
      return 0;
  }
}

extern "C" const wchar_t* krnln_edit_get_prop_text_runtime(long long editHandle, int propIndex) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return L"";
  EditRuntimeState* state = ensure_edit_state(hwnd);

  switch (propIndex) {
    case 4:
      return state ? store_text_slot(state->tag) : L"";
    case 8:
      return store_text_slot(get_window_text_copy(hwnd));
    case 19: {
      std::wstring one;
      if (state && state->passwordChar) one.push_back(state->passwordChar);
      return store_text_slot(one);
    }
    case 26:
      return get_edit_sel_text(hwnd);
    case 27:
      return state ? store_text_slot(state->dataSource) : L"";
    case 28:
      return state ? store_text_slot(state->dataCol) : L"";
    default:
      return L"";
  }
}

extern "C" int krnln_edit_get_property(long long editHandle, const char* propName) {
  const int index = map_prop_name_to_index(propName);
  if (index < 0) return 0;
  return static_cast<int>(krnln_edit_get_prop_num_runtime(editHandle, index));
}

extern "C" void krnln_edit_set_property(long long editHandle, const char* propName, long long value) {
  const int index = map_prop_name_to_index(propName);
  if (index < 0) return;
  krnln_edit_set_prop_num_runtime(editHandle, index, static_cast<intptr_t>(value));
}

extern "C" void krnln_edit_addtext_impl(long long editHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd) || !text) return;
  const int textLen = GetWindowTextLengthW(hwnd);
  SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(textLen), static_cast<LPARAM>(textLen));
  SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
}

extern "C" void krnln_edit_add_text(long long editHandle, const char* text) {
  if (!text) return;
  int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (len <= 0) return;
  std::vector<wchar_t> wbuf(static_cast<size_t>(len));
  MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf.data(), len);
  krnln_edit_addtext_impl(editHandle, wbuf.data());
}

extern "C" void krnln_edit_clear(long long editHandle) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return;
  SetWindowTextW(hwnd, L"");
  SendMessageW(hwnd, EM_SETSEL, 0, 0);
}

extern "C" void krnln_edit_destroy(long long editHandle) {
  HWND hwnd = as_hwnd(editHandle);
  if (!hwnd || !IsWindow(hwnd)) return;
  EditRuntimeState* state = get_edit_state(hwnd);
  if (state && state->oldWndProc) {
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->oldWndProc));
    destroy_edit_state(hwnd, state);
  }
}
