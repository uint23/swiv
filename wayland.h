#ifndef WAYLAND_H
#define WAYLAND_H

extern const struct wl_buffer_listener buffer_listener;
extern const struct xdg_wm_base_listener wm_base_listener;
extern const struct xdg_surface_listener xdg_surface_listener;
extern const struct xdg_toplevel_listener xdg_toplevel_listener;
extern const struct wl_registry_listener registry_listener;

#endif /* WAYLAND_H */

