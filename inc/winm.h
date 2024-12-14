#ifndef WINM_H
#define WINM_H

extern "C" {
#include <xcb/xcb.h>
}
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "utils.hpp"
#include "xcb.hpp"

namespace x11
{

class WindowManager
{
public:
    ~WindowManager();
    static WindowManager* instance(const std::string &display_name = "");

    WindowManager(WindowManager &&wm) noexcept = delete;
    WindowManager &operator=(WindowManager &&wm) noexcept = delete;

    WindowManager(const WindowManager &wm) = delete;
    WindowManager &operator=(const WindowManager &wm) = delete;

    // Event loop
    void run();

private:
    explicit WindowManager(xcb_connection_t *c, xcb_screen_t *s);

    /***
     * @description: Frame a window
     * @param {xcb_window_t} window to be framed
     * @param {bool} does the window was created before wm
     */
    void addFrame(xcb_window_t w, bool created_before);
    /***
     * @description: UnFrame a window
     * @param {xcb_window_t} window to be framed
     */
    void unFrame(xcb_window_t w);

    // Callbacks
    void onClientMessage(xcb_client_message_event_t *ev);
    void onCreateNotify(xcb_create_notify_event_t *ev);
    void onDestroyNotify(xcb_destroy_notify_event_t *ev);
    void onConfigureRequest(xcb_configure_request_event_t *ev);
    void onConfigureNotify(xcb_configure_notify_event_t *ev);
    void onMapRequest(xcb_map_request_event_t *ev);
    void onMapNotify(xcb_map_notify_event_t *ev);
    void onUnmapNotify(xcb_unmap_notify_event_t *ev);
    void onReparentNotify(xcb_reparent_notify_event_t *ev);
    void onMotionNotify(xcb_motion_notify_event_t *ev);
    void onEnterNotify(xcb_enter_notify_event_t *ev);
    void onLeaveNotify(xcb_leave_notify_event_t *ev);

    void onExpose(xcb_expose_event_t *ev);
    void onResizeRequest(xcb_resize_request_event_t *ev);
    void onFocusIn(xcb_focus_in_event_t *ev);
    void onFocusOut(xcb_focus_out_event_t *ev);
    void onButtonPress(xcb_button_press_event_t *ev);
    void onButtonRelease(xcb_button_release_event_t *ev);
    void onKeyPress(xcb_key_press_event_t *ev);
    void onKeyRelease(xcb_key_release_event_t *ev);

    // Geometorys
    utils::Position<int16_t> m_drag_start_pos;
    utils::Position<int16_t> m_drag_start_frame_pos;
    utils::Size<int16_t> m_drag_start_frame_size;

    // Attributes
    // const xcb_atom_t XCB_PROPERTY_DELETE;
    xcb_connection_t *m_connection;
    xcb_screen_t *m_screen;
    const xcb_window_t m_root;
    std::unordered_map<xcb_window_t, xcb_window_t> m_clients; // <w,f>
    static WindowManager *m_instance;
};

} // namespace x11

#endif // WINM_H