#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "swiv.h"
#include "wayland.h"

struct swiv_ctx *swiv = NULL;

enum {
	MOD_SHIFT = 1u << 0,
	MOD_CTRL = 1u << 1,
	MOD_ALT = 1u << 2,
	MOD_LOGO = 1u << 3,
};

struct keybind {
	uint32_t mods;
	xkb_keysym_t keysym;
	void (*action)(struct swiv_ctx *ctx);
};

static void action_quit(struct swiv_ctx *ctx);
static void buffer_release(void *data, struct wl_buffer *wl);
static void keybind_perform(struct swiv_ctx *ctx, xkb_keysym_t keysym);
static void keyboard_clear_state(struct swiv_ctx *ctx);
static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys);
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state);
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size);
static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface);
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group);
static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay);
static uint32_t mods_pressed(struct xkb_state *state);
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version);
static void registry_remove(void *data, struct wl_registry *registry, uint32_t name);
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void seat_name(void *data, struct wl_seat *seat, const char *name);
static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial);
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel);
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states);
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial);

static void action_quit(struct swiv_ctx *ctx)
{
	ctx->runtime.running = false;
}

static const struct keybind keybinds[] = { /* TODO? move to config */
	{ .mods = 0, .keysym = XKB_KEY_Escape, .action = action_quit },
	{ .mods = 0, .keysym = XKB_KEY_q, .action = action_quit },
	{ .mods = MOD_SHIFT, .keysym = XKB_KEY_Q, .action = action_quit },
};

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void buffer_release(void *data, struct wl_buffer *wl)
{
	struct wld_buffer *buffer = data;
	(void)wl;

	if (swiv && swiv->render.wld_surface)
		wld_surface_release(swiv->render.wld_surface, buffer);
}

static void keybind_perform(struct swiv_ctx *ctx, xkb_keysym_t keysym)
{
	/* find matching keybind and execute action */
	uint32_t mods = mods_pressed(ctx->input.xkb_state);
	for (size_t i = 0; i < sizeof keybinds / sizeof keybinds[0]; ++i) {
		if (keybinds[i].keysym == keysym && keybinds[i].mods == mods) {
			keybinds[i].action(ctx);
			return;
		}
	}
}

static void keyboard_clear_state(struct swiv_ctx *ctx)
{
	/* deref and clear xkb state and keymap */
	if (ctx->input.xkb_state) {
		xkb_state_unref(ctx->input.xkb_state);
		ctx->input.xkb_state = NULL;
	}
	if (ctx->input.xkb_keymap) {
		xkb_keymap_unref(ctx->input.xkb_keymap);
		ctx->input.xkb_keymap = NULL;
	}
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state)
{
	(void)keyboard;
	(void)serial;
	(void)time;

	struct swiv_ctx *ctx = data;
	xkb_keysym_t keysym;
	
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !ctx->input.xkb_state)
		return;

	/* get keysym for key event */
	keysym = xkb_state_key_get_one_sym(ctx->input.xkb_state, key + 8);
	if (keysym != XKB_KEY_NoSymbol)
		keybind_perform(ctx, keysym);
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
	struct swiv_ctx *ctx = data;
	char *mapped;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	(void)keyboard;

	/* only support xkb v1 keymaps */
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || fd < 0 || size == 0) {
		if (fd >= 0)
			close(fd);
		return;
	}

	mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED) {
		close(fd);
		return;
	}

	/* clear old state and keymap */
	keymap = xkb_keymap_new_from_string(ctx->input.xkb_context, mapped,
	                                    XKB_KEYMAP_FORMAT_TEXT_V1,
	                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(mapped, size);
	close(fd);

	if (!keymap)
		return;

	/* create new xkb state from keymap */
	state = xkb_state_new(keymap);
	if (!state) {
		xkb_keymap_unref(keymap);
		return;
	}

	keyboard_clear_state(ctx);
	ctx->input.xkb_keymap = keymap;
	ctx->input.xkb_state = state;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
	(void)keyboard;
	(void)serial;

	struct swiv_ctx *ctx = data;
	if (!ctx->input.xkb_state)
		return;

	/* update xkb state with new modifiers */
	xkb_state_update_mask(ctx->input.xkb_state,
	                      mods_depressed,
	                      mods_latched,
	                      mods_locked,
	                      0,
	                      0,
	                      group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static uint32_t mods_pressed(struct xkb_state *state)
{
	uint32_t mods = 0;

	if (!state)
		return mods;

	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
		mods |= MOD_SHIFT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		mods |= MOD_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		mods |= MOD_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
		mods |= MOD_LOGO;

	return mods;
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
	struct swiv_ctx *ctx = data;

	/* bind wl_compositor, xdg_wm_base, wl_seat */
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t bind_version = version < 4 ? version : 4;
		ctx->wl.compositor = wl_registry_bind(registry, name,
		                                      &wl_compositor_interface,
		                                      bind_version);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		ctx->wl.wm_base = wl_registry_bind(registry, name,
		                                   &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(ctx->wl.wm_base, &wm_base_listener, ctx);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		uint32_t bind_version = version < 5 ? version : 5;
		ctx->wl.seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
		wl_seat_add_listener(ctx->wl.seat, &seat_listener, ctx);
	}
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
	struct swiv_ctx *ctx = data;

	/* if seat has keyboard cap, get wl_keyboard, add listener */
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->wl.keyboard) {
		ctx->wl.keyboard = wl_seat_get_keyboard(seat);
		if (ctx->wl.keyboard)
			wl_keyboard_add_listener(ctx->wl.keyboard, &keyboard_listener, ctx);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && ctx->wl.keyboard) {
		wl_keyboard_destroy(ctx->wl.keyboard);
		ctx->wl.keyboard = NULL;
		keyboard_clear_state(ctx);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
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
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};
const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

