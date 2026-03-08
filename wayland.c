#include <stdio.h>
#include <string.h>

#include "swiv.h"
#include "wayland.h"

struct swiv_ctx *swiv = NULL;

static void buffer_release(void *data, struct wl_buffer *wl)
{
	struct wld_buffer *buffer = data;
	(void)wl;

	if (swiv && swiv->render.wld_surface)
		wld_surface_release(swiv->render.wld_surface, buffer);
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
	struct swiv_ctx *ctx = data;

	/* bind wl_compositor and xdg_wm_base */
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t bind_version = version < 4 ? version : 4;
		ctx->wl.compositor = wl_registry_bind(registry, name,
		                                   &wl_compositor_interface,
		                                   bind_version);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		ctx->wl.wm_base = wl_registry_bind(registry, name,
		                               &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(ctx->wl.wm_base, &wm_base_listener, ctx);
	}
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	struct swiv_ctx *ctx = data;

	/* ack configure */
	xdg_surface_ack_configure(surface, serial);
	if (!ctx->runtime.configured) {
		ctx->view.pending_width = ctx->view.image.width;
		ctx->view.pending_height = ctx->view.image.height;
	}
	ctx->runtime.configured = true;
	render(ctx);
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct swiv_ctx *ctx = data;
	(void)toplevel;
	ctx->runtime.running = false;
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                  int32_t width, int32_t height,
                                  struct wl_array *states)
{
	struct swiv_ctx *ctx = data;
	(void)toplevel;
	(void)states;

	if (width > 0 || height > 0) {
		int fitted_w = 0;
		int fitted_h = 0;
		aspect_fit(width, height, ctx->view.image.width, ctx->view.image.height,
		           &fitted_w, &fitted_h);
		ctx->view.pending_width = fitted_w;
		ctx->view.pending_height = fitted_h;
	}
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(base, serial);
}

const struct wl_buffer_listener buffer_listener = { .release = buffer_release, };
const struct xdg_wm_base_listener wm_base_listener = { .ping = xdg_wm_base_ping, };
const struct xdg_surface_listener xdg_surface_listener = { .configure = xdg_surface_configure, };
const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure, .close = xdg_toplevel_close,
};
const struct wl_registry_listener registry_listener = {
	.global = registry_global, .global_remove = registry_remove,
};

