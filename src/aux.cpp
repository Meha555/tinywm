#include "aux.h"

#include <xcb/xproto.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace x11
{

void print_modifiers(uint32_t mask)
{
    const char **mod,
        *mods[] = {"Shift", "Lock", "Ctrl", "Alt", "Mod2",
                   "Mod3", "Mod4", "Mod5", "Button1", "Button2",
                   "Button3", "Button4", "Button5"};
    printf("Modifier mask: ");
    for (mod = mods; mask; mask >>= 1, mod++)
        if (mask & 1)
            printf(*mod);
    putchar('\n');
}

xcb_gcontext_t gc_font_get(xcb_connection_t *c, xcb_screen_t *screen,
                           xcb_window_t window, const char *font_name)
{
    uint32_t value_list[3];
    xcb_void_cookie_t cookie_font;
    xcb_void_cookie_t cookie_gc;
    xcb_generic_error_t *error;
    xcb_font_t font;
    xcb_gcontext_t gc;
    uint32_t mask;

    font = xcb_generate_id(c);
    cookie_font = xcb_open_font_checked(c, font, strlen(font_name), font_name);

    error = xcb_request_check(c, cookie_font);
    if (error) {
        fprintf(stderr, "ERROR: can't open font : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    gc = xcb_generate_id(c);
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
    value_list[0] = screen->black_pixel;
    value_list[1] = screen->white_pixel;
    value_list[2] = font;
    cookie_gc = xcb_create_gc_checked(c, gc, window, mask, value_list);
    error = xcb_request_check(c, cookie_gc);
    if (error) {
        fprintf(stderr, "ERROR: can't create gc : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    cookie_font = xcb_close_font_checked(c, font);
    error = xcb_request_check(c, cookie_font);
    if (error) {
        fprintf(stderr, "ERROR: can't close font : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    return gc;
}

void text_draw(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
               int16_t x1, int16_t y1, const char *label)
{
    xcb_void_cookie_t cookie_gc;
    xcb_void_cookie_t cookie_text;
    xcb_generic_error_t *error;
    xcb_gcontext_t gc;
    uint8_t length;

    length = strlen(label);

    gc = gc_font_get(c, screen, window, "7x13");

    cookie_text = xcb_image_text_8_checked(c, length, window, gc, x1, y1, label);
    error = xcb_request_check(c, cookie_text);
    if (error) {
        fprintf(stderr, "ERROR: can't paste text : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    cookie_gc = xcb_free_gc(c, gc);
    error = xcb_request_check(c, cookie_gc);
    if (error) {
        fprintf(stderr, "ERROR: can't free gc : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }
}
void button_draw(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window,
                 int16_t x1, int16_t y1, const char *label)
{
    xcb_point_t points[5];
    xcb_void_cookie_t cookie_gc;
    xcb_void_cookie_t cookie_line;
    xcb_void_cookie_t cookie_text;
    xcb_generic_error_t *error;
    xcb_gcontext_t gc;
    int16_t width;
    int16_t height;
    uint8_t length;
    int16_t inset;

    length = strlen(label);
    inset = 2;

    gc = gc_font_get(c, screen, window, "7x13");

    width = 7 * length + 2 * (inset + 1);
    height = 13 + 2 * (inset + 1);
    points[0].x = x1;
    points[0].y = y1;
    points[1].x = x1 + width;
    points[1].y = y1;
    points[2].x = x1 + width;
    points[2].y = y1 - height;
    points[3].x = x1;
    points[3].y = y1 - height;
    points[4].x = x1;
    points[4].y = y1;
    cookie_line =
        xcb_poly_line_checked(c, XCB_COORD_MODE_ORIGIN, window, gc, 5, points);

    error = xcb_request_check(c, cookie_line);
    if (error) {
        fprintf(stderr, "ERROR: can't draw lines : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    cookie_text = xcb_image_text_8_checked(c, length, window, gc, x1 + inset + 1,
                                           y1 - inset - 1, label);
    error = xcb_request_check(c, cookie_text);
    if (error) {
        fprintf(stderr, "ERROR: can't paste text : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    cookie_gc = xcb_free_gc(c, gc);
    error = xcb_request_check(c, cookie_gc);
    if (error) {
        fprintf(stderr, "ERROR: can't free gc : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }
}
void cursor_set(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window, CursorGlyph cursor_id)
{
    uint32_t values_list[3];
    xcb_void_cookie_t cookie_font;
    xcb_void_cookie_t cookie_gc;
    xcb_generic_error_t *error;
    xcb_font_t font;
    xcb_cursor_t cursor;
    xcb_gcontext_t gc;
    uint32_t mask;
    uint32_t value_list;

    font = xcb_generate_id(c);
    cookie_font = xcb_open_font_checked(c, font, strlen("cursor"), "cursor");
    error = xcb_request_check(c, cookie_font);
    if (error) {
        fprintf(stderr, "ERROR: can't open font : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    cursor = xcb_generate_id(c);
    xcb_create_glyph_cursor(c, cursor, font, font, static_cast<uint16_t>(cursor_id), static_cast<uint16_t>(cursor_id) + 1, 0, 0,
                            0, 0, 0, 0);

    gc = xcb_generate_id(c);
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
    values_list[0] = screen->black_pixel;
    values_list[1] = screen->white_pixel;
    values_list[2] = font;
    cookie_gc = xcb_create_gc_checked(c, gc, window, mask, values_list);
    error = xcb_request_check(c, cookie_gc);
    if (error) {
        fprintf(stderr, "ERROR: can't create gc : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }

    mask = XCB_CW_CURSOR;
    value_list = cursor;
    xcb_change_window_attributes(c, window, mask, &value_list);

    xcb_free_cursor(c, cursor);

    cookie_font = xcb_close_font_checked(c, font);
    error = xcb_request_check(c, cookie_font);
    if (error) {
        fprintf(stderr, "ERROR: can't close font : %d\n", error->error_code);
        xcb_disconnect(c);
        exit(EXIT_FAILURE);
    }
}

uint32_t transRGB(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
    return blue | (green << 8) | (blue < 16) | (alpha < 24);
}

} // namespace x11