/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_EXT_FOREIGN_TOPLEVEL_H
#define LABWC_EXT_FOREIGN_TOPLEVEL_H

#include <wayland-server-core.h>

struct ext_foreign_toplevel {
	struct view *view;
	struct wlr_ext_foreign_toplevel_handle_v1 *handle;
	struct wlr_ext_foreign_toplevel_state_handle_v1 *state_handle;

	/* Client side events */
	struct {
		struct wl_listener request_maximize;
		struct wl_listener request_minimize;
		struct wl_listener request_fullscreen;
		struct wl_listener request_activate;
		struct wl_listener request_close;
		struct wl_listener request_always_on_top;
		struct wl_listener request_sticky;
		struct wl_listener request_shaded;
		struct wl_listener handle_destroy;
		struct wl_listener state_handle_destroy;
	} on;

	/* Compositor side state updates */
	struct {
		struct wl_listener new_app_id;
		struct wl_listener new_title;
		struct wl_listener new_outputs;
		struct wl_listener maximized;
		struct wl_listener minimized;
		struct wl_listener fullscreened;
		struct wl_listener set_always_on_top;
		struct wl_listener set_sticky;
		struct wl_listener set_shaded;
		struct wl_listener activated;
	} on_view;
};

void ext_foreign_toplevel_set_parent(struct ext_foreign_toplevel *ext_toplevel,
	struct ext_foreign_toplevel *parent);

void ext_foreign_toplevel_init(struct ext_foreign_toplevel *ext_toplevel,
	struct view *view);
void ext_foreign_toplevel_finish(struct ext_foreign_toplevel *ext_toplevel);

#endif /* LABWC_EXT_FOREIGN_TOPLEVEL_H */
