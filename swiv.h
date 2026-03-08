#ifndef SWIV_H
#define SWIV_H

#include <stdbool.h>

#include <wayland-client.h>
#include <wld/wayland.h>
#include <wld/wld.h>

#include "image.h"
#include "protocol/xdg-shell-client-protocol.h"

struct swiv_wayland_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
};

struct swiv_render_state {
	struct wld_context *wld_context;
	struct wld_surface *wld_surface;
	enum wld_format format;
	int surface_width;
	int surface_height;
};

struct swiv_view_state {
	struct image image;
	int pending_width;
	int pending_height;
	int window_width;
	int window_height;
};

struct swiv_runtime_state {
	bool configured;
	bool running;
};

struct swiv_ctx {
	struct swiv_wayland_state wl;
	struct swiv_render_state render;
	struct swiv_view_state view;
	struct swiv_runtime_state runtime;
};

extern struct swiv_ctx *swiv;

void render(struct swiv_ctx *ctx);
void aspect_fit(int in_w, int in_h, int img_w, int img_h, int *out_w, int *out_h);

#endif /* SWIV_H */

