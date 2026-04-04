#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int krnln_message_box(const char* text, const char* title);
int krnln_text_length(const char* text);
int krnln_init_window_units(void);
const wchar_t* krnln_control_get_text(long long controlHandle);
int krnln_control_set_text(long long controlHandle, const wchar_t* text);
intptr_t krnln_control_get_prop_num(long long controlHandle, int propIndex);
const wchar_t* krnln_control_get_prop_text(long long controlHandle, int propIndex);
int krnln_control_set_prop_num(long long controlHandle, int propIndex, intptr_t value);
int krnln_control_set_prop_text(long long controlHandle, int propIndex, const wchar_t* value);
int krnln_control_set_prop_bin(long long controlHandle, int propIndex, const void* value);
long long krnln_drawpanel_get_hdc(long long panelHandle);
int krnln_drawpanel_clear(long long panelHandle, int left, int top, int width, int height);
int krnln_drawpanel_get_pixel(long long panelHandle, int x, int y);
int krnln_drawpanel_set_pixel(long long panelHandle, int x, int y, int color);
int krnln_drawpanel_line(long long panelHandle, int x1, int y1, int x2, int y2, int color);
int krnln_drawpanel_set_pic(long long panelHandle, const unsigned char* data, int len);
int krnln_drawpanel_draw_rect(long long panelHandle, int left, int top, int right, int bottom);
int krnln_drawpanel_fill_rect(long long panelHandle, int left, int top, int right, int bottom);
int krnln_drawpanel_draw_ellipse(long long panelHandle, int left, int top, int right, int bottom);
int krnln_drawpanel_draw_round_rect(long long panelHandle, int left, int top, int right, int bottom, int arcWidth, int arcHeight);
int krnln_drawpanel_invert_rect(long long panelHandle, int left, int top, int right, int bottom);
int krnln_drawpanel_arc(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY);
int krnln_drawpanel_chord(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY);
int krnln_drawpanel_pie(long long panelHandle, int left, int top, int right, int bottom, int startX, int startY, int endX, int endY);
int krnln_drawpanel_unit_cnv(long long panelHandle, int value, int valueType);
int krnln_drawpanel_set_write_pos(long long panelHandle, int x, int y);
int krnln_drawpanel_write(long long panelHandle, const wchar_t* text);
int krnln_drawpanel_print(long long panelHandle, const wchar_t* text);
int krnln_drawpanel_say(long long panelHandle, int x, int y, const wchar_t* text);
int krnln_drawpanel_get_text_width(long long panelHandle, const wchar_t* text);
int krnln_drawpanel_get_text_height(long long panelHandle, const wchar_t* text);
int krnln_drawpanel_draw_jb_rect(long long panelHandle, int left, int top, int width, int height, int direction, int color1, int color2);
int krnln_drawpanel_copy(long long srcPanelHandle, int left, int top, int width, int height, long long dstPanelHandle, int dstLeft, int dstTop, int mode);
int krnln_drawpanel_draw_pic(long long panelHandle, long long picHandle, int x, int y, int width, int height, int mode);
int krnln_drawpanel_get_pic_width(long long panelHandle, long long picHandle);
int krnln_drawpanel_get_pic_height(long long panelHandle, long long picHandle);

#ifdef __cplusplus
}
#endif
