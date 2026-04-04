#include "krnln_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

using YC_BIN = std::vector<unsigned char>;

namespace {

static wchar_t* krnln_store_text(const std::wstring& text) {
  static std::wstring slots[8];
  static int slotIndex = 0;
  slotIndex = (slotIndex + 1) % 8;
  slots[slotIndex] = text;
  return slots[slotIndex].empty() ? const_cast<wchar_t*>(L"") : slots[slotIndex].data();
}

static int krnln_math_clamp_i32(double value) {
  if (!std::isfinite(value)) return 0;
  if (value > static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
  if (value < static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
  return static_cast<int>(value);
}

static std::mt19937& krnln_math_rng() {
  static std::mt19937 rng(
    static_cast<std::mt19937::result_type>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()
    )
  );
  return rng;
}

} // namespace

extern "C" int krnln_message_box(const char* text, const char* title) {
  const char* safeText = (text && text[0]) ? text : "";
  const char* safeTitle = (title && title[0]) ? title : "krnln";
  std::fprintf(stderr, "[%s] %s\n", safeTitle, safeText);
  return 1;
}

extern "C" int krnln_text_length(const char* text) {
  if (!text) return 0;
  return static_cast<int>(std::strlen(text));
}

double krnln_math_add2(double left, double right) { return left + right; }
wchar_t* krnln_math_add2(const wchar_t* left, const wchar_t* right) {
  std::wstring out = left ? left : L"";
  out += right ? right : L"";
  return krnln_store_text(out);
}
YC_BIN krnln_math_add2(const YC_BIN& left, const YC_BIN& right) {
  YC_BIN out = left;
  out.insert(out.end(), right.begin(), right.end());
  return out;
}
double krnln_math_sub2(double left, double right) { return left - right; }
double krnln_math_mul2(double left, double right) { return left * right; }
double krnln_math_div2(double left, double right) { return left / right; }
double krnln_math_idiv2(double left, double right) { return std::trunc(left / right); }
double krnln_math_mod2(double left, double right) { return std::fmod(left, right); }
double krnln_math_neg(double value) { return -value; }
int krnln_math_sgn(double value) { return std::isfinite(value) ? (value > 0.0) - (value < 0.0) : 0; }
double krnln_math_abs(double value) { return std::fabs(value); }
int krnln_math_int(double value) { return krnln_math_clamp_i32(std::floor(value)); }
int krnln_math_fix(double value) { return krnln_math_clamp_i32(std::trunc(value)); }
double krnln_math_round(double value, int digitPos) {
  if (!std::isfinite(value)) return value;
  if (digitPos == 0) return std::round(value);
  if (digitPos > 308 || digitPos < -308) return value;
  const double factor = std::pow(10.0, static_cast<double>(digitPos > 0 ? digitPos : -digitPos));
  if (!std::isfinite(factor) || factor == 0.0) return value;
  if (digitPos > 0) return std::round(value * factor) / factor;
  return std::round(value / factor) * factor;
}
double krnln_math_pow(double value, double expValue) { return std::pow(value, expValue); }
double krnln_math_sqr(double value) { return std::sqrt(value); }
double krnln_math_sin(double value) { return std::sin(value); }
double krnln_math_cos(double value) { return std::cos(value); }
double krnln_math_tan(double value) { return std::tan(value); }
double krnln_math_atn(double value) { return std::atan(value); }
double krnln_math_log(double value) { return std::log(value); }
double krnln_math_exp(double value) { return std::exp(value); }
int krnln_math_is_calc_ok(double value) { return std::isfinite(value) ? 1 : 0; }
int krnln_math_randomize(int seed) {
  if (seed == std::numeric_limits<int>::min()) {
    seed = static_cast<int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }
  krnln_math_rng().seed(static_cast<std::mt19937::result_type>(seed));
  return 1;
}
int krnln_math_rnd(int minValue, int maxValue) {
  if (minValue < 0) minValue = 0;
  if (maxValue < 0) maxValue = 0;
  if (maxValue < minValue) std::swap(maxValue, minValue);
  std::uniform_int_distribution<int> dist(minValue, maxValue);
  return dist(krnln_math_rng());
}

extern "C" int krnln_init_window_units(void) {
  return 0;
}

extern "C" long long krnln_drawpanel_get_hdc(long long panelHandle) {
  (void)panelHandle;
  return 0;
}

extern "C" int krnln_drawpanel_clear(long long panelHandle, int left, int top, int width, int height) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)width;
  (void)height;
  return 0;
}

extern "C" int krnln_drawpanel_get_pixel(long long panelHandle, int x, int y) {
  (void)panelHandle;
  (void)x;
  (void)y;
  return -1;
}

extern "C" int krnln_drawpanel_set_pixel(long long panelHandle, int x, int y, int color) {
  (void)panelHandle;
  (void)x;
  (void)y;
  (void)color;
  return 0;
}

extern "C" int krnln_drawpanel_line(long long panelHandle, int x1, int y1, int x2, int y2, int color) {
  (void)panelHandle;
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
  (void)color;
  return 0;
}

extern "C" int krnln_drawpanel_set_pic(long long panelHandle, const unsigned char* data, int len) {
  (void)panelHandle;
  (void)data;
  (void)len;
  return 0;
}

extern "C" int krnln_drawpanel_draw_rect(long long panelHandle, int left, int top, int right, int bottom) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  return 0;
}

extern "C" int krnln_drawpanel_fill_rect(long long panelHandle, int left, int top, int right, int bottom) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  return 0;
}

extern "C" int krnln_drawpanel_draw_ellipse(long long panelHandle, int left, int top, int right, int bottom) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  return 0;
}

extern "C" int krnln_drawpanel_draw_round_rect(long long panelHandle, int left, int top, int right, int bottom, int arcWidth, int arcHeight) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  (void)arcWidth;
  (void)arcHeight;
  return 0;
}

extern "C" int krnln_drawpanel_invert_rect(long long panelHandle, int left, int top, int right, int bottom) {
  (void)panelHandle;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  return 0;
}

extern "C" int krnln_drawpanel_arc(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  (void)panelHandle; (void)left; (void)top; (void)right; (void)bottom;
  (void)startX; (void)startY; (void)endX; (void)endY;
  return 0;
}

extern "C" int krnln_drawpanel_chord(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  (void)panelHandle; (void)left; (void)top; (void)right; (void)bottom;
  (void)startX; (void)startY; (void)endX; (void)endY;
  return 0;
}

extern "C" int krnln_drawpanel_pie(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY) {
  (void)panelHandle; (void)left; (void)top; (void)right; (void)bottom;
  (void)startX; (void)startY; (void)endX; (void)endY;
  return 0;
}

extern "C" int krnln_drawpanel_unit_cnv(long long panelHandle, int value, int valueType) {
  (void)panelHandle;
  (void)valueType;
  return value;
}

extern "C" int krnln_drawpanel_set_write_pos(long long panelHandle, int x, int y) {
  (void)panelHandle;
  (void)x;
  (void)y;
  return 0;
}

extern "C" int krnln_drawpanel_write(long long panelHandle, const wchar_t* text) {
  (void)panelHandle;
  (void)text;
  return 0;
}

extern "C" int krnln_drawpanel_print(long long panelHandle, const wchar_t* text) {
  (void)panelHandle;
  (void)text;
  return 0;
}

extern "C" int krnln_drawpanel_say(long long panelHandle, int x, int y, const wchar_t* text) {
  (void)panelHandle;
  (void)x;
  (void)y;
  (void)text;
  return 0;
}

extern "C" int krnln_drawpanel_get_text_width(long long panelHandle, const wchar_t* text) {
  (void)panelHandle;
  (void)text;
  return 0;
}

extern "C" int krnln_drawpanel_get_text_height(long long panelHandle, const wchar_t* text) {
  (void)panelHandle;
  (void)text;
  return 0;
}

extern "C" int krnln_drawpanel_draw_jb_rect(long long panelHandle, int left, int top, int width, int height, int direction, int color1, int color2) {
  (void)panelHandle; (void)left; (void)top; (void)width; (void)height;
  (void)direction; (void)color1; (void)color2;
  return 0;
}

extern "C" int krnln_drawpanel_copy(long long srcPanelHandle, int left, int top, int width, int height, long long dstPanelHandle, int dstLeft, int dstTop, int mode) {
  (void)srcPanelHandle; (void)left; (void)top; (void)width; (void)height;
  (void)dstPanelHandle; (void)dstLeft; (void)dstTop; (void)mode;
  return 0;
}

extern "C" int krnln_drawpanel_draw_pic(long long panelHandle, long long picHandle, int x, int y, int width, int height, int mode) {
  (void)panelHandle; (void)picHandle; (void)x; (void)y; (void)width; (void)height; (void)mode;
  return 0;
}

extern "C" int krnln_drawpanel_get_pic_width(long long panelHandle, long long picHandle) {
  (void)panelHandle; (void)picHandle;
  return 0;
}

extern "C" int krnln_drawpanel_get_pic_height(long long panelHandle, long long picHandle) {
  (void)panelHandle; (void)picHandle;
  return 0;
}
