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
    [[noreturn]] void run();

private:
    explicit WindowManager(xcb_connection_t *c, const xcb_screen_t *s);
    // Reparenting/Framing
    /***
     * @description: Frame a window
     * @param {xcb_window_t} window to be framed
     * @param {bool} does the window was created before wm
     * @return {*}
     */
    void addFrame(xcb_window_t* w, bool created_before);
    /***
     * @description: UnFrame a window
     * @param {xcb_window_t} window to be framed
     * @return {*}
     */
    void unFrame(xcb_window_t w);

    // Callbacks
    void onCreateNotify(const xcb_create_notify_event_t &e) const;
    void onDestroyNotify(const xcb_destroy_notify_event_t &e) const;
    void onConfigureRequest(const xcb_configure_request_event_t &e) const;
    void onConfigureNotify(const xcb_configure_notify_event_t &e) const;
    void onMapRequest(const xcb_map_request_event_t &e) const;
    void onMapNotify(const xcb_map_notify_event_t &e) const;
    void onUnmapNotify(const xcb_unmap_notify_event_t &e) const;
    void onReparentNotify(const xcb_reparent_notify_event_t &e) const;
    void onResizeRequest(const xcb_resize_request_event_t &e) const;
    void onExpose(const xcb_expose_event_t &e) const;

    void onFocusIn(const xcb_focus_in_event_t &e) const;
    void onFocusOut(const xcb_focus_out_event_t &e) const;
    void onButtonPress(const xcb_button_press_event_t &e) const;
    void onButtonRelease(const xcb_button_release_event_t &e) const;
    void onKeyPress(const xcb_key_press_event_t &e) const;
    void onKeyRelease(const xcb_key_release_event_t &e) const;
    void onMotionNotify(const xcb_motion_notify_event_t &e) const;
    void onEnterNotify(const xcb_enter_notify_event_t &e) const;
    void onLeaveNotify(const xcb_leave_notify_event_t &e) const;

    static uint8_t onXError(xcb_connection_t *c, xcb_generic_error_t *e);
    static uint8_t onWMDetected(xcb_connection_t *c, xcb_generic_error_t *e);
    // Geometerys
    utils::Coordinate<uint16_t> drag_start_pos_;
    utils::Coordinate<uint16_t> drag_start_frame_pos_;
    utils::Size<uint16_t> drag_start_frame_size_;

    // Attributes
    // const xcb_atom_t XCB_PROPERTY_DELETE;
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    const xcb_window_t root;
    ::std::unordered_map<xcb_window_t, xcb_window_t> clients_;
    static ::std::atomic<bool> wm_detected_;
    static ::std::mutex wm_detected_mutex_;
};

} // namespace x11

#endif // WINM_H