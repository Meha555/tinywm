#include "winm.h"
#include <algorithm>
#include <cstring>
#include <glog/logging.h>
#include <xcb/bigreq.h>
#include <xcb/xc_misc.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xproto.h>

static void print_modifiers(uint32_t mask) {
    const char **mod,
        *mods[] = {"Shift",   "Lock",    "Ctrl",   "Alt",     "Mod2",
                   "Mod3",    "Mod4",    "Mod5",   "Button1", "Button2",
                   "Button3", "Button4", "Button5"};
    printf("Modifier mask: ");
    for (mod = mods; mask; mask >>= 1, mod++)
        if (mask & 1)
            printf(*mod);
    putchar('\n');
}

namespace x11 {
::std::unique_ptr<WindowManager>
WindowManager::getInstance(const ::std::string &display_name) {
    const char *display_c_str =
        display_name.empty() ? nullptr : display_name.c_str();
    xcb_connection_t *c = xcb_connect(display_c_str, NULL);
    if (c == nullptr || xcb_connection_has_error(c) != 0) {
        LOG(ERROR) << "Failed to open X connection ";
        return nullptr;
    }
    xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

    return ::std::unique_ptr<WindowManager>(new WindowManager(c, s));
}
WindowManager::WindowManager(xcb_connection_t *c, const xcb_screen_t *s)
    : conn(c), root(s->root) {}

WindowManager::~WindowManager() { xcb_disconnect(conn); }

void WindowManager::run() {
    wm_detected_.store(false);
    // TODO - 注册错误处理函数(xcb好像没有这个功能)
    const static uint32_t values[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                                      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};
    xcb_change_window_attributes(conn, root, XCB_CW_EVENT_MASK, values);
    xcb_flush(conn);
    if (wm_detected_.load()) {
        LOG(ERROR) << "Detected another window manager on connection";
        return;
    }
    // TODO - 注册错误处理函数(xcb好像没有这个功能)

    xcb_void_cookie_t cookie = xcb_grab_server_checked(conn);
    xcb_generic_error_t *error = xcb_request_check(conn, cookie);
    if (error) {
        fprintf(stderr, "ERROR: can't grab X Server : %d\n", error->error_code);
        xcb_disconnect(conn);
        free(error);
        exit(EXIT_FAILURE);
    }

    xcb_generic_error_t *error;
    xcb_query_tree_reply_t *result =
        xcb_query_tree_reply(conn, xcb_query_tree(conn, root), &error);
    if (error) {
        fprintf(stderr, "ERROR: can't query for window tree : %d\n",
                error->error_code);
        xcb_disconnect(conn);
        free(error);
        exit(EXIT_FAILURE);
    }
    CHECK_EQ(result->root, root);
    for (uint i = 0; i < result->children_len; ++i) {
        addFrame(xcb_query_tree_children(result), true);
    }
    free(result);

    xcb_void_cookie_t cookie = xcb_ungrab_server_checked(conn);
    xcb_generic_error_t *error = xcb_request_check(conn, cookie);
    if (error) {
        fprintf(stderr, "ERROR: can't ungrab X Server : %d\n",
                error->error_code);
        xcb_disconnect(conn);
        free(error);
        exit(EXIT_FAILURE);
    }
    xcb_generic_event_t *event;
    while ((event = xcb_wait_for_event(conn))) {
        switch (event->response_type & ~0x80) {
        case XCB_EXPOSE: {
            xcb_expose_event_t *ev = (xcb_expose_event_t *)event;

            printf("Window %u exposed. Region to be redrawn at location "
                   "(%d,%d), with dimension (%d,%d)\n",
                   ev->window, ev->x, ev->y, ev->width, ev->height);
            break;
        }
        case XCB_BUTTON_PRESS: {
            xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;
            print_modifiers(ev->state);

            switch (ev->detail) {
            case 4:
                printf("Wheel Button up in window %u, at coordinates (%d,%d)\n",
                       ev->event, ev->event_x, ev->event_y);
                break;
            case 5:
                printf(
                    "Wheel Button down in window %u, at coordinates (%d,%d)\n",
                    ev->event, ev->event_x, ev->event_y);
                break;
            default:
                printf(
                    "Button %d pressed in window %u, at coordinates (%d,%d)\n",
                    ev->detail, ev->event, ev->event_x, ev->event_y);
            }
            break;
        }
        case XCB_BUTTON_RELEASE: {
            xcb_button_release_event_t *ev = (xcb_button_release_event_t *)event;
            print_modifiers(ev->state);

            printf("Button %d released in window %u, at coordinates (%d,%d)\n",
                   ev->detail, ev->event, ev->event_x, ev->event_y);
            break;
        }
        case XCB_KEY_PRESS: {
            xcb_key_press_event_t *ev = (xcb_key_press_event_t *)event;
            print_modifiers(ev->state);

            printf("Key pressed in window %u\n", ev->event);
            break;
        }
        case XCB_KEY_RELEASE: {
            xcb_key_release_event_t *ev = (xcb_key_release_event_t *)event;
            print_modifiers(ev->state);

            printf("Key released in window %u\n", ev->event);
            break;
        }
        case XCB_MOTION_NOTIFY: {
            xcb_motion_notify_event_t *ev = (xcb_motion_notify_event_t *)event;

            printf("Mouse moved in window %u, at coordinates (%d,%d)\n",
                   ev->event, ev->event_x, ev->event_y);
            break;
        }
        case XCB_ENTER_NOTIFY: {
            xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t *)event;

            printf("Mouse entered window %u, at coordinates (%d,%d)\n",
                   ev->event, ev->event_x, ev->event_y);
            break;
        }
        case XCB_LEAVE_NOTIFY: {
            xcb_leave_notify_event_t *ev = (xcb_leave_notify_event_t *)event;

            printf("Mouse left window %u, at coordinates (%d,%d)\n", ev->event,
                   ev->event_x, ev->event_y);
            break;
        }
        default:
            printf("Unknown event: %d\n", event->response_type);
            break;
        }
        free(event);
    }
}

} // namespace x11
