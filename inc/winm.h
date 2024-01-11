#ifndef WINM_H
#define WINM_H

extern "C" {
#include <xcb/xcb.h>
}
#include "utils.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace x11 {

class WindowManager {
public:
    ~WindowManager();
    static ::std::unique_ptr<WindowManager>
    getInstance(const ::std::string &display_name = "");

    WindowManager(WindowManager &&wm) noexcept;
    WindowManager &operator=(WindowManager &&wm) noexcept;

    WindowManager(const WindowManager &wm) = delete;
    WindowManager &operator=(const WindowManager &wm) = delete;

    // Event loop
    void run();

private:
    explicit WindowManager(xcb_connection_t *c, const xcb_screen_t *s);
    // Reparenting/Framing
    /***
     * @description: Frame a window
     * @param {xcb_window_t} window to be framed
     * @param {bool} does the window was created before wm
     * @return {*}
     */
    void addFrame(xcb_window_t w, bool created_before);
    /***
     * @description: UnFrame a window
     * @param {xcb_window_t} window to be framed
     * @return {*}
     */
    void unFrame(xcb_window_t w);

    // Callbacks
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

    void errorHandler(xcb_generic_error_t *error,
                      const char *message) const noexcept;
    void errorHandler(xcb_void_cookie_t cookie,
                      const char *message) const noexcept;
    void onWMDetected(xcb_connection_t *c, xcb_generic_error_t *e);
    // Geometerys
    utils::Position<int16_t> drag_start_pos_;
    utils::Position<int16_t> drag_start_frame_pos_;
    utils::Size<int16_t> drag_start_frame_size_;

    // Attributes
    // const xcb_atom_t XCB_PROPERTY_DELETE;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    const xcb_window_t root;
    ::std::unordered_map<xcb_window_t, xcb_window_t> clients;
    static ::std::atomic<bool> wm_detected_;
    static ::std::mutex wm_detected_mutex_;
};

} // namespace x11

#endif // WINM_H