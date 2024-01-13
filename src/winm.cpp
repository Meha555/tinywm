#include "winm.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "aux.h"
#include "utils.hpp"

extern "C" {
#include <X11/Xutil.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
}

#include <glog/logging.h>

using namespace utils;

namespace x11 {

std::atomic<bool> WindowManager::wm_detected_;
std::mutex WindowManager::wm_mutex_;
WindowManager *WindowManager::instance_ = nullptr;

std::unique_ptr<WindowManager> WindowManager::getInstance(
    const std::string &display_name) {
  if (instance_ == nullptr) {
    std::lock_guard<std::mutex> guard(wm_mutex_);
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
  return std::unique_ptr<WindowManager>(instance_);
}

WindowManager::WindowManager(xcb_connection_t *c, xcb_screen_t *s)
    : conn(c), screen(s), root(s->root) {}

WindowManager::~WindowManager() { xcb_disconnect(conn); }

void WindowManager::run() {
  // REVIEW - 为什么需要加锁？？
  //   wm_detected_.store(false);
  errorHandler(xcb_change_window_attributes_checked(
                   conn, root, XCB_CW_EVENT_MASK,
                   (const uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                                      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY}),
               "WM register substructure redirection on root window");
  xcb_flush(conn);
  //   if (wm_detected_.load()) {
  //     LOG(ERROR) << "Detected another window manager on connection";
  //     return;
  //   }
  // TODO -
  // 注册错误处理函数(xcb好像没有这个功能)，貌似都是在checked后缀的函数对应的xcb_request_check处理了

  errorHandler(xcb_grab_server_checked(conn), "grab X Server");

  xcb_generic_error_t *error = nullptr;
  xcb_query_tree_reply_t *result_tree =
      xcb_query_tree_reply(conn, xcb_query_tree(conn, root), &error);
  errorHandler(error, "query for window tree");

  CHECK_EQ(result_tree->root, root);
  LOG(WARNING) << "root children nums : " << result_tree->children_len;
  LOG(INFO) << "root : " << root;
  xcb_window_t *children = xcb_query_tree_children(result_tree);
  for (uint16_t i = 0; i < result_tree->children_len; ++i) {
    LOG(INFO) << "child " << i << " : " << children[i];
    addFrame(children[i], true);
  }
  // free(children); // 不需要释放这个数组
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
      case XCB_MOTION_NOTIFY: {  // Skip any already pending motion events.
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
  const unsigned int BORDER_WIDTH = 5;
  LOG(WARNING) << "want to frame :" << w;
  // Forbid multiple frame.
  CHECK(!clients_.count(w));

  xcb_generic_error_t *error = nullptr;
  xcb_get_window_attributes_reply_t *result_attr =
      xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, w),
                                      &error);

  // Make sure the window is managed by WM, and it must be currently visiable.
  if (created_before &&
      (result_attr->override_redirect == XCB_CW_OVERRIDE_REDIRECT ||
       result_attr->map_state != XCB_MAP_STATE_VIEWABLE)) {
    free(result_attr);
    return;
  }
  free(result_attr);
  // 1. Get the geometry of client window, so we can use it to create frame
  xcb_get_geometry_reply_t *result_geo =
      xcb_get_geometry_reply(conn, xcb_get_geometry(conn, w), &error);
  errorHandler(error, "get geometry");
  // 2. Create a frame.
  xcb_window_t frame = xcb_generate_id(conn);
  uint32_t mask;
  uint32_t values[3];
  mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
  values[0] = static_cast<uint32_t>(Colors::BG_COLOR);  // screen->black_pixel
  values[1] = static_cast<uint32_t>(Colors::BORDER_COLOR);
  // REVIEW -
  // 实测frame不能添加事件，否则由于其比client更大一些，导致事件会先经过frame的处理，导致断言失败
  values[2] =
      // XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_KEY_PRESS |
      //             XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
      //             XCB_EVENT_MASK_BUTTON_RELEASE |
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
  errorHandler(xcb_create_window_checked(
                   conn, result_geo->depth, frame, root, result_geo->x,
                   result_geo->y, result_geo->width, result_geo->height,
                   BORDER_WIDTH, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                   XCB_COPY_FROM_PARENT, mask, values),
               "create frame");
  free(result_geo);
  // Configure window title
  const std::string title = std::string("WID: ").append(toString(w));
  const char title_icon[] = "XCB tinywm (iconified)";

  errorHandler(
      xcb_change_property(conn, XCB_PROP_MODE_REPLACE, frame, XCB_ATOM_WM_NAME,
                          XCB_ATOM_STRING, 8, title.length(), title.c_str()),
      "configure window title");
  // Configure window icon name
  errorHandler(xcb_change_property(conn, XCB_PROP_MODE_REPLACE, frame,
                                   XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8,
                                   strlen(title_icon), title_icon),
               "configure window icon name");
  // 3. Add client window to save set.
  errorHandler(xcb_change_save_set_checked(conn, XCB_SET_MODE_INSERT, w),
               "add client window to save set");
  // 4. Reparent client window with frame window.
  errorHandler(xcb_reparent_window_checked(conn, w, frame, 0, 0),
               "reparent client window with frame window");
  // 5. Map frame.
  errorHandler(xcb_map_window_checked(conn, frame),
               "map frame and client window");
  clients_[w] = frame;
  // 6. Grab universal window management actions on client window.
  // 6.1 Move windows with alt + left button.
  errorHandler(xcb_grab_button_checked(
                   conn, 0, w,
                   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                       XCB_EVENT_MASK_BUTTON_MOTION,
                   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
                   XCB_BUTTON_INDEX_1, XCB_MOD_MASK_1),
               "grab alt + button1");
  // 6.2  Resize windows with alt + right button.
  errorHandler(xcb_grab_button_checked(
                   conn, 0, w,
                   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                       XCB_EVENT_MASK_BUTTON_MOTION,
                   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                   XCB_BUTTON_INDEX_3, XCB_MOD_MASK_1),
               "grab alt + button3");
  // 6.3 Kill windows with alt + middle button
  errorHandler(xcb_grab_button_checked(
                   conn, 0, w,
                   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                       XCB_EVENT_MASK_BUTTON_MOTION,
                   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                   XCB_BUTTON_INDEX_2, XCB_MOD_MASK_1),
               "grab alt + button2");
  // errorHandler(xcb_ungrab_key_checked(conn, xcb_keycode_), "grab key");
  // 6.4 Switch windows with ctrl.
  errorHandler(xcb_grab_key_checked(conn, 1, w, XCB_MOD_MASK_CONTROL, XCB_NONE,
                                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC),
               "grab ctrl");
  LOG(INFO) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::unFrame(xcb_window_t w) {
  CHECK(clients_.count(w));
  // 1. Unmap frame.
  errorHandler(xcb_unmap_window_checked(conn, clients_[w]), "unmap frame");
  // 2. Reparent client window.
  errorHandler(xcb_reparent_window_checked(conn, w, root, 0, 0),
               "reparent client window");
  // 3. Remove client windom from save set.
  errorHandler(xcb_change_save_set_checked(conn, XCB_SET_MODE_DELETE, w),
               "remove client window from save set");
  // 4. Destroy frame.
  errorHandler(xcb_destroy_window_checked(conn, clients_[w]), "destroy frame");
  clients_.erase(w);
  LOG(INFO) << "Unframed window " << w << " [" << clients_[w] << "]";
}

void WindowManager::onCreateNotify(xcb_create_notify_event_t *ev) {}

void WindowManager::onDestroyNotify(xcb_destroy_notify_event_t *ev) {}

void WindowManager::onConfigureNotify(xcb_configure_notify_event_t *ev) {}

void WindowManager::onMapNotify(xcb_map_notify_event_t *ev) {}

void WindowManager::onUnmapNotify(xcb_unmap_notify_event_t *ev) {
  if (!clients_.count(ev->window)) {
    LOG(INFO) << "Ignore UnmapNotify for non-client window " << ev->window;
    return;
  }
  if (ev->event == root) {
    LOG(INFO) << "Ignore UnmapNotify for reparented pre-existing window "
              << ev->window;
    return;
  }
  unFrame(ev->window);
}

void WindowManager::onReparentNotify(xcb_reparent_notify_event_t *ev) {}

void WindowManager::onExpose(xcb_expose_event_t *ev) {
  printf(
      "Window %u exposed. Region to be redrawn at location "
      "(%d,%d), with dimension (%d,%d)\n",
      ev->window, ev->x, ev->y, ev->width, ev->height);

  xcb_generic_error_t *error = nullptr;
  // xcb_intern_atom_reply_t *result_atom = xcb_intern_atom_reply(
  //     conn,
  //     xcb_intern_atom(conn, 1, strlen("XCB_ATOM_WM_NAME"),
  //     "XCB_ATOM_WM_NAME"), &error);
  xcb_get_property_reply_t *result_prop = xcb_get_property_reply(
      conn,
      xcb_get_property(conn, 0, ev->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                       0, 64),
      &error);
  const char *text =
      static_cast<const char *>(xcb_get_property_value(result_prop));
  // free(result_atom);
  free(result_prop);
  errorHandler(error, "get window geometry");
  text_draw(conn, screen, ev->window, 10, 0, text);
}

void WindowManager::onConfigureRequest(xcb_configure_request_event_t *ev) {
  printf("Captured Configure request from window %u!\n", ev->window);
  LOG(WARNING) << "ALL window: ";
  for (auto ele : clients_) LOG(INFO) << ele.first << " ^ " << ele.second;
  LOG(WARNING) << "current: " << ev->parent << " | " << ev->window;
  CHECK(clients_.count(ev->window));
  // If client want to configure, sure it will be fine.
  // But we need to configure its frame first.
  uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                  XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                  XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_SIBLING |
                  XCB_CONFIG_WINDOW_STACK_MODE;
  const uint32_t values[] = {
      static_cast<uint32_t>(ev->x),
      static_cast<uint32_t>(ev->y),
      ev->width,
      ev->height,
      ev->border_width,
      ev->sibling,
      ev->stack_mode,
  };
  if (clients_.count(ev->window)) {
    errorHandler(
        xcb_configure_window_checked(conn, clients_[ev->window], mask, values),
        "configure frame"); /*ev->value_mask*/
    LOG(INFO) << "Resize Frame [" << clients_[ev->window] << "] to "
              << Size<uint16_t>(ev->width, ev->height);
  }
  errorHandler(xcb_configure_window_checked(conn, ev->window, mask, values),
               "configure window"); /*ev->value_mask*/
  LOG(INFO) << "Resize Window [" << clients_[ev->window] << "] to "
            << Size<uint16_t>(ev->width, ev->height);
}

void WindowManager::onMapRequest(xcb_map_request_event_t *ev) {
  printf("Captured Map request from window %u!\n", ev->window);
  // If client want to map, sure it will be fine.
  // And we must frame and reparent it first.
  addFrame(ev->window, false);
  errorHandler(xcb_map_window_checked(conn, ev->window), "map window");
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
  CHECK(clients_.count(ev->child));
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
          xcb_translate_coordinates(conn, ev->child, result_tree->parent,
                                    result_geo->x, result_geo->y),
          &error);
  errorHandler(error, "query for parent tree");
  drag_start_frame_pos_ =
      Position<int16_t>(result_trans->dst_x, result_trans->dst_y);
  drag_start_frame_size_ = Size<int16_t>(result_geo->width, result_geo->height);
  free(result_geo);
  free(result_tree);
  free(result_trans);
  // 2. Raise clicked window to top.
  errorHandler(xcb_configure_window_checked(
                   conn, ev->child, XCB_CONFIG_WINDOW_STACK_MODE,
                   (const uint32_t[]){XCB_STACK_MODE_ABOVE}),
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

  // Now we can check the window position to see if it has moved.
  LOG(WARNING) << clients_.size();
  for (auto &ele : clients_)
    LOG(WARNING) << ele.first << " : " << ele.second << std::endl;
  LOG(INFO) << ev->root << " | " << ev->event << " | " << ev->child;
  CHECK(clients_.count(
      ev->child));  // FIXME -
                    // 问题，鼠标经过窗口，应该是先经过外部的frame吧。因此这里ev->event是外框，ev->child是孩子
  // 1. Move the frame first.
  xcb_generic_error_t *error;
  xcb_query_tree_reply_t *result_tree =
      xcb_query_tree_reply(conn, xcb_query_tree(conn, ev->child), &error);
  // 2. Move the window to destination.
  const Position<int16_t> drag_pos(ev->root_x, ev->root_y);
  const Vector2D<int16_t> delta = drag_pos - drag_start_pos_;
  // 3. Check the pressed keys.
  // Move the frame, so its children will follow it.
  if (ev->state & XCB_BUTTON_MASK_1) {
    LOG(INFO) << "Alt+Mouse Left Click pressed";
    const Position<int16_t> dest_frame_pos = drag_start_frame_pos_ + delta;
    const static uint32_t values[] = {static_cast<uint32_t>(dest_frame_pos.x),
                                      static_cast<uint32_t>(dest_frame_pos.y)};
    errorHandler(xcb_configure_window_checked(conn, clients_[ev->child],
                                              ev->state, values),
                 "move window");
  } else if (ev->state & XCB_BUTTON_MASK_3) {
    LOG(INFO) << "Alt+Mouse Right Click pressed";
    auto cmp = [](int16_t a, int16_t b) -> int16_t { return a > b ? a : b; };
    const Vector2D<int16_t> size_delta(
        // std::max(delta.x, -drag_start_frame_size_.width),
        // std::max(delta.y, -drag_start_frame_size_.height)
        cmp(delta.x, -drag_start_frame_size_.width),
        cmp(delta.y, -drag_start_frame_size_.height));
    const Size<int16_t> dest_frame_size = drag_start_frame_size_ + size_delta;
    // Resize frame.
    const uint32_t values[] = {static_cast<uint32_t>(dest_frame_size.width),
                               static_cast<uint32_t>(dest_frame_size.height)};
    errorHandler(
        xcb_configure_window_checked(
            conn, clients_[ev->child],
            XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values),
        "resize window");
    // Resize client.
    errorHandler(
        xcb_configure_window_checked(
            conn, ev->child,
            XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values),
        "resize window");
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
  xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(conn);
  xcb_keysym_t keysym = xcb_key_symbols_get_keysym(symbols, ev->detail, 0);
  // After elimate the target window, the next window in the stacking order
  // should get focus.
  if (ev->detail == XCB_MOD_MASK_CONTROL) {
    // 1. Find next window.
    auto i = clients_.find(ev->child);
    CHECK(i != clients_.end());
    ++i;  // Get next iterator of current window
    if (i == clients_.end()) i = clients_.begin();
    // 2. Raise and set focus.
    errorHandler(xcb_configure_window_checked(
                     conn, ev->child, XCB_CONFIG_WINDOW_STACK_MODE,
                     (const uint32_t[]){XCB_STACK_MODE_ABOVE}),
                 "raise to top");
    errorHandler(xcb_set_input_focus_checked(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                                             i->first, XCB_CURRENT_TIME),
                 "set input focus");
  }

  // // TODO - 实现通过属性和原子来让窗管控制窗口关闭的逻辑
  // if (ev->detail == static_cast<uint16_t>(KeyMap::ESC)) {
  //   errorHandler(xcb_destroy_window_checked(conn, ev->event), "destroy
  //   window"); free(ev); xcb_disconnect(conn);
  // }
}

void WindowManager::errorHandler(xcb_generic_error_t *error,
                                 const char *message) const noexcept {
  if (error) {
    // fprintf(stderr, "ERROR: can't %s : %d\n", message,
    // error->error_code);
    LOG(ERROR) << message << " failed. : " << error->error_code;
    free(error);
    error = nullptr;
    xcb_disconnect(conn);
    exit(EXIT_FAILURE);
  }
}

void WindowManager::errorHandler(xcb_void_cookie_t cookie,
                                 const char *message) const noexcept {
  if (auto error = xcb_request_check(conn, cookie)) {
    // fprintf(stderr, "ERROR: can't %s : %d\n", message,
    // error->error_code);
    LOG(ERROR) << message << " failed. : " << error->error_code;
    free(error);
    error = nullptr;
    xcb_disconnect(conn);
    exit(EXIT_FAILURE);
  }
}

}  // namespace x11
