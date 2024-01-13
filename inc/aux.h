#ifndef AUX_H
#define AUX_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <xcb/xcb.h>
}

namespace x11 {
enum class KeyMap { ESC = 9 };
enum class Colors : unsigned long {
  BORDER_COLOR = 0x0000ff,  // 蓝色
  BG_COLOR = 0xff0000,      // 黄色
};

void print_modifiers(uint32_t mask);
xcb_gcontext_t gc_font_get(xcb_connection_t *c, xcb_screen_t *screen,
                           xcb_window_t window, const char *font_name);
void text_draw(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
               int16_t x1, int16_t y1, const char *label);

}  // namespace x11

#endif  // AUX_H