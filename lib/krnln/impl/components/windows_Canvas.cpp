#include "krnln_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <objidl.h>
#include <vector>
#include <gdiplus.h>

namespace {

constexpr UINT kSetPropMessage = WM_APP + 1;
constexpr WORD kCanvasPaintNotifyCode = 0x7E01;
const wchar_t* kCanvasClassName = L"KRNLN_CANVAS";

struct YCBinView {
  const unsigned char* data;
  int size;
};

struct CanvasState {
  HBITMAP hBitmap = nullptr;
  int bmpWidth = 0;
  int bmpHeight = 0;
  HBITMAP hSurface = nullptr;
  int surfaceW = 0;
  int surfaceH = 0;
  COLORREF backColor = RGB(255, 255, 255);
  int backPicMode = 2; // 0=left-top, 1=tile, 2=center
  bool autoRedraw = true;
  int unit = 0; // 0=px, 1=0.1mm, 2=0.01mm, 3=0.01in, 4=0.001in, 5=1/1440in
  int penStyle = 1;
  int drawRop2 = 12;
  int penWidth = 0;
  COLORREF penColor = RGB(0, 0, 0);
  int brushStyle = 1;
  COLORREF brushColor = RGB(255, 255, 255);
  COLORREF textColor = RGB(0, 0, 0);
  COLORREF textBackColor = RGB(255, 255, 255);
  int writePosX = 0;
  int writePosY = 0;
  std::vector<unsigned char> picData;
};

static ULONG_PTR g_gdiplusToken = 0;

static HWND as_hwnd(long long panelHandle) {
  return reinterpret_cast<HWND>(static_cast<intptr_t>(panelHandle));
}

static CanvasState* get_canvas_state(HWND hwnd) {
  return reinterpret_cast<CanvasState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static void ensure_gdiplus_started() {
  if (g_gdiplusToken != 0) return;
  Gdiplus::GdiplusStartupInput startupInput;
  Gdiplus::GdiplusStartup(&g_gdiplusToken, &startupInput, nullptr);
}

static void destroy_canvas_bitmap(CanvasState* state) {
  if (!state || !state->hBitmap) return;
  DeleteObject(state->hBitmap);
  state->hBitmap = nullptr;
  state->bmpWidth = 0;
  state->bmpHeight = 0;
}

static void destroy_canvas_surface(CanvasState* state) {
  if (!state || !state->hSurface) return;
  DeleteObject(state->hSurface);
  state->hSurface = nullptr;
  state->surfaceW = 0;
  state->surfaceH = 0;
}

static bool rebuild_canvas_bitmap(CanvasState* state) {
  if (!state) return false;
  destroy_canvas_bitmap(state);
  if (state->picData.empty()) return false;

  ensure_gdiplus_started();

  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, state->picData.size());
  if (!hMem) return false;

  void* memPtr = GlobalLock(hMem);
  if (!memPtr) {
    GlobalFree(hMem);
    return false;
  }

  std::memcpy(memPtr, state->picData.data(), state->picData.size());
  GlobalUnlock(hMem);

  IStream* stream = nullptr;
  if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK || !stream) {
    GlobalFree(hMem);
    return false;
  }

  bool ok = false;
  {
    Gdiplus::Bitmap bmp(stream, FALSE);
    if (bmp.GetLastStatus() == Gdiplus::Ok) {
      state->bmpWidth = static_cast<int>(bmp.GetWidth());
      state->bmpHeight = static_cast<int>(bmp.GetHeight());
      Gdiplus::Color bg(255, 255, 255, 255);
      ok = (bmp.GetHBITMAP(bg, &state->hBitmap) == Gdiplus::Ok && state->hBitmap != nullptr);
    }
  }

  stream->Release();
  return ok;
}

static int normalize_back_pic_mode(int mode) {
  if (mode < 0) return 0;
  if (mode > 2) return 2;
  return mode;
}

static int normalize_unit_mode(int unit) {
  if (unit < 0) return 0;
  if (unit > 5) return 5;
  return unit;
}

static int get_dpi_x(HWND hwnd) {
  HDC hdc = GetDC(hwnd);
  if (!hdc) return 96;
  const int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(hwnd, hdc);
  return dpi > 0 ? dpi : 96;
}

static int get_dpi_y(HWND hwnd) {
  HDC hdc = GetDC(hwnd);
  if (!hdc) return 96;
  const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
  ReleaseDC(hwnd, hdc);
  return dpi > 0 ? dpi : 96;
}

static double unit_scale_x(HWND hwnd, int unit) {
  const double dpi = static_cast<double>(get_dpi_x(hwnd));
  switch (normalize_unit_mode(unit)) {
    case 0: return 1.0;
    case 1: return dpi / 254.0;   // 0.1 mm
    case 2: return dpi / 2540.0;  // 0.01 mm
    case 3: return dpi / 100.0;   // 0.01 inch
    case 4: return dpi / 1000.0;  // 0.001 inch
    case 5: return dpi / 1440.0;  // 1/1440 inch
    default: return 1.0;
  }
}

static double unit_scale_y(HWND hwnd, int unit) {
  const double dpi = static_cast<double>(get_dpi_y(hwnd));
  switch (normalize_unit_mode(unit)) {
    case 0: return 1.0;
    case 1: return dpi / 254.0;
    case 2: return dpi / 2540.0;
    case 3: return dpi / 100.0;
    case 4: return dpi / 1000.0;
    case 5: return dpi / 1440.0;
    default: return 1.0;
  }
}

static int unit_to_px_x(HWND hwnd, const CanvasState* state, int value) {
  const int unit = state ? state->unit : 0;
  return static_cast<int>(std::lround(static_cast<double>(value) * unit_scale_x(hwnd, unit)));
}

static int unit_to_px_y(HWND hwnd, const CanvasState* state, int value) {
  const int unit = state ? state->unit : 0;
  return static_cast<int>(std::lround(static_cast<double>(value) * unit_scale_y(hwnd, unit)));
}

static int px_to_unit_x(HWND hwnd, const CanvasState* state, int value) {
  const int unit = state ? state->unit : 0;
  const double scale = unit_scale_x(hwnd, unit);
  if (scale <= 0.0) return value;
  return static_cast<int>(std::lround(static_cast<double>(value) / scale));
}

static int px_to_unit_y(HWND hwnd, const CanvasState* state, int value) {
  const int unit = state ? state->unit : 0;
  const double scale = unit_scale_y(hwnd, unit);
  if (scale <= 0.0) return value;
  return static_cast<int>(std::lround(static_cast<double>(value) / scale));
}

static int map_pen_style(int penStyle) {
  switch (penStyle) {
    case 0: return PS_NULL;
    case 1: return PS_SOLID;
    case 2: return PS_DASH;
    case 3: return PS_DOT;
    case 4: return PS_DASHDOT;
    case 5: return PS_DASHDOTDOT;
    case 6: return PS_INSIDEFRAME;
    default: return PS_SOLID;
  }
}

static int map_rop2_mode(int drawRop2) {
  switch (drawRop2) {
    case 0: return R2_BLACK;
    case 1: return R2_NOTMERGEPEN;
    case 2: return R2_MASKNOTPEN;
    case 3: return R2_NOTCOPYPEN;
    case 4: return R2_MASKPENNOT;
    case 5: return R2_NOT;
    case 6: return R2_XORPEN;
    case 7: return R2_NOTMASKPEN;
    case 8: return R2_MASKPEN;
    case 9: return R2_NOTXORPEN;
    case 10: return R2_NOP;
    case 11: return R2_MERGENOTPEN;
    case 12: return R2_COPYPEN;
    case 13: return R2_MERGEPENNOT;
    case 14: return R2_MERGEPEN;
    case 15: return R2_WHITE;
    default: return R2_COPYPEN;
  }
}

static DWORD map_blit_mode(int mode) {
  switch (mode) {
    case 2: return NOTSRCCOPY;
    case 3: return SRCINVERT;
    case 4: return SRCPAINT;
    case 5: return SRCAND;
    case 1:
    default:
      return SRCCOPY;
  }
}

static COLORREF blend_color(COLORREF c1, COLORREF c2, double t) {
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  const int r = static_cast<int>(GetRValue(c1) + (GetRValue(c2) - GetRValue(c1)) * t);
  const int g = static_cast<int>(GetGValue(c1) + (GetGValue(c2) - GetGValue(c1)) * t);
  const int b = static_cast<int>(GetBValue(c1) + (GetBValue(c2) - GetBValue(c1)) * t);
  return RGB(r, g, b);
}

static bool draw_gradient_rect(HDC dc, int x, int y, int w, int h, int direction, COLORREF c1, COLORREF c2) {
  if (!dc || w <= 0 || h <= 0) return false;

  int dir = direction;
  if (dir < 1 || dir > 8) dir = 2;

  // 1:top->bottom, 2:left->right, 5:bottom->top, 6:right->left
  // diagonal directions fall back to horizontal/vertical approximation for compatibility.
  if (dir == 3) dir = 2; // left-top -> right-bottom
  if (dir == 4) dir = 6; // right-top -> left-bottom
  if (dir == 7) dir = 6; // right-bottom -> left-top
  if (dir == 8) dir = 2; // left-bottom -> right-top

  if (dir == 1 || dir == 5) {
    for (int i = 0; i < h; i++) {
      const double t0 = (h > 1) ? (static_cast<double>(i) / static_cast<double>(h - 1)) : 0.0;
      const double t = (dir == 5) ? (1.0 - t0) : t0;
      HBRUSH brush = CreateSolidBrush(blend_color(c1, c2, t));
      RECT line{ x, y + i, x + w, y + i + 1 };
      FillRect(dc, &line, brush);
      DeleteObject(brush);
    }
    return true;
  }

  for (int i = 0; i < w; i++) {
    const double t0 = (w > 1) ? (static_cast<double>(i) / static_cast<double>(w - 1)) : 0.0;
    const double t = (dir == 6) ? (1.0 - t0) : t0;
    HBRUSH brush = CreateSolidBrush(blend_color(c1, c2, t));
    RECT col{ x + i, y, x + i + 1, y + h };
    FillRect(dc, &col, brush);
    DeleteObject(brush);
  }
  return true;
}

static HPEN create_pen_from_state(HWND hwnd, const CanvasState* state, COLORREF overrideColor, bool useOverrideColor) {
  int width = 1;
  if (state && state->penWidth > 0) {
    width = unit_to_px_x(hwnd, state, state->penWidth);
    if (width < 1) width = 1;
  }
  const int style = map_pen_style(state ? state->penStyle : 1);
  const COLORREF color = useOverrideColor ? overrideColor : (state ? state->penColor : RGB(0, 0, 0));
  return CreatePen(style, width, color);
}

static HBRUSH create_brush_from_state(const CanvasState* state) {
  if (!state || state->brushStyle == 0) {
    return static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
  }
  return CreateSolidBrush(state->brushColor);
}

static void notify_parent(HWND hwnd, WORD code) {
  HWND parent = GetParent(hwnd);
  if (!parent) return;
  const int ctrlId = GetDlgCtrlID(hwnd);
  SendMessageW(parent, WM_COMMAND, MAKEWPARAM(ctrlId, code), reinterpret_cast<LPARAM>(hwnd));
}

static void draw_canvas_background(HDC hdc, int clientW, int clientH, CanvasState* state) {
  RECT rc{ 0, 0, clientW, clientH };
  HBRUSH bg = CreateSolidBrush(state ? state->backColor : RGB(255, 255, 255));
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);

  if (!state || !state->hBitmap || state->bmpWidth <= 0 || state->bmpHeight <= 0) return;

  HDC memDC = CreateCompatibleDC(hdc);
  if (!memDC) return;
  HGDIOBJ oldBmp = SelectObject(memDC, state->hBitmap);

  if (state->backPicMode == 1) {
    for (int y = 0; y < clientH; y += state->bmpHeight) {
      for (int x = 0; x < clientW; x += state->bmpWidth) {
        BitBlt(hdc, x, y, state->bmpWidth, state->bmpHeight, memDC, 0, 0, SRCCOPY);
      }
    }
  } else if (state->backPicMode == 2) {
    int dstW = state->bmpWidth;
    int dstH = state->bmpHeight;
    if (dstW > clientW || dstH > clientH) {
      const double sx = static_cast<double>(clientW) / static_cast<double>(dstW);
      const double sy = static_cast<double>(clientH) / static_cast<double>(dstH);
      const double scale = sx < sy ? sx : sy;
      dstW = static_cast<int>(dstW * scale);
      dstH = static_cast<int>(dstH * scale);
      if (dstW < 1) dstW = 1;
      if (dstH < 1) dstH = 1;
    }
    const int dstX = (clientW - dstW) / 2;
    const int dstY = (clientH - dstH) / 2;
    if (dstW == state->bmpWidth && dstH == state->bmpHeight) {
      BitBlt(hdc, dstX, dstY, dstW, dstH, memDC, 0, 0, SRCCOPY);
    } else {
      SetStretchBltMode(hdc, HALFTONE);
      StretchBlt(hdc, dstX, dstY, dstW, dstH, memDC, 0, 0, state->bmpWidth, state->bmpHeight, SRCCOPY);
    }
  } else {
    BitBlt(hdc, 0, 0, state->bmpWidth, state->bmpHeight, memDC, 0, 0, SRCCOPY);
  }

  SelectObject(memDC, oldBmp);
  DeleteDC(memDC);
}

static bool ensure_canvas_surface(HWND hwnd, CanvasState* state) {
  if (!hwnd || !state) return false;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientW = rc.right - rc.left;
  const int clientH = rc.bottom - rc.top;
  if (clientW <= 0 || clientH <= 0) return false;

  if (state->hSurface && state->surfaceW == clientW && state->surfaceH == clientH) {
    return true;
  }

  destroy_canvas_surface(state);
  HDC wndDC = GetDC(hwnd);
  if (!wndDC) return false;
  HBITMAP surface = CreateCompatibleBitmap(wndDC, clientW, clientH);
  ReleaseDC(hwnd, wndDC);
  if (!surface) return false;

  state->hSurface = surface;
  state->surfaceW = clientW;
  state->surfaceH = clientH;

  HDC memDC = CreateCompatibleDC(nullptr);
  if (!memDC) return false;
  HGDIOBJ oldSurface = SelectObject(memDC, state->hSurface);
  draw_canvas_background(memDC, state->surfaceW, state->surfaceH, state);
  SelectObject(memDC, oldSurface);
  DeleteDC(memDC);
  return true;
}

static void reset_canvas_surface(HWND hwnd, CanvasState* state) {
  if (!state) return;
  if (!state->autoRedraw) {
    destroy_canvas_surface(state);
    return;
  }
  if (!ensure_canvas_surface(hwnd, state)) return;
  HDC memDC = CreateCompatibleDC(nullptr);
  if (!memDC) return;
  HGDIOBJ oldSurface = SelectObject(memDC, state->hSurface);
  draw_canvas_background(memDC, state->surfaceW, state->surfaceH, state);
  SelectObject(memDC, oldSurface);
  DeleteDC(memDC);
}

static HDC acquire_canvas_surface_dc(HWND hwnd, CanvasState* state, HGDIOBJ* oldSurfaceOut) {
  if (!oldSurfaceOut) return nullptr;
  *oldSurfaceOut = nullptr;
  if (!state || !state->autoRedraw) return nullptr;
  if (!ensure_canvas_surface(hwnd, state)) return nullptr;

  HDC memDC = CreateCompatibleDC(nullptr);
  if (!memDC) return nullptr;
  *oldSurfaceOut = SelectObject(memDC, state->hSurface);
  return memDC;
}

static void release_canvas_surface_dc(HDC dc, HGDIOBJ oldSurface) {
  if (!dc) return;
  if (oldSurface) SelectObject(dc, oldSurface);
  DeleteDC(dc);
}

static void paint_canvas(HWND hwnd, HDC hdc, CanvasState* state) {
  if (!state) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    return;
  }
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientW = rc.right - rc.left;
  const int clientH = rc.bottom - rc.top;
  if (clientW <= 0 || clientH <= 0) return;

  if (!state->autoRedraw) {
    draw_canvas_background(hdc, clientW, clientH, state);
    return;
  }
  if (!ensure_canvas_surface(hwnd, state)) {
    draw_canvas_background(hdc, clientW, clientH, state);
    return;
  }
  HDC memDC = CreateCompatibleDC(hdc);
  if (!memDC) return;
  HGDIOBJ oldSurface = SelectObject(memDC, state->hSurface);
  BitBlt(hdc, 0, 0, state->surfaceW, state->surfaceH, memDC, 0, 0, SRCCOPY);
  SelectObject(memDC, oldSurface);
  DeleteDC(memDC);
}

static void set_canvas_pic_data(CanvasState* state, const unsigned char* data, int len) {
  if (!state) return;
  if (!data || len <= 0) {
    state->picData.clear();
    destroy_canvas_bitmap(state);
    return;
  }
  state->picData.assign(data, data + len);
  rebuild_canvas_bitmap(state);
}

static bool draw_canvas_text(HWND hwnd, CanvasState* state, const wchar_t* text, bool moveNextLine, bool keepOldPos) {
  if (!state || !text) return false;
  const int len = static_cast<int>(std::wcslen(text));

  HGDIOBJ oldSurface = nullptr;
  HDC dc = nullptr;
  if (state->autoRedraw) {
    dc = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
  } else {
    dc = GetDC(hwnd);
  }
  if (!dc) return false;

  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = nullptr;
  if (font) oldFont = SelectObject(dc, font);

  const int oldBkMode = SetBkMode(dc, OPAQUE);
  const COLORREF oldTextColor = SetTextColor(dc, state->textColor);
  const COLORREF oldBkColor = SetBkColor(dc, state->textBackColor);
  const int oldRop2 = SetROP2(dc, map_rop2_mode(state->drawRop2));

  const BOOL ok = TextOutW(dc, state->writePosX, state->writePosY, text, len);

  SIZE ext{};
  if (ok) {
    GetTextExtentPoint32W(dc, text, len, &ext);
    if (!keepOldPos) {
      if (moveNextLine) {
        state->writePosX = 0;
        state->writePosY += ext.cy;
      } else {
        state->writePosX += ext.cx;
      }
    }
  }

  SetROP2(dc, oldRop2);
  SetTextColor(dc, oldTextColor);
  SetBkColor(dc, oldBkColor);
  SetBkMode(dc, oldBkMode);
  if (oldFont) SelectObject(dc, oldFont);

  if (state->autoRedraw) {
    release_canvas_surface_dc(dc, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, dc);
  }
  return ok ? true : false;
}

static void apply_control_position_size_prop(HWND hwnd, int propIndex, intptr_t value) {
  HWND parent = GetParent(hwnd);
  RECT rc{};
  GetWindowRect(hwnd, &rc);
  if (parent) {
    MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&rc), 2);
  }

  int x = rc.left;
  int y = rc.top;
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  switch (propIndex) {
    case 0: x = static_cast<int>(value); break; // left
    case 1: y = static_cast<int>(value); break; // top
    case 2: w = static_cast<int>(value); if (w < 1) w = 1; break; // width
    case 3: h = static_cast<int>(value); if (h < 1) h = 1; break; // height
    default: return;
  }

  SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT handle_canvas_set_prop(HWND hwnd, CanvasState* state, WPARAM wParam, LPARAM lParam) {
  if (!state) return 0;

  const int propIndex = static_cast<int>(wParam);
  const intptr_t intValue = static_cast<intptr_t>(lParam);

  switch (propIndex) {
    case 0:
    case 1:
    case 2:
    case 3:
      apply_control_position_size_prop(hwnd, propIndex, intValue);
      return 1;

    case 5: // visible
      ShowWindow(hwnd, intValue ? SW_SHOW : SW_HIDE);
      return 1;

    case 6: // disable
      EnableWindow(hwnd, intValue ? FALSE : TRUE);
      return 1;

    case 8: { // border
      LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (intValue == 0) style &= ~WS_BORDER;
      else style |= WS_BORDER;
      SetWindowLongPtrW(hwnd, GWL_STYLE, style);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;
    }

    case 9: // BackColor
      state->backColor = static_cast<COLORREF>(static_cast<uint32_t>(intValue));
      reset_canvas_surface(hwnd, state);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;

    case 10: { // pic
      const YCBinView* bin = reinterpret_cast<const YCBinView*>(lParam);
      if (!bin || !bin->data || bin->size <= 0) {
        set_canvas_pic_data(state, nullptr, 0);
      } else {
        set_canvas_pic_data(state, bin->data, bin->size);
      }
      reset_canvas_surface(hwnd, state);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;
    }

    case 11: // BackPicMode
      state->backPicMode = normalize_back_pic_mode(static_cast<int>(intValue));
      reset_canvas_surface(hwnd, state);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;

    case 12: // AutoRedraw
      state->autoRedraw = (intValue != 0);
      reset_canvas_surface(hwnd, state);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 1;

    case 13: // unit
      state->unit = normalize_unit_mode(static_cast<int>(intValue));
      return 1;

    case 14: // PenStyle
      state->penStyle = static_cast<int>(intValue);
      return 1;

    case 15: // DrawRop2
      state->drawRop2 = static_cast<int>(intValue);
      return 1;

    case 16: // PenWidth
      state->penWidth = static_cast<int>(intValue);
      return 1;

    case 17: // PenColor
      state->penColor = static_cast<COLORREF>(static_cast<uint32_t>(intValue));
      return 1;

    case 18: // BrushStyle
      state->brushStyle = static_cast<int>(intValue);
      return 1;

    case 19: // BrushColor
      state->brushColor = static_cast<COLORREF>(static_cast<uint32_t>(intValue));
      return 1;

    case 20: // TextColor
      state->textColor = static_cast<COLORREF>(static_cast<uint32_t>(intValue));
      return 1;

    case 21: // TextBackColor
      state->textBackColor = static_cast<COLORREF>(static_cast<uint32_t>(intValue));
      return 1;

    default:
      return 0;
  }
}

static LRESULT CALLBACK krnln_canvas_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_NCCREATE: {
      CanvasState* state = new CanvasState();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      break;
    }

    case WM_NCDESTROY: {
      CanvasState* state = get_canvas_state(hwnd);
      if (state) {
        destroy_canvas_bitmap(state);
        destroy_canvas_surface(state);
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      break;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      CanvasState* state = get_canvas_state(hwnd);
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      paint_canvas(hwnd, hdc, state);
      EndPaint(hwnd, &ps);
      notify_parent(hwnd, kCanvasPaintNotifyCode);
      return 0;
    }

    case WM_LBUTTONUP:
      notify_parent(hwnd, STN_CLICKED);
      break;

    case WM_SIZE: {
      CanvasState* state = get_canvas_state(hwnd);
      if (state) {
        destroy_canvas_surface(state);
        reset_canvas_surface(hwnd, state);
      }
      break;
    }

    case kSetPropMessage:
      return handle_canvas_set_prop(hwnd, get_canvas_state(hwnd), wParam, lParam);

    default:
      break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool is_canvas_window(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return false;
  wchar_t className[64] = {};
  if (GetClassNameW(hwnd, className, static_cast<int>(sizeof(className) / sizeof(className[0]))) <= 0) {
    return false;
  }
  return lstrcmpiW(className, kCanvasClassName) == 0;
}

} // namespace

extern "C" const wchar_t* krnln_control_get_text(long long controlHandle) {
  static std::vector<wchar_t> slots[4];
  static int slotIndex = 0;

  HWND hwnd = as_hwnd(controlHandle);
  if (!hwnd || !IsWindow(hwnd)) return L"";

  const int slot = (slotIndex++) & 3;
  int len = GetWindowTextLengthW(hwnd);
  if (len < 0) len = 0;
  slots[slot].assign(static_cast<size_t>(len) + 1u, L'\0');
  GetWindowTextW(hwnd, slots[slot].data(), len + 1);
  return slots[slot].empty() ? L"" : slots[slot].data();
}

extern "C" int krnln_control_set_text(long long controlHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(controlHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  return SetWindowTextW(hwnd, text ? text : L"") ? 1 : 0;
}

extern "C" int krnln_control_set_prop_num(long long controlHandle, int propIndex, intptr_t value) {
  HWND hwnd = as_hwnd(controlHandle);
  if (!hwnd || !IsWindow(hwnd) || propIndex < 0) return 0;

  if (is_canvas_window(hwnd)) {
    return static_cast<int>(SendMessageW(hwnd, kSetPropMessage, static_cast<WPARAM>(propIndex), static_cast<LPARAM>(value)) != 0);
  }

  switch (propIndex) {
    case 0:
    case 1:
    case 2:
    case 3:
      apply_control_position_size_prop(hwnd, propIndex, value);
      return 1;
    case 5:
      ShowWindow(hwnd, value ? SW_SHOW : SW_HIDE);
      return 1;
    case 6:
      EnableWindow(hwnd, value ? FALSE : TRUE);
      return 1;
    default:
      break;
  }

  return static_cast<int>(SendMessageW(hwnd, kSetPropMessage, static_cast<WPARAM>(propIndex), static_cast<LPARAM>(value)) != 0);
}

extern "C" int krnln_control_set_prop_bin(long long controlHandle, int propIndex, const void* value) {
  HWND hwnd = as_hwnd(controlHandle);
  if (!hwnd || !IsWindow(hwnd) || propIndex < 0) return 0;
  return static_cast<int>(SendMessageW(hwnd, kSetPropMessage, static_cast<WPARAM>(propIndex), reinterpret_cast<LPARAM>(value)) != 0);
}

extern "C" int krnln_init_window_units(void) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = krnln_canvas_wnd_proc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hIcon = nullptr;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = kCanvasClassName;
  wc.hIconSm = nullptr;

  if (RegisterClassExW(&wc)) return 1;
  if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) return 1;
  return 0;
}

extern "C" long long krnln_drawpanel_get_hdc(long long panelHandle) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  HDC hdc = GetDC(hwnd);
  return static_cast<long long>(reinterpret_cast<intptr_t>(hdc));
}

extern "C" int krnln_drawpanel_clear(long long panelHandle, int left, int top, int width, int height) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxLeft = unit_to_px_x(hwnd, state, left);
    const int pxTop = unit_to_px_y(hwnd, state, top);
    int pxWidth = unit_to_px_x(hwnd, state, width);
    int pxHeight = unit_to_px_y(hwnd, state, height);
    if (width > 0 && pxWidth < 1) pxWidth = 1;
    if (height > 0 && pxHeight < 1) pxHeight = 1;
    if (width <= 0 || height <= 0) {
      if (!state->autoRedraw) {
        HDC hdc = GetDC(hwnd);
        if (!hdc) return 0;
        RECT fullRect{};
        GetClientRect(hwnd, &fullRect);
        HBRUSH brush = CreateSolidBrush(state->backColor);
        FillRect(hdc, &fullRect, brush);
        DeleteObject(brush);
        ReleaseDC(hwnd, hdc);
        return 1;
      }
      reset_canvas_surface(hwnd, state);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 1;
    }

    RECT rect{ pxLeft, pxTop, pxLeft + pxWidth, pxTop + pxHeight };
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HBRUSH brush = CreateSolidBrush(state->backColor);
      FillRect(memDC, &rect, brush);
      DeleteObject(brush);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, &rect, FALSE);
      return 1;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HBRUSH brush = CreateSolidBrush(state->backColor);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    ReleaseDC(hwnd, hdc);
    return 1;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  RECT rect{};
  GetClientRect(hwnd, &rect);
  if (width > 0 && height > 0) {
    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + height;
  }

  COLORREF color = RGB(255, 255, 255);
  if (CanvasState* state = get_canvas_state(hwnd)) {
    color = state->backColor;
  }

  HBRUSH brush = CreateSolidBrush(color);
  FillRect(hdc, &rect, brush);
  DeleteObject(brush);
  ReleaseDC(hwnd, hdc);
  InvalidateRect(hwnd, &rect, FALSE);
  return 1;
}

extern "C" int krnln_drawpanel_get_pixel(long long panelHandle, int x, int y) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return -1;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return -1;
    const int pxX = unit_to_px_x(hwnd, state, x);
    const int pxY = unit_to_px_y(hwnd, state, y);
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return -1;
      COLORREF color = GetPixel(memDC, pxX, pxY);
      release_canvas_surface_dc(memDC, oldSurface);
      if (color == CLR_INVALID) return -1;
      return static_cast<int>(color);
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return -1;
    COLORREF color = GetPixel(hdc, pxX, pxY);
    ReleaseDC(hwnd, hdc);
    if (color == CLR_INVALID) return -1;
    return static_cast<int>(color);
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return -1;
  COLORREF color = GetPixel(hdc, x, y);
  ReleaseDC(hwnd, hdc);
  if (color == CLR_INVALID) return -1;
  return static_cast<int>(color);
}

extern "C" int krnln_drawpanel_set_pixel(long long panelHandle, int x, int y, int color) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxX = unit_to_px_x(hwnd, state, x);
    const int pxY = unit_to_px_y(hwnd, state, y);
    HGDIOBJ oldSurface = nullptr;
    if (state->autoRedraw) {
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      COLORREF written = SetPixel(memDC, pxX, pxY, static_cast<COLORREF>(color));
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return written == CLR_INVALID ? 0 : 1;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    COLORREF written = SetPixel(hdc, pxX, pxY, static_cast<COLORREF>(color));
    ReleaseDC(hwnd, hdc);
    return written == CLR_INVALID ? 0 : 1;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;
  COLORREF written = SetPixel(hdc, x, y, static_cast<COLORREF>(color));
  ReleaseDC(hwnd, hdc);
  return written == CLR_INVALID ? 0 : 1;
}

extern "C" int krnln_drawpanel_line(long long panelHandle, int x1, int y1, int x2, int y2, int color) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxX1 = unit_to_px_x(hwnd, state, x1);
    const int pxY1 = unit_to_px_y(hwnd, state, y1);
    const int pxX2 = unit_to_px_x(hwnd, state, x2);
    const int pxY2 = unit_to_px_y(hwnd, state, y2);
    HGDIOBJ oldSurface = nullptr;
    if (state->autoRedraw) {
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HPEN pen = create_pen_from_state(hwnd, state, static_cast<COLORREF>(color), true);
      HGDIOBJ oldPen = SelectObject(memDC, pen);
      int oldRop2 = SetROP2(memDC, map_rop2_mode(state->drawRop2));
      MoveToEx(memDC, pxX1, pxY1, nullptr);
      const BOOL ok = LineTo(memDC, pxX2, pxY2);
      SetROP2(memDC, oldRop2);
      SelectObject(memDC, oldPen);
      DeleteObject(pen);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HPEN pen = create_pen_from_state(hwnd, state, static_cast<COLORREF>(color), true);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    int oldRop2 = SetROP2(hdc, map_rop2_mode(state->drawRop2));
    MoveToEx(hdc, pxX1, pxY1, nullptr);
    const BOOL ok = LineTo(hdc, pxX2, pxY2);
    SetROP2(hdc, oldRop2);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  HPEN pen = create_pen_from_state(hwnd, nullptr, static_cast<COLORREF>(color), true);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  int oldRop2 = SetROP2(hdc, R2_COPYPEN);
  MoveToEx(hdc, x1, y1, nullptr);
  const BOOL ok = LineTo(hdc, x2, y2);
  SetROP2(hdc, oldRop2);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_set_pic(long long panelHandle, const unsigned char* data, int len) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || len < 0) return 0;

  if (is_canvas_window(hwnd)) {
    YCBinView view{};
    view.data = data;
    view.size = len;
    return static_cast<int>(SendMessageW(hwnd, kSetPropMessage, 10, reinterpret_cast<LPARAM>(&view)) != 0);
  }

  return 0;
}

extern "C" int krnln_drawpanel_draw_rect(long long panelHandle, int left, int top, int right, int bottom) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxLeft = unit_to_px_x(hwnd, state, left);
    const int pxTop = unit_to_px_y(hwnd, state, top);
    const int pxRight = unit_to_px_x(hwnd, state, right);
    const int pxBottom = unit_to_px_y(hwnd, state, bottom);
    HGDIOBJ oldSurface = nullptr;
    if (state->autoRedraw) {
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HPEN pen = create_pen_from_state(hwnd, state, 0, false);
      HBRUSH brush = create_brush_from_state(state);
      HGDIOBJ oldPen = SelectObject(memDC, pen);
      HGDIOBJ oldBrush = SelectObject(memDC, brush);
      int oldRop2 = SetROP2(memDC, map_rop2_mode(state->drawRop2));
      BOOL ok = Rectangle(memDC, pxLeft, pxTop, pxRight, pxBottom);
      SetROP2(memDC, oldRop2);
      SelectObject(memDC, oldBrush);
      SelectObject(memDC, oldPen);
      if (state->brushStyle != 0) DeleteObject(brush);
      DeleteObject(pen);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HPEN pen = create_pen_from_state(hwnd, state, 0, false);
    HBRUSH brush = create_brush_from_state(state);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    int oldRop2 = SetROP2(hdc, map_rop2_mode(state->drawRop2));
    BOOL ok = Rectangle(hdc, pxLeft, pxTop, pxRight, pxBottom);
    SetROP2(hdc, oldRop2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    if (state->brushStyle != 0) DeleteObject(brush);
    DeleteObject(pen);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  HPEN pen = create_pen_from_state(hwnd, nullptr, 0, false);
  HBRUSH brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  int oldRop2 = SetROP2(hdc, R2_COPYPEN);
  BOOL ok = Rectangle(hdc, left, top, right, bottom);
  SetROP2(hdc, oldRop2);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_fill_rect(long long panelHandle, int left, int top, int right, int bottom) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    RECT rect{
      unit_to_px_x(hwnd, state, left),
      unit_to_px_y(hwnd, state, top),
      unit_to_px_x(hwnd, state, right),
      unit_to_px_y(hwnd, state, bottom)
    };
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HBRUSH brush = create_brush_from_state(state);
      BOOL ok = FillRect(memDC, &rect, brush);
      if (state->brushStyle != 0) DeleteObject(brush);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HBRUSH brush = create_brush_from_state(state);
    BOOL ok = FillRect(hdc, &rect, brush);
    if (state->brushStyle != 0) DeleteObject(brush);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  RECT rect{ left, top, right, bottom };
  HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
  BOOL ok = FillRect(hdc, &rect, brush);
  DeleteObject(brush);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_draw_ellipse(long long panelHandle, int left, int top, int right, int bottom) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxLeft = unit_to_px_x(hwnd, state, left);
    const int pxTop = unit_to_px_y(hwnd, state, top);
    const int pxRight = unit_to_px_x(hwnd, state, right);
    const int pxBottom = unit_to_px_y(hwnd, state, bottom);
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HPEN pen = create_pen_from_state(hwnd, state, 0, false);
      HBRUSH brush = create_brush_from_state(state);
      HGDIOBJ oldPen = SelectObject(memDC, pen);
      HGDIOBJ oldBrush = SelectObject(memDC, brush);
      int oldRop2 = SetROP2(memDC, map_rop2_mode(state->drawRop2));
      BOOL ok = Ellipse(memDC, pxLeft, pxTop, pxRight, pxBottom);
      SetROP2(memDC, oldRop2);
      SelectObject(memDC, oldBrush);
      SelectObject(memDC, oldPen);
      if (state->brushStyle != 0) DeleteObject(brush);
      DeleteObject(pen);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HPEN pen = create_pen_from_state(hwnd, state, 0, false);
    HBRUSH brush = create_brush_from_state(state);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    int oldRop2 = SetROP2(hdc, map_rop2_mode(state->drawRop2));
    BOOL ok = Ellipse(hdc, pxLeft, pxTop, pxRight, pxBottom);
    SetROP2(hdc, oldRop2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    if (state->brushStyle != 0) DeleteObject(brush);
    DeleteObject(pen);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  HPEN pen = create_pen_from_state(hwnd, nullptr, 0, false);
  HBRUSH brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  int oldRop2 = SetROP2(hdc, R2_COPYPEN);
  BOOL ok = Ellipse(hdc, left, top, right, bottom);
  SetROP2(hdc, oldRop2);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_draw_round_rect(long long panelHandle, int left, int top, int right, int bottom, int arcWidth, int arcHeight) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    const int pxLeft = unit_to_px_x(hwnd, state, left);
    const int pxTop = unit_to_px_y(hwnd, state, top);
    const int pxRight = unit_to_px_x(hwnd, state, right);
    const int pxBottom = unit_to_px_y(hwnd, state, bottom);
    const int aw = arcWidth > 0 ? arcWidth : 8;
    const int ah = arcHeight > 0 ? arcHeight : aw;
    int pxAw = unit_to_px_x(hwnd, state, aw);
    int pxAh = unit_to_px_y(hwnd, state, ah);
    if (pxAw < 1) pxAw = 1;
    if (pxAh < 1) pxAh = 1;
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      HPEN pen = create_pen_from_state(hwnd, state, 0, false);
      HBRUSH brush = create_brush_from_state(state);
      HGDIOBJ oldPen = SelectObject(memDC, pen);
      HGDIOBJ oldBrush = SelectObject(memDC, brush);
      int oldRop2 = SetROP2(memDC, map_rop2_mode(state->drawRop2));
      BOOL ok = RoundRect(memDC, pxLeft, pxTop, pxRight, pxBottom, pxAw, pxAh);
      SetROP2(memDC, oldRop2);
      SelectObject(memDC, oldBrush);
      SelectObject(memDC, oldPen);
      if (state->brushStyle != 0) DeleteObject(brush);
      DeleteObject(pen);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    HPEN pen = create_pen_from_state(hwnd, state, 0, false);
    HBRUSH brush = create_brush_from_state(state);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    int oldRop2 = SetROP2(hdc, map_rop2_mode(state->drawRop2));
    BOOL ok = RoundRect(hdc, pxLeft, pxTop, pxRight, pxBottom, pxAw, pxAh);
    SetROP2(hdc, oldRop2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    if (state->brushStyle != 0) DeleteObject(brush);
    DeleteObject(pen);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  const int aw = arcWidth > 0 ? arcWidth : 8;
  const int ah = arcHeight > 0 ? arcHeight : aw;

  HPEN pen = create_pen_from_state(hwnd, nullptr, 0, false);
  HBRUSH brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  int oldRop2 = SetROP2(hdc, R2_COPYPEN);
  BOOL ok = RoundRect(hdc, left, top, right, bottom, aw, ah);
  SetROP2(hdc, oldRop2);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_invert_rect(long long panelHandle, int left, int top, int right, int bottom) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  if (is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (!state) return 0;
    RECT rect{
      unit_to_px_x(hwnd, state, left),
      unit_to_px_y(hwnd, state, top),
      unit_to_px_x(hwnd, state, right),
      unit_to_px_y(hwnd, state, bottom)
    };
    if (state->autoRedraw) {
      HGDIOBJ oldSurface = nullptr;
      HDC memDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
      if (!memDC) return 0;
      BOOL ok = InvertRect(memDC, &rect);
      release_canvas_surface_dc(memDC, oldSurface);
      InvalidateRect(hwnd, nullptr, FALSE);
      return ok ? 1 : 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 0;
    BOOL ok = InvertRect(hdc, &rect);
    ReleaseDC(hwnd, hdc);
    return ok ? 1 : 0;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) return 0;

  RECT rect{ left, top, right, bottom };
  BOOL ok = InvertRect(hdc, &rect);
  ReleaseDC(hwnd, hdc);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_arc(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  const int l = unit_to_px_x(hwnd, state, left);
  const int t = unit_to_px_y(hwnd, state, top);
  const int r = unit_to_px_x(hwnd, state, right);
  const int b = unit_to_px_y(hwnd, state, bottom);
  const int sx = unit_to_px_x(hwnd, state, startX);
  const int sy = unit_to_px_y(hwnd, state, startY);
  const int ex = unit_to_px_x(hwnd, state, endX);
  const int ey = unit_to_px_y(hwnd, state, endY);

  HGDIOBJ oldSurface = nullptr;
  HDC dc = state->autoRedraw ? acquire_canvas_surface_dc(hwnd, state, &oldSurface) : GetDC(hwnd);
  if (!dc) return 0;

  HPEN pen = create_pen_from_state(hwnd, state, 0, false);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  const int oldRop2 = SetROP2(dc, map_rop2_mode(state->drawRop2));
  const BOOL ok = Arc(dc, l, t, r, b, sx, sy, ex, ey);
  SetROP2(dc, oldRop2);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(pen);

  if (state->autoRedraw) {
    release_canvas_surface_dc(dc, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, dc);
  }
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_chord(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  const int l = unit_to_px_x(hwnd, state, left);
  const int t = unit_to_px_y(hwnd, state, top);
  const int r = unit_to_px_x(hwnd, state, right);
  const int b = unit_to_px_y(hwnd, state, bottom);
  const int sx = unit_to_px_x(hwnd, state, startX);
  const int sy = unit_to_px_y(hwnd, state, startY);
  const int ex = unit_to_px_x(hwnd, state, endX);
  const int ey = unit_to_px_y(hwnd, state, endY);

  HGDIOBJ oldSurface = nullptr;
  HDC dc = state->autoRedraw ? acquire_canvas_surface_dc(hwnd, state, &oldSurface) : GetDC(hwnd);
  if (!dc) return 0;

  HPEN pen = create_pen_from_state(hwnd, state, 0, false);
  HBRUSH brush = create_brush_from_state(state);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  const int oldRop2 = SetROP2(dc, map_rop2_mode(state->drawRop2));
  const BOOL ok = Chord(dc, l, t, r, b, sx, sy, ex, ey);
  SetROP2(dc, oldRop2);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  if (state->brushStyle != 0) DeleteObject(brush);
  DeleteObject(pen);

  if (state->autoRedraw) {
    release_canvas_surface_dc(dc, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, dc);
  }
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_pie(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  const int l = unit_to_px_x(hwnd, state, left);
  const int t = unit_to_px_y(hwnd, state, top);
  const int r = unit_to_px_x(hwnd, state, right);
  const int b = unit_to_px_y(hwnd, state, bottom);
  const int sx = unit_to_px_x(hwnd, state, startX);
  const int sy = unit_to_px_y(hwnd, state, startY);
  const int ex = unit_to_px_x(hwnd, state, endX);
  const int ey = unit_to_px_y(hwnd, state, endY);

  HGDIOBJ oldSurface = nullptr;
  HDC dc = state->autoRedraw ? acquire_canvas_surface_dc(hwnd, state, &oldSurface) : GetDC(hwnd);
  if (!dc) return 0;

  HPEN pen = create_pen_from_state(hwnd, state, 0, false);
  HBRUSH brush = create_brush_from_state(state);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  const int oldRop2 = SetROP2(dc, map_rop2_mode(state->drawRop2));
  const BOOL ok = Pie(dc, l, t, r, b, sx, sy, ex, ey);
  SetROP2(dc, oldRop2);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  if (state->brushStyle != 0) DeleteObject(brush);
  DeleteObject(pen);

  if (state->autoRedraw) {
    release_canvas_surface_dc(dc, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, dc);
  }
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_unit_cnv(long long panelHandle, int value, int valueType) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return value;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return value;

  switch (valueType) {
    case 1: return unit_to_px_x(hwnd, state, value);
    case 2: return unit_to_px_y(hwnd, state, value);
    case 3: return px_to_unit_x(hwnd, state, value);
    case 4: return px_to_unit_y(hwnd, state, value);
    default: return value;
  }
}

extern "C" int krnln_drawpanel_set_write_pos(long long panelHandle, int x, int y) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  if (x != 0x7fffffff) state->writePosX = unit_to_px_x(hwnd, state, x);
  if (y != 0x7fffffff) state->writePosY = unit_to_px_y(hwnd, state, y);
  return 1;
}

extern "C" int krnln_drawpanel_write(long long panelHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;
  return draw_canvas_text(hwnd, state, text ? text : L"", false, false) ? 1 : 0;
}

extern "C" int krnln_drawpanel_print(long long panelHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;
  return draw_canvas_text(hwnd, state, text ? text : L"", true, false) ? 1 : 0;
}

extern "C" int krnln_drawpanel_say(long long panelHandle, int x, int y, const wchar_t* text) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  const int oldX = state->writePosX;
  const int oldY = state->writePosY;
  if (x != 0x7fffffff) state->writePosX = unit_to_px_x(hwnd, state, x);
  if (y != 0x7fffffff) state->writePosY = unit_to_px_y(hwnd, state, y);
  const bool ok = draw_canvas_text(hwnd, state, text ? text : L"", false, true);
  state->writePosX = oldX;
  state->writePosY = oldY;
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_get_text_width(long long panelHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  HGDIOBJ oldSurface = nullptr;
  HDC dc = state->autoRedraw ? acquire_canvas_surface_dc(hwnd, state, &oldSurface) : GetDC(hwnd);
  if (!dc) return 0;
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = nullptr;
  if (font) oldFont = SelectObject(dc, font);
  SIZE ext{};
  const wchar_t* safe = text ? text : L"";
  GetTextExtentPoint32W(dc, safe, static_cast<int>(std::wcslen(safe)), &ext);
  if (oldFont) SelectObject(dc, oldFont);
  if (state->autoRedraw) release_canvas_surface_dc(dc, oldSurface);
  else ReleaseDC(hwnd, dc);
  return px_to_unit_x(hwnd, state, ext.cx);
}

extern "C" int krnln_drawpanel_get_text_height(long long panelHandle, const wchar_t* text) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd) || !is_canvas_window(hwnd)) return 0;
  CanvasState* state = get_canvas_state(hwnd);
  if (!state) return 0;

  HGDIOBJ oldSurface = nullptr;
  HDC dc = state->autoRedraw ? acquire_canvas_surface_dc(hwnd, state, &oldSurface) : GetDC(hwnd);
  if (!dc) return 0;
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = nullptr;
  if (font) oldFont = SelectObject(dc, font);
  SIZE ext{};
  const wchar_t* safe = text ? text : L"";
  GetTextExtentPoint32W(dc, safe, static_cast<int>(std::wcslen(safe)), &ext);
  if (oldFont) SelectObject(dc, oldFont);
  if (state->autoRedraw) release_canvas_surface_dc(dc, oldSurface);
  else ReleaseDC(hwnd, dc);
  return px_to_unit_y(hwnd, state, ext.cy);
}

extern "C" int krnln_drawpanel_draw_jb_rect(long long panelHandle, int left, int top, int width, int height, int direction, int color1, int color2) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;

  int x = left;
  int y = top;
  int w = width;
  int h = height;
  CanvasState* state = nullptr;

  if (is_canvas_window(hwnd)) {
    state = get_canvas_state(hwnd);
    if (!state) return 0;
    x = unit_to_px_x(hwnd, state, left);
    y = unit_to_px_y(hwnd, state, top);
    w = unit_to_px_x(hwnd, state, width);
    h = unit_to_px_y(hwnd, state, height);
  }

  if (w <= 0 || h <= 0) return 0;

  HGDIOBJ oldSurface = nullptr;
  HDC dc = nullptr;
  if (state && state->autoRedraw) dc = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
  else dc = GetDC(hwnd);
  if (!dc) return 0;

  const BOOL ok = draw_gradient_rect(
    dc,
    x,
    y,
    w,
    h,
    direction,
    static_cast<COLORREF>(static_cast<uint32_t>(color1)),
    static_cast<COLORREF>(static_cast<uint32_t>(color2))
  ) ? TRUE : FALSE;

  if (state && state->autoRedraw) {
    release_canvas_surface_dc(dc, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, dc);
  }
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_copy(long long srcPanelHandle, int left, int top, int width, int height, long long dstPanelHandle, int dstLeft, int dstTop, int mode) {
  HWND srcHwnd = as_hwnd(srcPanelHandle);
  if (!srcHwnd || !IsWindow(srcHwnd)) return 0;

  HWND dstHwnd = as_hwnd(dstPanelHandle);
  if (!dstHwnd || !IsWindow(dstHwnd)) dstHwnd = srcHwnd;

  CanvasState* srcState = is_canvas_window(srcHwnd) ? get_canvas_state(srcHwnd) : nullptr;
  CanvasState* dstState = is_canvas_window(dstHwnd) ? get_canvas_state(dstHwnd) : nullptr;

  int srcX = left;
  int srcY = top;
  int srcW = width;
  int srcH = height;
  int outX = dstLeft;
  int outY = dstTop;

  if (srcState) {
    srcX = unit_to_px_x(srcHwnd, srcState, left);
    srcY = unit_to_px_y(srcHwnd, srcState, top);
    srcW = unit_to_px_x(srcHwnd, srcState, width);
    srcH = unit_to_px_y(srcHwnd, srcState, height);
  }
  if (dstState) {
    outX = unit_to_px_x(dstHwnd, dstState, dstLeft);
    outY = unit_to_px_y(dstHwnd, dstState, dstTop);
  }

  RECT srcClient{};
  GetClientRect(srcHwnd, &srcClient);
  const int srcClientW = srcClient.right - srcClient.left;
  const int srcClientH = srcClient.bottom - srcClient.top;

  if (srcW <= 0) srcW = srcClientW - srcX;
  if (srcH <= 0) srcH = srcClientH - srcY;
  if (srcW <= 0 || srcH <= 0) return 0;

  HGDIOBJ srcOldSurface = nullptr;
  HDC srcDC = nullptr;
  if (srcState && srcState->autoRedraw) srcDC = acquire_canvas_surface_dc(srcHwnd, srcState, &srcOldSurface);
  else srcDC = GetDC(srcHwnd);
  if (!srcDC) return 0;

  HGDIOBJ dstOldSurface = nullptr;
  HDC dstDC = nullptr;
  if (dstState && dstState->autoRedraw) dstDC = acquire_canvas_surface_dc(dstHwnd, dstState, &dstOldSurface);
  else dstDC = GetDC(dstHwnd);
  if (!dstDC) {
    if (srcState && srcState->autoRedraw) release_canvas_surface_dc(srcDC, srcOldSurface);
    else ReleaseDC(srcHwnd, srcDC);
    return 0;
  }

  const BOOL ok = BitBlt(dstDC, outX, outY, srcW, srcH, srcDC, srcX, srcY, map_blit_mode(mode));

  if (srcState && srcState->autoRedraw) release_canvas_surface_dc(srcDC, srcOldSurface);
  else ReleaseDC(srcHwnd, srcDC);

  if (dstState && dstState->autoRedraw) {
    release_canvas_surface_dc(dstDC, dstOldSurface);
    InvalidateRect(dstHwnd, nullptr, FALSE);
  } else {
    ReleaseDC(dstHwnd, dstDC);
  }

  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_draw_pic(long long panelHandle, long long picHandle, int x, int y, int width, int height, int mode) {
  HWND hwnd = as_hwnd(panelHandle);
  if (!hwnd || !IsWindow(hwnd)) return 0;
  HBITMAP hBitmap = reinterpret_cast<HBITMAP>(static_cast<intptr_t>(picHandle));
  if (!hBitmap) return 0;

  BITMAP bm{};
  if (GetObjectW(hBitmap, sizeof(bm), &bm) != sizeof(bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return 0;

  CanvasState* state = is_canvas_window(hwnd) ? get_canvas_state(hwnd) : nullptr;
  int outX = x;
  int outY = y;
  int outW = width;
  int outH = height;

  if (state) {
    outX = unit_to_px_x(hwnd, state, x);
    outY = unit_to_px_y(hwnd, state, y);
    outW = (width > 0) ? unit_to_px_x(hwnd, state, width) : 0;
    outH = (height > 0) ? unit_to_px_y(hwnd, state, height) : 0;
  }
  if (outW <= 0) outW = bm.bmWidth;
  if (outH <= 0) outH = bm.bmHeight;
  if (outW <= 0 || outH <= 0) return 0;

  HDC picDC = CreateCompatibleDC(nullptr);
  if (!picDC) return 0;
  HGDIOBJ oldPic = SelectObject(picDC, hBitmap);

  HGDIOBJ oldSurface = nullptr;
  HDC targetDC = nullptr;
  if (state && state->autoRedraw) targetDC = acquire_canvas_surface_dc(hwnd, state, &oldSurface);
  else targetDC = GetDC(hwnd);

  if (!targetDC) {
    SelectObject(picDC, oldPic);
    DeleteDC(picDC);
    return 0;
  }

  BOOL ok = FALSE;
  if (outW == bm.bmWidth && outH == bm.bmHeight) {
    ok = BitBlt(targetDC, outX, outY, outW, outH, picDC, 0, 0, map_blit_mode(mode));
  } else {
    SetStretchBltMode(targetDC, HALFTONE);
    ok = StretchBlt(targetDC, outX, outY, outW, outH, picDC, 0, 0, bm.bmWidth, bm.bmHeight, map_blit_mode(mode));
  }

  if (state && state->autoRedraw) {
    release_canvas_surface_dc(targetDC, oldSurface);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else {
    ReleaseDC(hwnd, targetDC);
  }

  SelectObject(picDC, oldPic);
  DeleteDC(picDC);
  return ok ? 1 : 0;
}

extern "C" int krnln_drawpanel_get_pic_width(long long panelHandle, long long picHandle) {
  HBITMAP hBitmap = reinterpret_cast<HBITMAP>(static_cast<intptr_t>(picHandle));
  if (!hBitmap) return 0;
  BITMAP bm{};
  if (GetObjectW(hBitmap, sizeof(bm), &bm) != sizeof(bm) || bm.bmWidth <= 0) return 0;

  HWND hwnd = as_hwnd(panelHandle);
  if (hwnd && IsWindow(hwnd) && is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (state) return px_to_unit_x(hwnd, state, bm.bmWidth);
  }
  return bm.bmWidth;
}

extern "C" int krnln_drawpanel_get_pic_height(long long panelHandle, long long picHandle) {
  HBITMAP hBitmap = reinterpret_cast<HBITMAP>(static_cast<intptr_t>(picHandle));
  if (!hBitmap) return 0;
  BITMAP bm{};
  if (GetObjectW(hBitmap, sizeof(bm), &bm) != sizeof(bm) || bm.bmHeight <= 0) return 0;

  HWND hwnd = as_hwnd(panelHandle);
  if (hwnd && IsWindow(hwnd) && is_canvas_window(hwnd)) {
    CanvasState* state = get_canvas_state(hwnd);
    if (state) return px_to_unit_y(hwnd, state, bm.bmHeight);
  }
  return bm.bmHeight;
}
