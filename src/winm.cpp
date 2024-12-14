#include "winm.h"

#include <mutex>
#include <xcb/xcb_aux.h>

#include "aux.h"
#include "utils.hpp"

extern "C" {
#include <X11/Xutil.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
}

#include <glog/logging.h>

using namespace utils;

namespace x11
{

static std::once_flag wm_flag;
WindowManager *WindowManager::m_instance = nullptr;
static xcb::Atoms *atoms = xcb::Atoms::instance();

WindowManager *WindowManager::instance(const std::string &display_name)
{
    std::call_once(wm_flag, [&] {
        const char *name = display_name.empty() ? nullptr : display_name.data();
        xcb_connection_t *c = xcb_connect(name, nullptr);
        if (c == nullptr || xcb_connection_has_error(c) != 0) {
            LOG(ERROR) << "Failed to open X connection ";
            m_instance = nullptr;
            return;
        }
        // xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
        xcb_screen_t *s = xcb_aux_get_screen(c, 0); // we can just use xcb auxiliary function to do above stuff
        m_instance = new WindowManager(c, s);
    });
    return m_instance;
}

WindowManager::WindowManager(xcb_connection_t *c, xcb_screen_t *s)
    : m_connection(c)
    , m_screen(s)
    , m_root(s->root)
{
}

WindowManager::~WindowManager()
{
    xcb_disconnect(m_connection);
}

void WindowManager::run()
{
    // Register SubstructureRedirection on Root Window
    xcb::errorHandler(xcb_change_window_attributes_checked(
                          m_connection, m_root, XCB_CW_EVENT_MASK,
                          (const uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY}),
                      "WM register substructure redirection on m_root window");
    xcb_flush(m_connection); // Flush to X Server

    // Acquire all clients which are created before wm starts
    {
        xcb::ServerGraber graber(m_connection);

        XCB_CHECKED_REPLY(query_tree, "query for window tree", m_connection, m_root);

        CHECK_EQ(query_tree_result->root, m_root);
        LOG(WARNING) << "m_root children nums : " << query_tree_result->children_len;
        LOG(INFO) << "m_root : " << m_root;
        xcb_window_t *children = xcb_query_tree_children(query_tree_result.get());
        for (size_t i = 0; i < query_tree_result->children_len; ++i) {
            LOG(INFO) << "child " << i << " : " << children[i];
            addFrame(children[i], true);
        }
        // free(children); // 不需要释放这个数组
    }

    xcb_generic_event_t *event;
    while ((event = xcb_wait_for_event(m_connection))) {
        switch (event->response_type & ~0x80) {
        case XCB_CLIENT_MESSAGE:
            onClientMessage((xcb_client_message_event_t *)event);
            break;
        case XCB_CREATE_NOTIFY:
            onCreateNotify((xcb_create_notify_event_t *)event);
            break;
        case XCB_DESTROY_NOTIFY:
            onDestroyNotify((xcb_destroy_notify_event_t *)event);
            break;
        case XCB_REPARENT_NOTIFY:
            onReparentNotify((xcb_reparent_notify_event_t *)event);
            break;
        case XCB_MAP_NOTIFY:
            onMapNotify((xcb_map_notify_event_t *)event);
            break;
        case XCB_UNMAP_NOTIFY:
            onUnmapNotify((xcb_unmap_notify_event_t *)event);
            break;
        case XCB_CONFIGURE_NOTIFY:
            onConfigureNotify((xcb_configure_notify_event_t *)event);
            break;
        case XCB_EXPOSE:
            onExpose((xcb_expose_event_t *)event);
            break;
        case XCB_MAP_REQUEST:
            onMapRequest((xcb_map_request_event_t *)event);
            break;
        case XCB_CONFIGURE_REQUEST:
            onConfigureRequest((xcb_configure_request_event_t *)event);
            break;
        case XCB_ENTER_NOTIFY:
            onEnterNotify((xcb_enter_notify_event_t *)event);
            break;
        case XCB_LEAVE_NOTIFY:
            onLeaveNotify((xcb_leave_notify_event_t *)event);
            break;
        case XCB_FOCUS_IN:
            onFocusIn((xcb_focus_in_event_t *)event);
            break;
        case XCB_FOCUS_OUT:
            onFocusOut((xcb_focus_out_event_t *)event);
            break;
        case XCB_BUTTON_PRESS:
            onButtonPress((xcb_button_press_event_t *)event);
            break;
        case XCB_BUTTON_RELEASE:
            onButtonRelease((xcb_button_release_event_t *)event);
            break;
        case XCB_KEY_PRESS:
            onKeyPress((xcb_key_press_event_t *)event);
            break;
        case XCB_KEY_RELEASE:
            onKeyRelease((xcb_key_release_event_t *)event);
            break;
        case XCB_MOTION_NOTIFY:
            // Skip any already pending motion events, we only need the newest one.
            while ((event = xcb_poll_for_queued_event(m_connection)))
                if (event->response_type == XCB_MOTION_NOTIFY)
                    free(event);
            onMotionNotify((xcb_motion_notify_event_t *)event);
            break;
        default:
            LOG(INFO) << "Unknown event: " << event->response_type;
            break;
        }
        free(event);
    }
}

void WindowManager::addFrame(xcb_window_t w, bool created_before)
{
    constexpr unsigned int BORDER_WIDTH = 5;
    LOG(WARNING) << "want to frame :" << w;
    // Forbid multiple frame.
    CHECK(!m_clients.count(w));

    XCB_CHECKED_REPLY(get_window_attributes, "get window attributes", m_connection, w);
    // Make sure the window is managed by WM, and it must be currently visiable.
    if (created_before && (get_window_attributes_result->override_redirect == XCB_CW_OVERRIDE_REDIRECT || get_window_attributes_result->map_state != XCB_MAP_STATE_VIEWABLE)) {
        return;
    }

    // 1. Get the geometry of client window, so we can use it to create frame
    XCB_CHECKED_REPLY(get_geometry, "get geometry", m_connection, w);
    // 2. Create a frame.
    xcb_window_t frame = xcb_generate_id(m_connection);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        static_cast<uint32_t>(Colors::GREEN),
        static_cast<uint32_t>(Colors::GREY),
        // XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
    xcb::errorHandler(xcb_create_window_checked(
                          m_connection, get_geometry_result->depth, frame, m_root, get_geometry_result->x,
                          get_geometry_result->y, get_geometry_result->width, get_geometry_result->height,
                          BORDER_WIDTH, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                          XCB_COPY_FROM_PARENT, mask, values),
                      "create frame");
    // Configure window title
    const std::string title = std::string("WID: ").append(toString(w));
    const char title_icon[] = "XCB tinywm (iconified)";

    xcb::errorHandler(
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, frame, XCB_ATOM_WM_NAME,
                            XCB_ATOM_STRING, 8, title.length(), title.data()),
        "configure window title");
    // Configure window icon name
    xcb::errorHandler(xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, frame,
                                          XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8,
                                          strlen(title_icon), title_icon),
                      "configure window icon name");
    // 3. Add client window to save set.
    xcb::errorHandler(xcb_change_save_set_checked(m_connection, XCB_SET_MODE_INSERT, w),
                      "add client window to save set");
    // 4. Reparent client window with frame window.
    xcb::errorHandler(xcb_reparent_window_checked(m_connection, w, frame, 0, 0),
                      "reparent client window with frame window");
    // 5. Map frame.
    xcb::errorHandler(xcb_map_window_checked(m_connection, frame),
                      "map frame and client window");

    m_clients[w] = frame;

    // 6. Grab universal window management actions on client window.
    // 6.1 Move windows with alt + left button.
    xcb::errorHandler(xcb_grab_button_checked(
                          m_connection, 0, w,
                          XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
                          XCB_BUTTON_INDEX_1, XCB_MOD_MASK_1),
                      "grab alt + button1");
    // 6.2  Resize windows with alt + right button.
    xcb::errorHandler(xcb_grab_button_checked(
                          m_connection, 0, w,
                          XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                          XCB_BUTTON_INDEX_3, XCB_MOD_MASK_1),
                      "grab alt + button3");
    // 6.3 Kill windows with alt + middle button
    xcb::errorHandler(xcb_grab_button_checked(
                          m_connection, 0, w,
                          XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, w, XCB_NONE,
                          XCB_BUTTON_INDEX_2, XCB_MOD_MASK_1),
                      "grab alt + button2");
    // xcb::errorHandler(xcb_ungrab_key_checked(m_connection, xcb_keycode_), "grab key");
    // 6.4 Switch windows with ctrl.
    xcb::errorHandler(xcb_grab_key_checked(m_connection, 1, w, XCB_MOD_MASK_CONTROL, XCB_NONE,
                                           XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC),
                      "grab ctrl");
    xcb_flush(m_connection);
    LOG(INFO) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::unFrame(xcb_window_t w)
{
    CHECK(m_clients.count(w));
    // 1. Unmap frame.
    xcb::errorHandler(xcb_unmap_window_checked(m_connection, m_clients[w]), "unmap frame");
    // 2. Reparent client window.
    xcb::errorHandler(xcb_reparent_window_checked(m_connection, w, m_root, 0, 0),
                      "reparent client window");
    // 3. Remove client windom from save set.
    xcb::errorHandler(xcb_change_save_set_checked(m_connection, XCB_SET_MODE_DELETE, w),
                      "remove client window from save set");
    // 4. Destroy frame.
    xcb::errorHandler(xcb_destroy_window_checked(m_connection, m_clients[w]), "destroy frame");
    m_clients.erase(w);
    xcb_flush(m_connection);
    LOG(INFO) << "Unframed window " << w << " [" << m_clients[w] << "]";
}

void WindowManager::onClientMessage(xcb_client_message_event_t *ev)
{
    void *message = nullptr;
    switch (ev->format) {
    case 8:
        message = ev->data.data8;
        break;
    case 16:
        message = ev->data.data16;
        break;
    case 32:
        message = ev->data.data32;
        break;
    default:
        const char *(message) = "DATA ERROR!";
    }
    LOG(WARNING) << "WM listen a ClientMessage from " << ev->window
                 << ", content is " << ev->type << " : " << message;
}

void WindowManager::onCreateNotify(xcb_create_notify_event_t *ev)
{
}

void WindowManager::onDestroyNotify(xcb_destroy_notify_event_t *ev)
{
}

void WindowManager::onConfigureNotify(xcb_configure_notify_event_t *ev)
{
}

void WindowManager::onMapNotify(xcb_map_notify_event_t *ev)
{
}

void WindowManager::onUnmapNotify(xcb_unmap_notify_event_t *ev)
{
    if (!m_clients.count(ev->window)) {
        LOG(INFO) << "Ignore UnmapNotify for non-client window " << ev->window;
        return;
    }
    if (ev->event == m_root) {
        LOG(INFO) << "Ignore UnmapNotify for reparented pre-existing window "
                  << ev->window;
        return;
    }
    unFrame(ev->window);
}

void WindowManager::onReparentNotify(xcb_reparent_notify_event_t *ev)
{
}

void WindowManager::onExpose(xcb_expose_event_t *ev)
{
    XCB_CHECKED_REPLY(get_property, "get window name", m_connection, 0, ev->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 128);
    const char *text = nullptr;
    if (!m_clients.count(ev->window)) {
        text = static_cast<const char *>(xcb_get_property_value(get_property_result.get()));
        button_draw(m_connection, m_screen, ev->window, (ev->width - 7 * strlen(text)) / 2, (ev->height - 16) / 2, text);
        LOG(WARNING) << text;
        text = "Press ESC key to exit...";
        text_draw(m_connection, m_screen, ev->window, 10, ev->height - 10, text);
        // text_draw(m_connection, m_screen, m_clients[ev->window], (ev->x + ev->width) >> 1,
        // (ev->y + ev->height) >> 1, text);
        xcb_rectangle_t btn = {static_cast<int16_t>((ev->x + ev->width) >> 1), static_cast<int16_t>((ev->y + ev->height) >> 1),
                               15, 15}; // ev->x + 2 ev->y + ev->ev->width - 8
        xcb_poly_fill_rectangle(m_connection, ev->window, xcb_generate_id(m_connection), 1, &btn);
        LOG(WARNING) << text;
        xcb_flush(m_connection);
    }
    LOG(INFO) << "Window " << ev->window << " [" << text << "] exposed. Region to be redrawn at location (" << ev->x << "," << ev->y << "), with dimension (" << ev->width << "x" << ev->height << ")";
}

void WindowManager::onConfigureRequest(xcb_configure_request_event_t *ev)
{
    LOG(INFO) << "Captured Configure request from window " << ev->window;
    LOG(WARNING) << "ALL window: ";
    for (auto ele : m_clients)
        LOG(INFO) << ele.first << " ^ " << ele.second;
    LOG(WARNING) << "current: " << ev->parent << " | " << ev->window;
    CHECK(m_clients.count(ev->window));
    // If client want to configure, sure it will be fine. But we need to configure its frame first.
    uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE;
    const uint32_t values[] = {
        static_cast<uint32_t>(ev->x),
        static_cast<uint32_t>(ev->y),
        ev->width,
        ev->height,
        ev->border_width,
        ev->sibling,
        ev->stack_mode,
    };
    if (m_clients.count(ev->window)) {
        xcb::errorHandler(
            xcb_configure_window_checked(m_connection, m_clients[ev->window], mask, values),
            "configure frame"); /*ev->value_mask*/
        LOG(INFO) << "Resize Frame [" << m_clients[ev->window] << "] to "
                  << Size<uint16_t>(ev->width, ev->height);
    }
    xcb::errorHandler(xcb_configure_window_checked(m_connection, ev->window, mask, values),
                      "configure window"); /*ev->value_mask*/
    LOG(INFO) << "Resize Window [" << m_clients[ev->window] << "] to "
              << Size<uint16_t>(ev->width, ev->height);
}

void WindowManager::onMapRequest(xcb_map_request_event_t *ev)
{
    LOG(INFO) << "Captured Map request from window " << ev->window;
    // If client want to map, sure it will be fine.
    // And we must frame and reparent it first.
    addFrame(ev->window, false);
    xcb::errorHandler(xcb_map_window_checked(m_connection, ev->window), "map window");
}

void WindowManager::onResizeRequest(xcb_resize_request_event_t *ev)
{
    LOG(INFO) << "Captured Resize request from window " << ev->window;
}

void WindowManager::onFocusIn(xcb_focus_in_event_t *ev)
{
    LOG(INFO) << "Captured FocusIn from window  " << ev->event;
}

void WindowManager::onFocusOut(xcb_focus_out_event_t *ev)
{
    LOG(INFO) << "Captured FocusOut from window  " << ev->event;
}

void WindowManager::onButtonPress(xcb_button_press_event_t *ev)
{
    print_modifiers(ev->state);
    switch (ev->detail) {
    case 4:
        LOG(INFO) << "Wheel Button up in window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
        break;
    case 5:
        LOG(INFO) << "Wheel Button down in window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
        break;
    default:
        LOG(INFO) << "Button %d pressed in window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
    }

    // We need supervise the button(mice click) status for the provision of motion in case.
    CHECK(m_clients.count(ev->child));
    // 1. Store current window position and geometry.
    // NOTE - The coordinates must be global!
    m_drag_start_pos = Position<int16_t>(ev->event_x, ev->event_y);

    XCB_CHECKED_REPLY(get_geometry, "get window geometry", m_connection, ev->event);

    // Query for its parent window.
    XCB_CHECKED_REPLY(query_tree, "query for parent tree", m_connection, ev->event);
    XCB_CHECKED_REPLY(translate_coordinates, "query for parent coordinates", m_connection, ev->child, query_tree_result->parent,
                      get_geometry_result->x, get_geometry_result->y);
    m_drag_start_frame_pos = Position<int16_t>(translate_coordinates_result->dst_x, translate_coordinates_result->dst_y);
    m_drag_start_frame_size = Size<int16_t>(get_geometry_result->width, get_geometry_result->height);

    // 2. Raise clicked window to top.
    xcb::errorHandler(xcb_configure_window_checked(m_connection, ev->child, XCB_CONFIG_WINDOW_STACK_MODE, 
    (const uint32_t[]){XCB_STACK_MODE_ABOVE}), "raise to top");
}

void WindowManager::onButtonRelease(xcb_button_release_event_t *ev)
{
    print_modifiers(ev->state);
    LOG(INFO) << "Button %d released in window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
}

void WindowManager::onKeyRelease(xcb_key_release_event_t *ev)
{
    print_modifiers(ev->state);
    LOG(INFO) << "Key released in window " << ev->event;
}

void WindowManager::onMotionNotify(xcb_motion_notify_event_t *ev)
{
    LOG(INFO) << "Mouse moved in window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";

    // Now we can check the window position to see if it has moved.
    LOG(WARNING) << m_clients.size();
    for (auto &ele : m_clients)
        LOG(WARNING) << ele.first << " : " << ele.second << std::endl;
    LOG(INFO) << ev->root << " | " << ev->event << " | " << ev->child;
    CHECK(m_clients.count(ev->child)); // FIXME - 问题，鼠标经过窗口，应该是先经过外部的frame吧。因此这里ev->event是外框，ev->child是孩子
    // 1. Move the frame first.
    // XCB_CHECKED_REPLY(query_tree, "query for parent tree", m_connection, ev->child);
    // 2. Move the window to destination.
    const Position<int16_t> drag_pos(ev->root_x, ev->root_y);
    const Vector2D<int16_t> delta = drag_pos - m_drag_start_pos;
    // 3. Check the pressed keys.
    // Move the frame, so its children should be moved(the children won't move automatically,
    // I just didn't write relavent code here).
    if (ev->state & XCB_BUTTON_MASK_1) {
        LOG(INFO) << "Alt+Mouse Left Click pressed";
        const Position<int16_t> dest_frame_pos = m_drag_start_frame_pos + delta;
        const static uint32_t values[] = {static_cast<uint32_t>(dest_frame_pos.x),
                                          static_cast<uint32_t>(dest_frame_pos.y)};
        xcb::errorHandler(xcb_configure_window_checked(m_connection, m_clients[ev->child],
                                                       ev->state, values),
                          "move window");
    } else if (ev->state & XCB_BUTTON_MASK_3) {
        LOG(INFO) << "Alt+Mouse Right Click pressed";
        static const auto cmp = [](int16_t a, int16_t b) -> int16_t {
            return a > b ? a : b;
        };
        const Vector2D<int16_t> size_delta(
            cmp(delta.x, -m_drag_start_frame_size.width),
            cmp(delta.y, -m_drag_start_frame_size.height));
        const Size<int16_t> dest_frame_size = m_drag_start_frame_size + size_delta;
        // Resize frame.
        const uint32_t values[] = {static_cast<uint32_t>(dest_frame_size.width),
                                   static_cast<uint32_t>(dest_frame_size.height)};
        xcb::errorHandler(
            xcb_configure_window_checked(
                m_connection, m_clients[ev->child],
                XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values),
            "resize window");
        // Resize client.
        xcb::errorHandler(
            xcb_configure_window_checked(
                m_connection, ev->child,
                XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values),
            "resize window");
    }
}

void WindowManager::onEnterNotify(xcb_enter_notify_event_t *ev)
{
    LOG(INFO) << "Mouse entered window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
    cursor_set(m_connection, m_screen, ev->event, CursorGlyph::Hand);
}

void WindowManager::onLeaveNotify(xcb_leave_notify_event_t *ev)
{
    LOG(INFO) << "Mouse left window " << ev->event << ", at coordinates ("<< ev->event_x << "," << ev->event_y << ")";
    cursor_set(m_connection, m_screen, ev->event, CursorGlyph::Arrow);
}

void WindowManager::onKeyPress(xcb_key_press_event_t *ev)
{
    LOG(INFO) << "Key pressed in window " << ev->event;
    print_modifiers(ev->state);

    // ESC: Close window.
    xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(m_connection);
    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(symbols, ev->detail, 0);
    // After elimate the target window, the next window in the stacking order
    // should get focus.
    if (ev->detail == static_cast<xcb_keycode_t>(KeyMap::ESC)) {
        xcb_icccm_get_wm_protocols_reply_t protocols_reply;
        if (xcb_icccm_get_wm_protocols_reply(m_connection, xcb_icccm_get_wm_protocols(m_connection, ev->child, 
                *atoms->atom("WM_DELETE_WINDOW")), &protocols_reply, nullptr)) {
            if (std::find(protocols_reply.atoms, protocols_reply.atoms + protocols_reply.atoms_len,
                          *atoms->atom("WM_DELETE_WINDOW")) != protocols_reply.atoms + protocols_reply.atoms_len) {
                LOG(INFO) << "Send message to deleting window " << ev->child;

                xcb_client_message_event_t msg;
                memset(&msg, 0, sizeof(msg));
                msg.response_type = XCB_CLIENT_MESSAGE;
                msg.window = ev->child;
                msg.type = XCB_ATOM_STRING;
                msg.format = 32;
                msg.data.data32[0] = *atoms->atom("WM_DELETE_WINDOW");

                xcb::errorHandler(
                    xcb_send_event_checked(m_connection, false, ev->child,
                                           XCB_EVENT_MASK_NO_EVENT, (const char *)&msg),
                    "send window delete message");

                xcb_flush(m_connection);

                xcb_icccm_get_wm_protocols_reply_wipe(&protocols_reply);
            } else {
                // Just kill window by force.
                LOG(INFO) << "Killing window " << ev->child;
                xcb_kill_client(m_connection, ev->child);
                xcb_flush(m_connection);
            }
        }
    } else if (ev->detail == XCB_MOD_MASK_CONTROL) {
        // Ctrl: Switch window.
        // (Assuming m_clients is a std::map or similar container)
        auto it = m_clients.find(ev->child);
        if (it != m_clients.end()) {
            ++it;
            if (it == m_clients.end())
                it = m_clients.begin();

            // Raise and set focus
            xcb_change_window_attributes(m_connection, it->second, XCB_STACK_MODE_ABOVE,
                                         (const uint32_t[]){XCB_STACK_MODE_ABOVE});
            xcb_set_input_focus(m_connection, XCB_INPUT_FOCUS_POINTER_ROOT, it->first,
                                XCB_CURRENT_TIME);

            xcb_flush(m_connection);
        }
    }
}

} // namespace x11
