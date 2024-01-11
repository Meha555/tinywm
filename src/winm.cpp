#include "winm.h"
#include <algorithm>
#include <cstring>
#include <glog/logging.h>
#include <xcb/bigreq.h>
#include <xcb/xc_misc.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xproto.h>

using namespace utils;

enum class KeyMap { ESC = 9 };
enum class Colors : unsigned long {
    BORDER_COLOR = 0xff0000, // 黄色
    BG_COLOR = 0x0000ff,     // 蓝色
};

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

::std::atomic<bool> WindowManager::wm_detected_;
::std::mutex WindowManager::wm_mutex_;
WindowManager *WindowManager::instance_ = nullptr;

::std::unique_ptr<WindowManager>
WindowManager::getInstance(const ::std::string &display_name) {
    if (instance_ == nullptr) {
        ::std::lock_guard<::std::mutex> guard(wm_mutex_);
        if (instance_ == nullptr) {
            const char *display_c_str =
                display_name.empty() ? nullptr : display_name.c_str();
            xcb_connection_t *c = xcb_connect(display_c_str, NULL);
            if (c == nullptr || xcb_connection_has_error(c) != 0) {
                LOG(ERROR) << "Failed to open X connection ";
                return nullptr;
            }
            xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
            instance_ = new WindowManager(c, s);
        }
    }
    return ::std::unique_ptr<WindowManager>(instance_);
}

WindowManager::WindowManager(xcb_connection_t *c, xcb_screen_t *s)
    : conn(c), screen(s), root(s->root) {}

WindowManager::~WindowManager() { xcb_disconnect(conn); }

void WindowManager::run() {
    // REVIEW - 为什么需要加锁？？
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
    // TODO -
    // 注册错误处理函数(xcb好像没有这个功能)，貌似都是在checked后缀的函数对应的xcb_request_check处理了

    errorHandler(xcb_grab_server_checked(conn), "grab X Server");
    
    xcb_generic_error_t *error = nullptr;
    xcb_query_tree_reply_t *result_tree =
        xcb_query_tree_reply(conn, xcb_query_tree(conn, root), &error);
    errorHandler(error, "query for window tree");

    CHECK_EQ(result_tree->root, root);
    for (uint i = 0; i < result_tree->children_len; ++i) {
        addFrame(*xcb_query_tree_children(result_tree), true);
    }
    free(result_tree);

    errorHandler(xcb_ungrab_server_checked(conn), "ungrab X Server");

    xcb_generic_event_t *event;
    while ((event = xcb_wait_for_event(conn))) {
        switch (event->response_type & ~0x80) {
        case XCB_CREATE_NOTIFY: {
            onCreateNotify((xcb_create_notify_event_t *)event);
            break;
        }
        case XCB_DESTROY_NOTIFY: {
            onDestroyNotify((xcb_destroy_notify_event_t *)event);
            break;
        }
        case XCB_REPARENT_NOTIFY: {
            onReparentNotify((xcb_reparent_notify_event_t *)event);
            break;
        }
        case XCB_MAP_NOTIFY: {
            onMapNotify((xcb_map_notify_event_t *)event);
            break;
        }
        case XCB_UNMAP_NOTIFY: {
            onUnmapNotify((xcb_unmap_notify_event_t *)event);
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            onConfigureNotify((xcb_configure_notify_event_t *)event);
            break;
        }
        case XCB_EXPOSE: {
            onExpose((xcb_expose_event_t *)event);
            break;
        }
        case XCB_MAP_REQUEST: {
            onMapRequest((xcb_map_request_event_t *)event);
            break;
        }
        case XCB_CONFIGURE_REQUEST: {
            onConfigureRequest((xcb_configure_request_event_t *)event);
            break;
        }
        case XCB_ENTER_NOTIFY: {
            onEnterNotify((xcb_enter_notify_event_t *)event);
            break;
        }
        case XCB_LEAVE_NOTIFY: {
            onLeaveNotify((xcb_leave_notify_event_t *)event);
            break;
        }
        case XCB_FOCUS_IN: {
            onFocusIn((xcb_focus_in_event_t *)event);
            break;
        }
        case XCB_FOCUS_OUT: {
            onFocusOut((xcb_focus_out_event_t *)event);
            break;
        }
        case XCB_BUTTON_PRESS: {
            onButtonPress((xcb_button_press_event_t *)event);
            break;
        }
        case XCB_BUTTON_RELEASE: {
            onButtonRelease((xcb_button_release_event_t *)event);
            break;
        }
        case XCB_KEY_PRESS: {
            onKeyPress((xcb_key_press_event_t *)event);
            break;
        }
        case XCB_KEY_RELEASE: {
            onKeyRelease((xcb_key_release_event_t *)event);
            break;
        }
        case XCB_MOTION_NOTIFY: {
            onMotionNotify((xcb_motion_notify_event_t *)event);
            break;
        }
        default:
            printf("Unknown event: %d\n", event->response_type);
            break;
        }
        free(event);
    }
}

void WindowManager::addFrame(xcb_window_t w, bool created_before) {
    // REVIEW - 为什么要grab server和输入设备
    const unsigned int BORDER_WIDTH = 5;

    // Forbid multiple frame.
    CHECK(!clients.count(w));

    xcb_generic_error_t *error = nullptr;
    xcb_get_window_attributes_reply_t *result_attr =
        xcb_get_window_attributes_reply(
            conn, xcb_get_window_attributes(conn, w), &error);

    // Make sure the window is managed by WM, and it must be currently visiable.
    if (created_before &&
        (result_attr->override_redirect == XCB_CW_OVERRIDE_REDIRECT ||
         result_attr->map_state != XCB_MAP_STATE_VIEWABLE)) {
        return;
    }
    free(result_attr);
    // 1. Get the geometry of client window, so we can use it to create frame
    xcb_get_geometry_reply_t *result_geo =
        xcb_get_geometry_reply(conn, xcb_get_geometry(conn, w), &error);
    // 2. Create a frame.
    xcb_window_t frame = xcb_generate_id(conn);
    uint32_t mask;
    uint32_t values[2];
    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = static_cast<uint32_t>(Colors::BG_COLOR); // screen->black_pixel
    values[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE;
    errorHandler(xcb_create_window_checked(
                     conn, result_geo->depth, frame, root, result_geo->x,
                     result_geo->y, result_geo->width, result_geo->height,
                     BORDER_WIDTH, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                     XCB_COPY_FROM_PARENT, mask, values),
                 "create frame");
    // 3. Add client window to save set.
    errorHandler(xcb_change_save_set_checked(conn, XCB_SET_MODE_INSERT, w),
                 "add client window to save set");
    // 4. Reparent client window with frame window.
    errorHandler(xcb_reparent_window_checked(conn, w, frame, 0, 0),
                 "reparent client window with frame window");
    // 5. Map frame. //REVIEW -
    // 映射一个窗口的时候，其子窗口也会一起被映射吗？其父窗口呢？
    errorHandler(xcb_map_window_checked(conn, frame),
                 "map frame and client window");
    clients[w] = frame;
    // 6. Grab universal window management actions on client window.
    // REVIEW - 回顾一下grab，应该是让按住一个键的时候事件被这个窗口独占
    // 6.1 Move windows with alt + left button.
    errorHandler(xcb_grab_button_checked(conn, 0, w, 0, XCB_GRAB_MODE_ASYNC,
                                         XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                                         XCB_BUTTON_INDEX_1, XCB_MOD_MASK_1),
                 "grab button1");
    // 6.2  Resize windows with alt + right button.
    errorHandler(xcb_grab_button_checked(conn, 0, w, 0, XCB_GRAB_MODE_ASYNC,
                                         XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                                         XCB_BUTTON_INDEX_3, XCB_MOD_MASK_3),
                 "grab button3");
    // 6.3 Kill windows with alt + f4.
    // 不会xcb的键盘
    // errorHandler(xcb_ungrab_key_checked(conn, xcb_keycode_), "grab key");
    // 6.4 Switch windows with alt + tab.
    LOG(INFO) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::unFrame(xcb_window_t w) {
    CHECK(clients.count(w));
    // 1. Unmap frame.
    errorHandler(xcb_unmap_window_checked(conn, clients[w]), "unmap frame");
    // 2. Reparent client window.
    errorHandler(xcb_reparent_window_checked(conn, w, root, 0, 0),
                 "reparent client window");
    // 3. Remove client windom from save set.
    errorHandler(xcb_change_save_set_checked(conn, XCB_SET_MODE_DELETE, w),
                 "remove client window from save set");
    // 4. Destroy frame.
    errorHandler(xcb_destroy_window_checked(conn, clients[w]), "destroy frame");
    clients.erase(w);
    LOG(INFO) << "Unframed window " << w << " [" << clients[w] << "]";
}

void WindowManager::onCreateNotify(xcb_create_notify_event_t *ev) {}

void WindowManager::onDestroyNotify(xcb_destroy_notify_event_t *ev) {}

void WindowManager::onConfigureNotify(xcb_configure_notify_event_t *ev) {}

void WindowManager::onMapNotify(xcb_map_notify_event_t *ev) {}

void WindowManager::onUnmapNotify(xcb_unmap_notify_event_t *ev) {}

void WindowManager::onReparentNotify(xcb_reparent_notify_event_t *ev) {}

void WindowManager::onExpose(xcb_expose_event_t *ev) {
    printf("Window %u exposed. Region to be redrawn at location "
           "(%d,%d), with dimension (%d,%d)\n",
           ev->window, ev->x, ev->y, ev->width, ev->height);
}

void WindowManager::onConfigureRequest(xcb_configure_request_event_t *ev) {
    printf("Captured Configure request from window %u!\n", ev->window);

    // If client want to configure, sure it will be fine.
    // But we need to configure its frame first.
    const uint32_t values[] = {
        ev->x,       ev->y,         ev->width, ev->height, ev->border_width,
        ev->sibling, ev->stack_mode};

    if (clients.count(ev->window)) {
        errorHandler(xcb_configure_window_checked(conn, clients[ev->window],
                                                  ev->value_mask, values),
                     "configure window");
        LOG(INFO) << "Resize Frame [" << clients[ev->window] << "] to "
                  << Size<uint16_t>(ev->width, ev->height);
    }
    errorHandler(
        xcb_configure_window_checked(conn, ev->window, ev->value_mask, values),
        "configure window");
    LOG(INFO) << "Resize Window [" << clients[ev->window] << "] to "
              << Size<uint16_t>(ev->width, ev->height);
}

void WindowManager::onMapRequest(xcb_map_request_event_t *ev) {
    printf("Captured Map request from window %u!\n", ev->window);
    // If client want to map, sure it will be fine.
    // And we must frame and reparent it first.
    addFrame(ev->window, false);
    xcb_void_cookie_t cookie_map = xcb_map_window_checked(conn, ev->window);
}

void WindowManager::onResizeRequest(xcb_resize_request_event_t *ev) {
    // TODO - 这个resize是configure的子集吗？
    printf("Captured Resize request from window %u!\n", ev->window);
}

void WindowManager::onFocusIn(xcb_focus_in_event_t *ev) {
    printf("Captured FocusIn from window %u!\n", ev->event);
}

void WindowManager::onFocusOut(xcb_focus_out_event_t *ev) {
    printf("Captured FocusOut from window %u!\n", ev->event);
}

void WindowManager::onButtonPress(xcb_button_press_event_t *ev) {
    print_modifiers(ev->state);
    switch (ev->detail) {
    case 4:
        printf("Wheel Button up in window %u, at coordinates (%d,%d)\n",
               ev->event, ev->event_x, ev->event_y);
        break;
    case 5:
        printf("Wheel Button down in window %u, at coordinates (%d,%d)\n",
               ev->event, ev->event_x, ev->event_y);
        break;
    default:
        printf("Button %d pressed in window %u, at coordinates (%d,%d)\n",
               ev->detail, ev->event, ev->event_x, ev->event_y);
    }

    // We need supervise the button(mice click) status for the provision of
    // motion in case.
    CHECK(clients.count(ev->event));
    // 1. Store current window position and geometry.
    // NOTE - The coordinates must be global!
    drag_start_frame_pos_ = Position<int16_t>(ev->event_x, ev->event_y);
    xcb_generic_error_t *error = nullptr;
    xcb_get_geometry_cookie_t cookie_geo = xcb_get_geometry(conn, ev->event);
    xcb_get_geometry_reply_t *result_geo =
        xcb_get_geometry_reply(conn, cookie_geo, &error);
    errorHandler(error, "get window geometry");

    // Query for its parent window.
    xcb_query_tree_reply_t *result_tree =
        xcb_query_tree_reply(conn, xcb_query_tree(conn, ev->event), &error);
    xcb_translate_coordinates_reply_t *result_trans =
        xcb_translate_coordinates_reply(
            conn,
            xcb_translate_coordinates(conn, ev->event, result_tree->parent,
                                      result_geo->x, result_geo->y),
            &error);
    errorHandler(error, "query for parent tree");
    drag_start_frame_pos_ =
        Position<int16_t>(result_trans->dst_x, result_trans->dst_y);
    drag_start_frame_size_ =
        Size<int16_t>(result_geo->width, result_geo->height);
    free(result_geo);
    free(result_tree);
    free(result_trans);
    // 2. Raise clicked window to top.
    const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
    errorHandler(xcb_configure_window_checked(
                     conn, ev->event, XCB_CONFIG_WINDOW_STACK_MODE, values),
                 "raise to top");
}

void WindowManager::onButtonRelease(xcb_button_release_event_t *ev) {
    print_modifiers(ev->state);
    printf("Button %d released in window %u, at coordinates (%d,%d)\n",
           ev->detail, ev->event, ev->event_x, ev->event_y);
}

void WindowManager::onKeyRelease(xcb_key_release_event_t *ev) {
    print_modifiers(ev->state);
    printf("Key released in window %u\n", ev->event);
}

void WindowManager::onMotionNotify(xcb_motion_notify_event_t *ev) {
    printf("Mouse moved in window %u, at coordinates (%d,%d)\n", ev->event,
           ev->event_x, ev->event_y);

    if (ev->detail == static_cast<uint16_t>(KeyMap::ESC)) {
        free(ev);
        xcb_disconnect(conn);
    } else {
        // Now we can check the window position to see if it has moved.
        CHECK(clients.count(ev->event));
        // 1. Move the frame first.
        // REVIEW
        // -问题1：frame本身也是一个窗口，如果我在移动的时候点击的是frame窗口，
        // 那么这个MotionNotify事件就应该是frame窗口发出的，但是basic_wm中只是针对client窗口的
        // 问题2：移动一个窗口，其子窗口也会一起移动吗？
        xcb_generic_error_t *error;
        xcb_query_tree_reply_t *result_tree =
            xcb_query_tree_reply(conn, xcb_query_tree(conn, ev->event), &error);
        // 2. Move the window to destination.
        const Position<int16_t> drag_pos(ev->root_x, ev->root_y);
        const Vector2D<int16_t> delta = drag_pos - drag_start_pos_;
        // 3. Check the pressed keys.
        switch (ev->detail) {
            // alt + left button: Move window.
        case XCB_KEY_BUT_MASK_BUTTON_1: {
            // Move the frame, so its children will follow it.
            const Position<int16_t> dest_frame_pos =
                drag_start_frame_pos_ + delta;
            const static uint32_t values[] = {dest_frame_pos.x,
                                              dest_frame_pos.y};
            errorHandler(xcb_configure_window_checked(conn, clients[ev->event],
                                                      ev->state, values),
                         "move window");
            break;
        } // alt + right button: Resize window.
        case XCB_KEY_BUT_MASK_BUTTON_3: {
            auto cmp = [](int16_t a, int16_t b) -> int16_t {
                return a > b ? a : b;
            };
            const Vector2D<int16_t> size_delta(
                // ::std::max(delta.x, -drag_start_frame_size_.width),
                // ::std::max(delta.y, -drag_start_frame_size_.height)
                cmp(delta.x, -drag_start_frame_size_.width),
                cmp(delta.y, -drag_start_frame_size_.height));
            const Size<int16_t> dest_frame_size =
                drag_start_frame_size_ + size_delta;
            // Resize frame.
            const uint32_t values[] = {dest_frame_size.width,
                                       dest_frame_size.height};
            errorHandler(
                xcb_configure_window_checked(conn, clients[ev->event],
                                             XCB_CONFIG_WINDOW_BORDER_WIDTH |
                                                 XCB_CONFIG_WINDOW_HEIGHT,
                                             values),
                "resize window");
            // Resize client.
            errorHandler(
                xcb_configure_window_checked(conn, ev->event,
                                             XCB_CONFIG_WINDOW_BORDER_WIDTH |
                                                 XCB_CONFIG_WINDOW_HEIGHT,
                                             values),
                "resize window");
            break;
        }
        default:
            break;
        }
    }
}

void WindowManager::onEnterNotify(xcb_enter_notify_event_t *ev) {
    printf("Mouse entered window %u, at coordinates (%d,%d)\n", ev->event,
           ev->event_x, ev->event_y);
}

void WindowManager::onLeaveNotify(xcb_leave_notify_event_t *ev) {
    printf("Mouse left window %u, at coordinates (%d,%d)\n", ev->event,
           ev->event_x, ev->event_y);
}

void WindowManager::onKeyPress(xcb_key_press_event_t *ev) {
    printf("Key pressed in window %u, ", ev->event);
    print_modifiers(ev->state);

    // alt + f4: Close window.
    // if((ev->detail & XCB_MOD_MASK_1) && (ev->detail))
    // TODO - 不会用XCB获取键盘按键，官方文档没写，代码注释也没有

    // TODO - 实现关闭窗口的逻辑
}

void WindowManager::errorHandler(xcb_generic_error_t *error,
                                 const char *message) const noexcept {
    if (error) {
        // fprintf(stderr, "ERROR: can't %s : %d\n", message,
        // error->error_code);
        LOG(ERROR) << "can't " << message << " : error-code "
                   << error->error_code;
        xcb_disconnect(conn);
        free(error);
        exit(EXIT_FAILURE);
    }
}

void WindowManager::errorHandler(xcb_void_cookie_t cookie,
                                 const char *message) const noexcept {
    if (auto error = xcb_request_check(conn, cookie)) {
        // fprintf(stderr, "ERROR: can't %s : %d\n", message,
        // error->error_code);
        LOG(ERROR) << "can't " << message << " : error-code "
                   << error->error_code;
        free(error);
        exit(EXIT_FAILURE);
    }
}

} // namespace x11
