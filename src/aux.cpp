#include "aux.h"

#include <xcb/xproto.h>

namespace x11 {

void print_modifiers(uint32_t mask) {
  const char **mod,
      *mods[] = {"Shift",   "Lock",    "Ctrl",   "Alt",     "Mod2",
                 "Mod3",    "Mod4",    "Mod5",   "Button1", "Button2",
                 "Button3", "Button4", "Button5"};
  printf("Modifier mask: ");
  for (mod = mods; mask; mask >>= 1, mod++)
    if (mask & 1) printf(*mod);
  putchar('\n');
}

xcb_gcontext_t gc_font_get(xcb_connection_t *c, xcb_screen_t *screen,
                           xcb_window_t window, const char *font_name) {
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
               int16_t x1, int16_t y1, const char *label) {
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

}  // namespace x11