// SPDX-License-Identifier: GPL-2.0-only
#include "foreign-toplevel/ext-foreign.h"
#include <assert.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_state_v1.h>
#include "common/macros.h"
#include "labwc.h"
#include "output.h"
#include "view.h"

/* ext signals */
static void
handle_request_minimize(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_minimize);
	struct wlr_ext_foreign_toplevel_state_handle_v1_minimized_event *event = data;

	view_minimize(ext_toplevel->view, event->minimized);
}

static void
handle_request_maximize(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_maximize);
	struct wlr_ext_foreign_toplevel_state_handle_v1_maximized_event *event = data;

	view_maximize(ext_toplevel->view,
		event->maximized ? VIEW_AXIS_BOTH : VIEW_AXIS_NONE,
		/*store_natural_geometry*/ true);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_fullscreen);
	struct wlr_ext_foreign_toplevel_state_handle_v1_fullscreen_event *event = data;

	/* TODO: This ignores event->output */
	view_set_fullscreen(ext_toplevel->view, event->fullscreen);
}

static void
handle_request_activate(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_activate);

	/* In a multi-seat world we would select seat based on event->seat here. */
	desktop_focus_view(ext_toplevel->view, /*raise*/ true);
}

static void
handle_request_close(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_close);

	view_close(ext_toplevel->view);
}

static void
handle_request_always_on_top(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_always_on_top);
	struct wlr_ext_foreign_toplevel_state_handle_v1_always_on_top_event *event = data;

	if (event->always_on_top != view_is_always_on_top(ext_toplevel->view)) {
		view_toggle_always_on_top(ext_toplevel->view);
	}
}

static void
handle_request_sticky(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_sticky);
	struct wlr_ext_foreign_toplevel_state_handle_v1_sticky_event *event = data;

	if (event->sticky != ext_toplevel->view->visible_on_all_workspaces) {
		view_toggle_visible_on_all_workspaces(ext_toplevel->view);
	}
}

static void
handle_request_shaded(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.request_shaded);
	struct wlr_ext_foreign_toplevel_state_handle_v1_shaded_event *event = data;

	view_set_shade(ext_toplevel->view, event->shaded);
}

static void
destroy_handle(struct ext_foreign_toplevel *ext_toplevel)
{
	/* Client side requests */
	wl_list_remove(&ext_toplevel->on.request_maximize.link);
	wl_list_remove(&ext_toplevel->on.request_minimize.link);
	wl_list_remove(&ext_toplevel->on.request_fullscreen.link);
	wl_list_remove(&ext_toplevel->on.request_activate.link);
	wl_list_remove(&ext_toplevel->on.request_close.link);
	wl_list_remove(&ext_toplevel->on.request_always_on_top.link);
	wl_list_remove(&ext_toplevel->on.request_sticky.link);
	wl_list_remove(&ext_toplevel->on.request_shaded.link);
	wl_list_remove(&ext_toplevel->on.handle_destroy.link);
	wl_list_remove(&ext_toplevel->on.state_handle_destroy.link);

	/* Compositor side state changes */
	wl_list_remove(&ext_toplevel->on_view.new_app_id.link);
	wl_list_remove(&ext_toplevel->on_view.new_title.link);
	wl_list_remove(&ext_toplevel->on_view.new_outputs.link);
	wl_list_remove(&ext_toplevel->on_view.maximized.link);
	wl_list_remove(&ext_toplevel->on_view.minimized.link);
	wl_list_remove(&ext_toplevel->on_view.fullscreened.link);
	wl_list_remove(&ext_toplevel->on_view.activated.link);
	wl_list_remove(&ext_toplevel->on_view.set_always_on_top.link);
	wl_list_remove(&ext_toplevel->on_view.set_sticky.link);
	wl_list_remove(&ext_toplevel->on_view.set_shaded.link);
	ext_toplevel->handle = NULL;
	ext_toplevel->state_handle = NULL;
}

static void
handle_handle_destroy(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.handle_destroy);
	destroy_handle(ext_toplevel);
}

static void
handle_state_handle_destroy(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on.state_handle_destroy);
	destroy_handle(ext_toplevel);
}

/* Compositor signals */
static void
handle_new_app_id(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.new_app_id);
	assert(ext_toplevel->handle);

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = ext_toplevel->view->title,
		.app_id = ext_toplevel->view->app_id,
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(ext_toplevel->handle,
		&state);
}

static void
handle_new_title(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.new_title);
	assert(ext_toplevel->handle);

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = ext_toplevel->view->title,
		.app_id = ext_toplevel->view->app_id,
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(ext_toplevel->handle,
		&state);
}

static void
handle_new_outputs(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.new_outputs);
	assert(ext_toplevel->handle);

	/*
	 * Loop over all outputs and notify foreign_toplevel clients about changes.
	 * wlr_foreign_toplevel_handle_v1_output_xxx() keeps track of the active
	 * outputs internally and merges the events. It also listens to output
	 * destroy events so its fine to just relay the current state and let
	 * wlr_foreign_toplevel handle the rest.
	 */
	struct output *output;
	wl_list_for_each(output, &ext_toplevel->view->server->outputs, link) {
		if (view_on_output(ext_toplevel->view, output)) {
			wlr_ext_foreign_toplevel_state_handle_v1_output_enter(
				ext_toplevel->state_handle, output->wlr_output);
		} else {
			wlr_ext_foreign_toplevel_state_handle_v1_output_leave(
				ext_toplevel->state_handle, output->wlr_output);
		}
	}
}

static void
handle_maximized(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.maximized);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_maximized(ext_toplevel->state_handle,
		ext_toplevel->view->maximized == VIEW_AXIS_BOTH);
}

static void
handle_minimized(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.minimized);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_minimized(ext_toplevel->state_handle,
		ext_toplevel->view->minimized);
}

static void
handle_fullscreened(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.fullscreened);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_fullscreen(ext_toplevel->state_handle,
		ext_toplevel->view->fullscreen);
}

static void
handle_activated(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.activated);
	assert(ext_toplevel->handle);

	bool *activated = data;
	wlr_ext_foreign_toplevel_state_handle_v1_set_activated(ext_toplevel->state_handle,
		*activated);
}

static void
handle_set_always_on_top(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.set_always_on_top);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_always_on_top(
		ext_toplevel->state_handle,
		view_is_always_on_top(ext_toplevel->view));
}

static void
handle_set_sticky(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.set_sticky);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_sticky(
		ext_toplevel->state_handle,
		ext_toplevel->view->visible_on_all_workspaces);
}

static void
handle_set_shaded(struct wl_listener *listener, void *data)
{
	struct ext_foreign_toplevel *ext_toplevel =
		wl_container_of(listener, ext_toplevel, on_view.set_shaded);
	assert(ext_toplevel->handle);

	wlr_ext_foreign_toplevel_state_handle_v1_set_shaded(
		ext_toplevel->state_handle, ext_toplevel->view->shaded);
}

void
ext_foreign_toplevel_set_parent(struct ext_foreign_toplevel *ext_toplevel,
		struct ext_foreign_toplevel *parent)
{
	if (!ext_toplevel->handle) {
		return;
	}

	/* The wlroots wlr-foreign-toplevel impl ensures parent is reset to NULL on destroy */
	wlr_ext_foreign_toplevel_state_handle_v1_set_parent(ext_toplevel->state_handle,
		parent ? parent->state_handle : NULL);
}

/* Internal API */
void
ext_foreign_toplevel_init(struct ext_foreign_toplevel *ext_toplevel,
		struct view *view)
{
	assert(view->server->foreign_toplevel_list);
	ext_toplevel->view = view;

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view->title,
		.app_id = view->app_id,
	};
	ext_toplevel->handle = wlr_ext_foreign_toplevel_handle_v1_create(
		view->server->foreign_toplevel_list, &state);
	if (!ext_toplevel->handle) {
		wlr_log(WLR_ERROR, "cannot create ext toplevel handle for (%s)",
			view->title);
		return;
	}

	ext_toplevel->state_handle = wlr_ext_foreign_toplevel_state_handle_v1_create(
		view->server->foreign_toplevel_state, ext_toplevel->handle);
	if (!ext_toplevel->state_handle) {
		wlr_log(WLR_ERROR, "cannot create ext toplevel state handle for (%s)",
			view->title);
		wlr_ext_foreign_toplevel_handle_v1_destroy(ext_toplevel->handle);
		ext_toplevel->handle = NULL;
		return;
	}

	/* Client side requests */
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_maximize);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_minimize);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_fullscreen);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_activate);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_close);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_always_on_top);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_sticky);
	CONNECT_SIGNAL(ext_toplevel->state_handle, &ext_toplevel->on, request_shaded);
	ext_toplevel->on.handle_destroy.notify = handle_handle_destroy;
	wl_signal_add(&ext_toplevel->handle->events.destroy,
		&ext_toplevel->on.handle_destroy);
	ext_toplevel->on.state_handle_destroy.notify = handle_state_handle_destroy;
	wl_signal_add(&ext_toplevel->state_handle->events.destroy,
		&ext_toplevel->on.state_handle_destroy);

	/* Compositor side state changes */
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, new_app_id);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, new_title);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, new_outputs);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, maximized);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, minimized);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, fullscreened);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, activated);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, set_always_on_top);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, set_sticky);
	CONNECT_SIGNAL(view, &ext_toplevel->on_view, set_shaded);
}

void
ext_foreign_toplevel_finish(struct ext_foreign_toplevel *ext_toplevel)
{
	if (!ext_toplevel->handle) {
		return;
	}

	/* invokes handle_handle_destroy() which does more cleanup */
	wlr_ext_foreign_toplevel_handle_v1_destroy(ext_toplevel->handle);
	assert(!ext_toplevel->handle);
}
