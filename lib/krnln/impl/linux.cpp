#include "krnln_api.h"

#include <cstring>
#include <cstdio>

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
