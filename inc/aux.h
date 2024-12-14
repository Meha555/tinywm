#ifndef AUX_H
#define AUX_H

// this file offer auxiliary functions

extern "C" {
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
}

#define XCB_CHECKED_REPLY(name, description, conn, ...)                             \
    xcb_generic_error_t *_##name##_error = nullptr;                                 \
    UniqueCPtr<xcb_##name##_reply_t> name##_result(                                 \
        xcb_##name##_reply(conn, xcb_##name(conn, __VA_ARGS__), &_##name##_error)); \
    xcb::errorHandler(_##name##_error, description);

namespace x11
{
enum class KeyMap { ESC = 9 };
enum class CursorGlyph : uint16_t {
    Hand = 58,
    Arrow = 68,
};
enum class Colors : uint32_t {
    BLUE = 0x0000ff, // 蓝色
    RED = 0xff0000, // 黄色
    GREY = 0x7f7f7f, // 灰色
    GREEN = 0xa0e93a, // 绿色
};

void print_modifiers(uint32_t mask);
xcb_gcontext_t gc_font_get(xcb_connection_t *c, xcb_screen_t *screen,
                           xcb_window_t window, const char *font_name);
void text_draw(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
               int16_t x1, int16_t y1, const char *label);
void button_draw(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
                 int16_t x1, int16_t y1, const char *label);
void cursor_set(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
                CursorGlyph cursor_id);
uint32_t transRGB(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha);

} // namespace x11

#endif // AUX_H