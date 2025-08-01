// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "input/cursor.h"
#include <assert.h>
#include <linux/input-event-codes.h>
#include <sys/time.h>
#include <time.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/region.h>
#include "action.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "common/surface-helpers.h"
#include "config/mousebind.h"
#include "dnd.h"
#include "idle.h"
#include "input/gestures.h"
#include "input/keyboard.h"
#include "input/tablet.h"
#include "input/touch.h"
#include "labwc.h"
#include "layers.h"
#include "menu/menu.h"
#include "output.h"
#include "regions.h"
#include "resistance.h"
#include "resize-outlines.h"
#include "ssd.h"
#include "view.h"
#include "xwayland.h"

#define LAB_CURSOR_SHAPE_V1_VERSION 1

struct constraint {
	struct seat *seat;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

static const char * const *cursor_names = NULL;

/* Usual cursor names */
static const char * const cursors_xdg[] = {
	NULL,
	"default",
	"grab",
	"nw-resize",
	"n-resize",
	"ne-resize",
	"e-resize",
	"se-resize",
	"s-resize",
	"sw-resize",
	"w-resize"
};

/* XCursor fallbacks */
static const char * const cursors_x11[] = {
	NULL,
	"left_ptr",
	"grabbing",
	"top_left_corner",
	"top_side",
	"top_right_corner",
	"right_side",
	"bottom_right_corner",
	"bottom_side",
	"bottom_left_corner",
	"left_side"
};

static_assert(
	ARRAY_SIZE(cursors_xdg) == LAB_CURSOR_COUNT,
	"XDG cursor names are out of sync");
static_assert(
	ARRAY_SIZE(cursors_x11) == LAB_CURSOR_COUNT,
	"X11 cursor names are out of sync");

enum lab_cursors
cursor_get_from_edge(uint32_t resize_edges)
{
	switch (resize_edges) {
	case WLR_EDGE_NONE:
		return LAB_CURSOR_DEFAULT;
	case WLR_EDGE_TOP | WLR_EDGE_LEFT:
		return LAB_CURSOR_RESIZE_NW;
	case WLR_EDGE_TOP:
		return LAB_CURSOR_RESIZE_N;
	case WLR_EDGE_TOP | WLR_EDGE_RIGHT:
		return LAB_CURSOR_RESIZE_NE;
	case WLR_EDGE_RIGHT:
		return LAB_CURSOR_RESIZE_E;
	case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT:
		return LAB_CURSOR_RESIZE_SE;
	case WLR_EDGE_BOTTOM:
		return LAB_CURSOR_RESIZE_S;
	case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT:
		return LAB_CURSOR_RESIZE_SW;
	case WLR_EDGE_LEFT:
		return LAB_CURSOR_RESIZE_W;
	default:
		wlr_log(WLR_ERROR,
			"Failed to resolve wlroots edge %u to cursor name", resize_edges);
		assert(false);
	}

	return LAB_CURSOR_DEFAULT;
}

static enum lab_cursors
cursor_get_from_ssd(enum ssd_part_type view_area)
{
	uint32_t resize_edges = ssd_resize_edges(view_area);
	return cursor_get_from_edge(resize_edges);
}

static struct wlr_surface *
get_toplevel(struct wlr_surface *surface)
{
	while (surface) {
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_try_from_wlr_surface(surface);
		if (!xdg_surface) {
			break;
		}

		switch (xdg_surface->role) {
		case WLR_XDG_SURFACE_ROLE_NONE:
			return NULL;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
			return surface;
		case WLR_XDG_SURFACE_ROLE_POPUP:
			surface = xdg_surface->popup->parent;
			continue;
		}
	}
	if (surface && wlr_layer_surface_v1_try_from_wlr_surface(surface)) {
		return surface;
	}
	return NULL;
}

static void
handle_request_set_cursor(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(listener, seat, request_set_cursor);

	if (seat->server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		/* Prevent setting a cursor image when moving or resizing */
		return;
	}

	/*
	 * Omit cursor notifications when the current cursor is
	 * invisible, e.g. on touch input.
	 */
	if (!seat->cursor_visible) {
		return;
	}

	/*
	 * Omit cursor notifications from a pointer when a tablet
	 * tool (stylus/pen) is in proximity. We expect to get cursor
	 * notifications from the tablet tool instead.
	 * Receiving cursor notifications from pointer and tablet tool at
	 * the same time is a side effect of also setting pointer focus
	 * when a tablet tool enters proximity on a tablet-capable surface.
	 * See also `notify_motion()` in `input/tablet.c`.
	 */
	if (tablet_tool_has_focused_surface(seat)) {
		return;
	}

	/*
	 * This event is raised by the seat when a client provides a cursor
	 * image
	 */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		seat->seat->pointer_state.focused_client;

	/*
	 * This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first.
	 */
	if (focused_client == event->seat_client) {
		/*
		 * Once we've vetted the client, we can tell the cursor to use
		 * the provided surface as the cursor image. It will set the
		 * hardware cursor on the output that it's currently on and
		 * continue to do so as the cursor moves between outputs.
		 */

		wlr_cursor_set_surface(seat->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void
handle_request_set_shape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	const char *shape_name = wlr_cursor_shape_v1_name(event->shape);
	struct seat *seat = wl_container_of(listener, seat, request_set_shape);
	struct wlr_seat_client *focused_client = seat->seat->pointer_state.focused_client;

	/* Prevent setting a cursor image when moving or resizing */
	if (seat->server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	/*
	 * Omit set shape when the current cursor is
	 * invisible, e.g. on touch input.
	 */
	if (!seat->cursor_visible) {
		return;
	}

	/*
	 * This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first.
	 */
	if (event->seat_client != focused_client) {
		wlr_log(WLR_INFO, "seat client %p != focused client %p",
			event->seat_client, focused_client);
		return;
	}

	/*
	 * Omit cursor notifications from a pointer when a tablet
	 * tool (stylus/pen) is in proximity.
	 */
	if (tablet_tool_has_focused_surface(seat)
			&& event->device_type
				!= WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_TABLET_TOOL) {
		return;
	}

	wlr_log(WLR_DEBUG, "set xcursor to shape %s", shape_name);
	wlr_cursor_set_xcursor(seat->cursor, seat->xcursor_manager, shape_name);
}

static void
handle_request_set_selection(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(
		listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->seat, event->source,
		event->serial);
}

static void
handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(
		listener, seat, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat->seat, event->source,
		event->serial);
}

static void
process_cursor_move(struct server *server, uint32_t time)
{
	struct view *view = server->grabbed_view;

	int x = server->grab_box.x + (server->seat.cursor->x - server->grab_x);
	int y = server->grab_box.y + (server->seat.cursor->y - server->grab_y);

	/* Apply resistance for maximized/tiled view */
	bool needs_untile = resistance_unsnap_apply(view, &x, &y);
	if (needs_untile) {
		/*
		 * When the view needs to be un-tiled, resize it to natural
		 * geometry while anchoring it to cursor. If the natural
		 * geometry is unknown (possible with xdg-shell views), then
		 * we set a size of 0x0 here and determine the correct geometry
		 * later. See do_late_positioning() in xdg.c.
		 */
		struct wlr_box new_geo = {
			.width = view->natural_geometry.width,
			.height = view->natural_geometry.height,
		};
		interactive_anchor_to_cursor(server, &new_geo);
		/* Shaded clients will not process resize events until unshaded */
		view_set_shade(view, false);
		view_set_untiled(view);
		view_restore_to(view, new_geo);
		x = new_geo.x;
		y = new_geo.y;
	}

	/* Then apply window & edge resistance */
	resistance_move_apply(view, &x, &y);

	view_move(view, x, y);
	overlay_update(&server->seat);
}

static void
process_cursor_resize(struct server *server, uint32_t time)
{
	/* Rate-limit resize events respecting monitor refresh rate */
	static uint32_t last_resize_time = 0;
	static struct view *last_resize_view = NULL;

	assert(server->grabbed_view);
	if (server->grabbed_view == last_resize_view) {
		int32_t refresh = 0;
		if (output_is_usable(last_resize_view->output)) {
			refresh = last_resize_view->output->wlr_output->refresh;
		}
		/* Limit to 250Hz if refresh rate is not available */
		if (refresh <= 0) {
			refresh = 250000;
		}
		/* Not caring overflow, but it won't be observable */
		if (time - last_resize_time < 1000000 / (uint32_t)refresh) {
			return;
		}
	}

	last_resize_time = time;
	last_resize_view = server->grabbed_view;

	double dx = server->seat.cursor->x - server->grab_x;
	double dy = server->seat.cursor->y - server->grab_y;

	struct view *view = server->grabbed_view;
	struct wlr_box new_view_geo = view->current;

	if (server->resize_edges & WLR_EDGE_TOP) {
		/* Shift y to anchor bottom edge when resizing top */
		new_view_geo.y = server->grab_box.y + dy;
		new_view_geo.height = server->grab_box.height - dy;
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_view_geo.height = server->grab_box.height + dy;
	}

	if (server->resize_edges & WLR_EDGE_LEFT) {
		/* Shift x to anchor right edge when resizing left */
		new_view_geo.x = server->grab_box.x + dx;
		new_view_geo.width = server->grab_box.width - dx;
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_view_geo.width = server->grab_box.width + dx;
	}

	resistance_resize_apply(view, &new_view_geo);
	view_adjust_size(view, &new_view_geo.width, &new_view_geo.height);

	if (server->resize_edges & WLR_EDGE_TOP) {
		/* After size adjustments, make sure to anchor bottom edge */
		new_view_geo.y = server->grab_box.y +
			server->grab_box.height - new_view_geo.height;
	}

	if (server->resize_edges & WLR_EDGE_LEFT) {
		/* After size adjustments, make sure to anchor bottom right */
		new_view_geo.x = server->grab_box.x +
			server->grab_box.width - new_view_geo.width;
	}

	if (rc.resize_draw_contents) {
		view_move_resize(view, new_view_geo);
	} else {
		resize_outlines_update(view, new_view_geo);
	}
}

void
cursor_set(struct seat *seat, enum lab_cursors cursor)
{
	assert(cursor > LAB_CURSOR_CLIENT && cursor < LAB_CURSOR_COUNT);

	/* Prevent setting the same cursor image twice */
	if (seat->server_cursor == cursor) {
		return;
	}

	if (seat->cursor_visible) {
		wlr_cursor_set_xcursor(seat->cursor, seat->xcursor_manager,
			cursor_names[cursor]);
	}
	seat->server_cursor = cursor;
}

void
cursor_set_visible(struct seat *seat, bool visible)
{
	if (seat->cursor_visible == visible) {
		return;
	}

	seat->cursor_visible = visible;
	cursor_update_image(seat);
}

void
cursor_update_image(struct seat *seat)
{
	enum lab_cursors cursor = seat->server_cursor;

	if (!seat->cursor_visible) {
		wlr_cursor_unset_image(seat->cursor);
		return;
	}

	if (cursor == LAB_CURSOR_CLIENT) {
		/*
		 * When we loose the output cursor while over a client
		 * surface (e.g. output was destroyed and we now deal with
		 * a new output instance), we have to force a re-enter of
		 * the surface so the client sets its own cursor again.
		 */
		if (seat->seat->pointer_state.focused_surface) {
			seat->server_cursor = LAB_CURSOR_DEFAULT;
			wlr_cursor_set_xcursor(seat->cursor, seat->xcursor_manager, "");
			wlr_seat_pointer_clear_focus(seat->seat);
			cursor_update_focus(seat->server);
		}
		return;
	}
	/*
	 * Call wlr_cursor_unset_image() first to force wlroots to
	 * update the cursor (e.g. for a new output). Otherwise,
	 * wlr_cursor_set_xcursor() may detect that we are setting the
	 * same cursor as before, and do nothing.
	 */
	wlr_cursor_unset_image(seat->cursor);
	wlr_cursor_set_xcursor(seat->cursor, seat->xcursor_manager,
		cursor_names[cursor]);
}

static bool
update_pressed_surface(struct seat *seat, struct cursor_context *ctx)
{
	/*
	 * In most cases, we don't want to leave one surface and enter
	 * another while a button is pressed.  We only do so when
	 * (1) there is a pointer grab active (e.g. XDG popup grab) and
	 * (2) both surfaces belong to the same XDG toplevel.
	 *
	 * GTK/Wayland menus are known to use an XDG popup grab and to
	 * rely on the leave/enter events to work properly.  Firefox
	 * context menus (in contrast) do not use an XDG popup grab and
	 * do not work properly if we send leave/enter events.
	 */
	if (!wlr_seat_pointer_has_grab(seat->seat)) {
		return false;
	}
	if (seat->pressed.surface && ctx->surface != seat->pressed.surface) {
		struct wlr_surface *toplevel = get_toplevel(ctx->surface);
		if (toplevel && toplevel == get_toplevel(seat->pressed.surface)) {
			seat_set_pressed(seat, ctx);
			return true;
		}
	}
	return false;
}

static bool
process_cursor_motion_out_of_surface(struct server *server,
		double *sx, double *sy)
{
	struct view *view = server->seat.pressed.view;
	struct wlr_scene_node *node = server->seat.pressed.node;
	struct wlr_surface *surface = server->seat.pressed.surface;
	assert(surface);
	int lx, ly;

	if (node && wlr_subsurface_try_from_wlr_surface(surface)) {
		wlr_scene_node_coords(node, &lx, &ly);
	} else if (view) {
		lx = view->current.x;
		ly = view->current.y;
		/* Take into account invisible xdg-shell CSD borders */
		if (view->type == LAB_XDG_SHELL_VIEW) {
			struct wlr_xdg_surface *xdg_surface = xdg_surface_from_view(view);
			lx -= xdg_surface->geometry.x;
			ly -= xdg_surface->geometry.y;
		}
	} else if (node && wlr_layer_surface_v1_try_from_wlr_surface(surface)) {
		wlr_scene_node_coords(node, &lx, &ly);
#if HAVE_XWAYLAND
	} else if (node && node->parent == server->unmanaged_tree) {
		wlr_scene_node_coords(node, &lx, &ly);
#endif
	} else {
		wlr_log(WLR_ERROR, "Can't detect surface for out-of-surface movement");
		return false;
	}

	*sx = server->seat.cursor->x - lx;
	*sy = server->seat.cursor->y - ly;

	return true;
}

/*
 * Common logic shared by cursor_update_focus(), process_cursor_motion()
 * and cursor_axis()
 */
static bool
cursor_update_common(struct server *server, struct cursor_context *ctx,
		bool cursor_has_moved, double *sx, double *sy)
{
	struct seat *seat = &server->seat;
	struct wlr_seat *wlr_seat = seat->seat;

	ssd_update_button_hover(ctx->node, server->ssd_hover_state);

	if (server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		/*
		 * Prevent updating focus/cursor image during
		 * interactive move/resize, window switcher and
		 * menu interaction.
		 */
		return false;
	}

	/* TODO: verify drag_icon logic */
	if (seat->pressed.surface && ctx->surface != seat->pressed.surface
			&& !update_pressed_surface(seat, ctx)
			&& !seat->drag.active) {
		if (cursor_has_moved) {
			/*
			 * Button has been pressed while over another
			 * surface and is still held down.  Just send
			 * the motion events to the focused surface so
			 * we can keep scrolling or selecting text even
			 * if the cursor moves outside of the surface.
			 */
			return process_cursor_motion_out_of_surface(server, sx, sy);
		}
		return false;
	}

	if (ctx->surface) {
		/*
		 * Cursor is over an input-enabled client surface.  The
		 * cursor image will be set by request_cursor_notify()
		 * in response to the enter event.
		 */
		wlr_seat_pointer_notify_enter(wlr_seat, ctx->surface,
			ctx->sx, ctx->sy);
		seat->server_cursor = LAB_CURSOR_CLIENT;
		if (cursor_has_moved) {
			*sx = ctx->sx;
			*sy = ctx->sy;
			return true;
		}
	} else {
		/*
		 * Cursor is over a server (labwc) surface.  Clear focus
		 * from the focused client (if any, no-op otherwise) and
		 * set the cursor image ourselves when not currently in
		 * a drag operation.
		 */
		wlr_seat_pointer_notify_clear_focus(wlr_seat);
		if (!seat->drag.active) {
			enum lab_cursors cursor = cursor_get_from_ssd(ctx->type);
			if (ctx->view && ctx->view->shaded && cursor > LAB_CURSOR_GRAB) {
				/* Prevent resize cursor on borders for shaded SSD */
				cursor = LAB_CURSOR_DEFAULT;
			}
			cursor_set(seat, cursor);
		}
	}
	return false;
}

uint32_t
cursor_get_resize_edges(struct wlr_cursor *cursor, struct cursor_context *ctx)
{
	uint32_t resize_edges = ssd_resize_edges(ctx->type);
	if (ctx->view && !resize_edges) {
		struct wlr_box box = ctx->view->current;
		resize_edges |=
			(int)cursor->x < box.x + box.width / 2 ?
				WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
		resize_edges |=
			(int)cursor->y < box.y + box.height / 2 ?
				WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
	}
	return resize_edges;
}

bool
cursor_process_motion(struct server *server, uint32_t time, double *sx, double *sy)
{
	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->input_mode == LAB_INPUT_STATE_MOVE) {
		process_cursor_move(server, time);
		return false;
	} else if (server->input_mode == LAB_INPUT_STATE_RESIZE) {
		process_cursor_resize(server, time);
		return false;
	}

	/* Otherwise, find view under the pointer and send the event along */
	struct cursor_context ctx = get_cursor_context(server);
	struct seat *seat = &server->seat;

	if (ctx.type == LAB_SSD_MENU) {
		menu_process_cursor_motion(ctx.node);
		cursor_set(&server->seat, LAB_CURSOR_DEFAULT);
		return false;
	}

	if (seat->drag.active) {
		dnd_icons_move(seat, seat->cursor->x, seat->cursor->y);
	}

	struct mousebind *mousebind;
	wl_list_for_each(mousebind, &rc.mousebinds, link) {
		if (ctx.type == LAB_SSD_CLIENT
				&& view_inhibits_actions(ctx.view, &mousebind->actions)) {
			continue;
		}
		if (mousebind->mouse_event == MOUSE_ACTION_DRAG
				&& mousebind->pressed_in_context) {
			/*
			 * Use view and resize edges from the press
			 * event (not the motion event) to prevent
			 * moving/resizing the wrong view
			 */
			mousebind->pressed_in_context = false;
			actions_run(seat->pressed.view, server,
				&mousebind->actions, &seat->pressed);
		}
	}

	struct wlr_surface *old_focused_surface =
		seat->seat->pointer_state.focused_surface;

	bool notify = cursor_update_common(server, &ctx,
		/* cursor_has_moved */ true, sx, sy);

	struct wlr_surface *new_focused_surface =
		seat->seat->pointer_state.focused_surface;

	if (rc.focus_follow_mouse && new_focused_surface
			&& old_focused_surface != new_focused_surface) {
		/*
		 * If followMouse=yes, update the keyboard focus when the
		 * cursor enters a surface
		 */
		desktop_focus_view_or_surface(seat,
			view_from_wlr_surface(new_focused_surface),
			new_focused_surface, rc.raise_on_focus);
	}

	return notify;
}

static void
_cursor_update_focus(struct server *server)
{
	/* Focus surface under cursor if it isn't already focused */
	struct cursor_context ctx = get_cursor_context(server);

	if ((ctx.view || ctx.surface) && rc.focus_follow_mouse
			&& !rc.focus_follow_mouse_requires_movement) {
		/*
		 * Always focus the surface below the cursor when
		 * followMouse=yes and followMouseRequiresMovement=no.
		 */
		desktop_focus_view_or_surface(&server->seat, ctx.view,
			ctx.surface, rc.raise_on_focus);
	}

	double sx, sy;
	cursor_update_common(server, &ctx, /*cursor_has_moved*/ false, &sx, &sy);
}

void
cursor_update_focus(struct server *server)
{
	/* Prevent recursion via view_move_to_front() */
	static bool updating_focus = false;
	if (!updating_focus) {
		updating_focus = true;
		_cursor_update_focus(server);
		updating_focus = false;
	}
}

static void
warp_cursor_to_constraint_hint(struct seat *seat,
		struct wlr_pointer_constraint_v1 *constraint)
{
	if (!seat->server->active_view) {
		return;
	}

	if (constraint->current.committed
			& WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
		double sx = constraint->current.cursor_hint.x;
		double sy = constraint->current.cursor_hint.y;
		wlr_cursor_warp(seat->cursor, NULL,
			seat->server->active_view->current.x + sx,
			seat->server->active_view->current.y + sy);

		/* Make sure we are not sending unnecessary surface movements */
		wlr_seat_pointer_warp(seat->seat, sx, sy);
	}
}

static void
handle_constraint_commit(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(listener, seat, constraint_commit);
	struct wlr_pointer_constraint_v1 *constraint = seat->current_constraint;
	/* Prevents unused variable warning when compiled without asserts */
	(void)constraint;
	assert(constraint->surface = data);
}

static void
handle_constraint_destroy(struct wl_listener *listener, void *data)
{
	struct constraint *constraint = wl_container_of(listener, constraint,
		destroy);
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	struct seat *seat = constraint->seat;

	wl_list_remove(&constraint->destroy.link);
	if (seat->current_constraint == wlr_constraint) {
		warp_cursor_to_constraint_hint(seat, wlr_constraint);

		if (seat->constraint_commit.link.next) {
			wl_list_remove(&seat->constraint_commit.link);
		}
		wl_list_init(&seat->constraint_commit.link);
		seat->current_constraint = NULL;
	}

	free(constraint);
}

void
create_constraint(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	struct server *server = wl_container_of(listener, server,
		new_constraint);
	struct constraint *constraint = znew(*constraint);

	constraint->constraint = wlr_constraint;
	constraint->seat = &server->seat;
	constraint->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);

	struct view *view = server->active_view;
	if (view && view->surface == wlr_constraint->surface) {
		constrain_cursor(server, wlr_constraint);
	}
}

void
constrain_cursor(struct server *server, struct wlr_pointer_constraint_v1
		*constraint)
{
	struct seat *seat = &server->seat;
	if (seat->current_constraint == constraint) {
		return;
	}
	wl_list_remove(&seat->constraint_commit.link);
	if (seat->current_constraint) {
		if (!constraint) {
			warp_cursor_to_constraint_hint(seat, seat->current_constraint);
		}

		wlr_pointer_constraint_v1_send_deactivated(
			seat->current_constraint);
	}

	seat->current_constraint = constraint;

	if (!constraint) {
		wl_list_init(&seat->constraint_commit.link);
		return;
	}

	wlr_pointer_constraint_v1_send_activated(constraint);
	seat->constraint_commit.notify = handle_constraint_commit;
	wl_signal_add(&constraint->surface->events.commit,
		&seat->constraint_commit);
}

static void
apply_constraint(struct seat *seat, struct wlr_pointer *pointer, double *x, double *y)
{
	if (!seat->server->active_view) {
		return;
	}
	if (!seat->current_constraint || pointer->base.type != WLR_INPUT_DEVICE_POINTER) {
		return;
	}
	assert(seat->current_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED);

	double sx = seat->cursor->x;
	double sy = seat->cursor->y;

	sx -= seat->server->active_view->current.x;
	sy -= seat->server->active_view->current.y;

	double sx_confined, sy_confined;
	if (!wlr_region_confine(&seat->current_constraint->region, sx, sy,
			sx + *x, sy + *y, &sx_confined, &sy_confined)) {
		return;
	}

	*x = sx_confined - sx;
	*y = sy_confined - sy;
}

static bool
cursor_locked(struct seat *seat, struct wlr_pointer *pointer)
{
	return seat->current_constraint
		&& pointer->base.type == WLR_INPUT_DEVICE_POINTER
		&& seat->current_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED;
}

static void
preprocess_cursor_motion(struct seat *seat, struct wlr_pointer *pointer,
		uint32_t time_msec, double dx, double dy)
{
	if (cursor_locked(seat, pointer)) {
		return;
	}
	apply_constraint(seat, pointer, &dx, &dy);

	/*
	 * The cursor doesn't move unless we tell it to. The cursor
	 * automatically handles constraining the motion to the output
	 * layout, as well as any special configuration applied for the
	 * specific input device which generated the event. You can pass
	 * NULL for the device if you want to move the cursor around
	 * without any input.
	 */
	wlr_cursor_move(seat->cursor, &pointer->base, dx, dy);
	double sx, sy;
	bool notify = cursor_process_motion(seat->server, time_msec, &sx, &sy);
	if (notify) {
		wlr_seat_pointer_notify_motion(seat->seat, time_msec, sx, sy);
	}
}

static double get_natural_scroll_factor(struct wlr_input_device *wlr_input_device)
{
	if (wlr_input_device_is_libinput(wlr_input_device)) {
		struct libinput_device *libinput_device =
			wlr_libinput_get_device_handle(wlr_input_device);
		if (libinput_device_config_scroll_get_natural_scroll_enabled(libinput_device)) {
			return -1.0;
		}
	}

	return 1.0;
}

static void
handle_motion(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a
	 * _relative_ pointer motion event (i.e. a delta)
	 */
	struct seat *seat = wl_container_of(listener, seat, on_cursor.motion);
	struct server *server = seat->server;
	struct wlr_pointer_motion_event *event = data;
	idle_manager_notify_activity(seat->seat);
	cursor_set_visible(seat, /* visible */ true);

	if (seat->cursor_scroll_wheel_emulation) {
		uint32_t orientation;
		double delta;
		if (fabs(event->delta_x) > fabs(event->delta_y)) {
			orientation = WL_POINTER_AXIS_HORIZONTAL_SCROLL;
			delta = event->delta_x;
		} else {
			orientation = WL_POINTER_AXIS_VERTICAL_SCROLL;
			delta = event->delta_y;
		}

		/*
		 * arbitrary factor that should give reasonable speed
		 * with the default configured scroll factor of 1.0
		 */
		double motion_to_scroll_factor = 0.04;
		double scroll_factor = motion_to_scroll_factor *
			get_natural_scroll_factor(&event->pointer->base);

		/* The delta of a single step for mouse wheel emulation */
		double pointer_axis_step = 15.0;

		cursor_emulate_axis(seat, &event->pointer->base,
			orientation,
			pointer_axis_step * scroll_factor * delta, 0,
			WL_POINTER_AXIS_SOURCE_CONTINUOUS, event->time_msec);
	} else {
		wlr_relative_pointer_manager_v1_send_relative_motion(
			server->relative_pointer_manager,
			seat->seat, (uint64_t)event->time_msec * 1000,
			event->delta_x, event->delta_y, event->unaccel_dx,
			event->unaccel_dy);

		preprocess_cursor_motion(seat, event->pointer,
			event->time_msec, event->delta_x, event->delta_y);
	}
}

static void
handle_motion_absolute(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an
	 * _absolute_ motion event, from 0..1 on each axis. This happens, for
	 * example, when wlroots is running under a Wayland window rather than
	 * KMS+DRM, and you move the mouse over the window. You could enter the
	 * window from any edge, so we have to warp the mouse there. There is
	 * also some hardware which emits these events.
	 */
	struct seat *seat = wl_container_of(listener, seat, on_cursor.motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	idle_manager_notify_activity(seat->seat);
	cursor_set_visible(seat, /* visible */ true);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(seat->cursor,
		&event->pointer->base, event->x, event->y, &lx, &ly);

	double dx = lx - seat->cursor->x;
	double dy = ly - seat->cursor->y;

	wlr_relative_pointer_manager_v1_send_relative_motion(
		seat->server->relative_pointer_manager,
		seat->seat, (uint64_t)event->time_msec * 1000,
		dx, dy, dx, dy);

	preprocess_cursor_motion(seat, event->pointer,
		event->time_msec, dx, dy);
}

static void
process_release_mousebinding(struct server *server,
		struct cursor_context *ctx, uint32_t button)
{
	if (server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER) {
		return;
	}

	struct mousebind *mousebind;
	uint32_t modifiers = keyboard_get_all_modifiers(&server->seat);

	wl_list_for_each(mousebind, &rc.mousebinds, link) {
		if (ctx->type == LAB_SSD_CLIENT
				&& view_inhibits_actions(ctx->view, &mousebind->actions)) {
			continue;
		}
		if (ssd_part_contains(mousebind->context, ctx->type)
				&& mousebind->button == button
				&& modifiers == mousebind->modifiers) {
			switch (mousebind->mouse_event) {
			case MOUSE_ACTION_RELEASE:
				break;
			case MOUSE_ACTION_CLICK:
				if (mousebind->pressed_in_context) {
					break;
				}
				continue;
			default:
				continue;
			}
			actions_run(ctx->view, server, &mousebind->actions, ctx);
		}
	}
}

static bool
is_double_click(long double_click_speed, uint32_t button,
		struct cursor_context *ctx)
{
	static enum ssd_part_type last_type;
	static uint32_t last_button;
	static struct view *last_view;
	static struct timespec last_click;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (now.tv_sec - last_click.tv_sec) * 1000 +
		(now.tv_nsec - last_click.tv_nsec) / 1000000;
	last_click = now;
	if (last_button != button || last_view != ctx->view
			|| last_type != ctx->type) {
		last_button = button;
		last_view = ctx->view;
		last_type = ctx->type;
		return false;
	}
	if (ms < double_click_speed && ms >= 0) {
		/*
		 * End sequence so that third click is not considered a
		 * double-click
		 */
		last_button = 0;
		last_view = NULL;
		last_type = 0;
		return true;
	}
	return false;
}

static bool
process_press_mousebinding(struct server *server, struct cursor_context *ctx,
		uint32_t button)
{
	if (server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER) {
		return false;
	}

	struct mousebind *mousebind;
	bool double_click = is_double_click(rc.doubleclick_time, button, ctx);
	bool consumed_by_frame_context = false;
	uint32_t modifiers = keyboard_get_all_modifiers(&server->seat);

	wl_list_for_each(mousebind, &rc.mousebinds, link) {
		if (ctx->type == LAB_SSD_CLIENT
				&& view_inhibits_actions(ctx->view, &mousebind->actions)) {
			continue;
		}
		if (ssd_part_contains(mousebind->context, ctx->type)
				&& mousebind->button == button
				&& modifiers == mousebind->modifiers) {
			switch (mousebind->mouse_event) {
			case MOUSE_ACTION_DRAG: /* fallthrough */
			case MOUSE_ACTION_CLICK:
				/*
				 * DRAG and CLICK actions will be processed on
				 * the release event, unless the press event is
				 * counted as a DOUBLECLICK.
				 */
				if (!double_click) {
					/* Swallow the press event */
					consumed_by_frame_context |=
						mousebind->context == LAB_SSD_FRAME;
					consumed_by_frame_context |=
						mousebind->context == LAB_SSD_ALL;
					mousebind->pressed_in_context = true;
				}
				continue;
			case MOUSE_ACTION_DOUBLECLICK:
				if (!double_click) {
					continue;
				}
				break;
			case MOUSE_ACTION_PRESS:
				break;
			default:
				continue;
			}
			consumed_by_frame_context |= mousebind->context == LAB_SSD_FRAME;
			consumed_by_frame_context |= mousebind->context == LAB_SSD_ALL;
			actions_run(ctx->view, server, &mousebind->actions, ctx);
		}
	}
	return consumed_by_frame_context;
}

static uint32_t press_msec;

bool
cursor_process_button_press(struct seat *seat, uint32_t button, uint32_t time_msec)
{
	struct server *server = seat->server;
	struct cursor_context ctx = get_cursor_context(server);

	/* Used on next button release to check if it can close menu or select menu item */
	press_msec = time_msec;

	if (ctx.view || ctx.surface) {
		/* Store cursor context for later action processing */
		seat_set_pressed(seat, &ctx);
	}

	if (server->input_mode == LAB_INPUT_STATE_MENU) {
		/*
		 * If menu was already opened on press, set a very small value
		 * so subsequent release always closes menu or selects menu item.
		 */
		press_msec = 0;
		lab_set_add(&seat->bound_buttons, button);
		return false;
	}

	/*
	 * On press, set focus to a non-view surface that wants it.
	 * Action processing does not run for these surfaces and thus
	 * the Focus action (used for normal views) does not work.
	 */
	if (ctx.type == LAB_SSD_LAYER_SURFACE) {
		wlr_log(WLR_DEBUG, "press on layer-surface");
		struct wlr_layer_surface_v1 *layer =
			wlr_layer_surface_v1_try_from_wlr_surface(ctx.surface);
		if (layer && layer->current.keyboard_interactive) {
			layer_try_set_focus(seat, layer);
		}
	} else if (ctx.type == LAB_SSD_LAYER_SUBSURFACE) {
		wlr_log(WLR_DEBUG, "press on layer-subsurface");
		struct wlr_layer_surface_v1 *layer =
			subsurface_parent_layer(ctx.surface);
		if (layer && layer->current.keyboard_interactive) {
			layer_try_set_focus(seat, layer);
		}
#ifdef HAVE_XWAYLAND
	} else if (ctx.type == LAB_SSD_UNMANAGED) {
		desktop_focus_view_or_surface(seat, NULL, ctx.surface,
			/*raise*/ false);
#endif
	}

	if (ctx.type != LAB_SSD_CLIENT && ctx.type != LAB_SSD_LAYER_SUBSURFACE
			&& wlr_seat_pointer_has_grab(seat->seat)) {
		/*
		 * If we have an active popup grab (an open popup) we want to
		 * cancel that grab whenever the user presses on anything that
		 * is not the client itself, for example the desktop or any
		 * part of the server side decoration.
		 *
		 * Note: This does not work for XWayland clients
		 */
		wlr_seat_pointer_end_grab(seat->seat);
		lab_set_add(&seat->bound_buttons, button);
		return false;
	}

	/* Bindings to the Frame context swallow mouse events if activated */
	bool consumed_by_frame_context =
		process_press_mousebinding(server, &ctx, button);

	if (ctx.surface && !consumed_by_frame_context) {
		/* Notify client with pointer focus of button press */
		return true;
	}

	lab_set_add(&seat->bound_buttons, button);
	return false;
}

bool
cursor_process_button_release(struct seat *seat, uint32_t button,
		uint32_t time_msec)
{
	struct server *server = seat->server;
	struct cursor_context ctx = get_cursor_context(server);
	struct wlr_surface *pressed_surface = seat->pressed.surface;

	/* Always notify button release event when it's not bound */
	const bool notify = !lab_set_contains(&seat->bound_buttons, button);

	seat_reset_pressed(seat);

	if (server->input_mode == LAB_INPUT_STATE_MENU) {
		/* TODO: take into account overflow of time_msec */
		if (time_msec - press_msec > rc.menu_ignore_button_release_period) {
			if (ctx.type == LAB_SSD_MENU) {
				menu_call_selected_actions(server);
			} else {
				menu_close_root(server);
				cursor_update_focus(server);
			}
		}
		return notify;
	}

	if (server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return notify;
	}

	if (pressed_surface && ctx.surface != pressed_surface) {
		/*
		 * Button released but originally pressed over a different surface.
		 * Just send the release event to the still focused surface.
		 */
		return notify;
	}

	process_release_mousebinding(server, &ctx, button);

	return notify;
}

bool
cursor_finish_button_release(struct seat *seat, uint32_t button)
{
	struct server *server = seat->server;

	/* Clear "pressed" status for all bindings of this mouse button */
	struct mousebind *mousebind;
	wl_list_for_each(mousebind, &rc.mousebinds, link) {
		if (mousebind->button == button) {
			mousebind->pressed_in_context = false;
		}
	}

	lab_set_remove(&seat->bound_buttons, button);

	if (server->input_mode == LAB_INPUT_STATE_MOVE
			|| server->input_mode == LAB_INPUT_STATE_RESIZE) {
		if (resize_outlines_enabled(server->grabbed_view)) {
			resize_outlines_finish(server->grabbed_view);
		}
		/* Exit interactive move/resize mode */
		interactive_finish(server->grabbed_view);
		return true;
	}

	return false;
}

static void
handle_button(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a button
	 * event.
	 */
	struct seat *seat = wl_container_of(listener, seat, on_cursor.button);
	struct wlr_pointer_button_event *event = data;
	idle_manager_notify_activity(seat->seat);
	cursor_set_visible(seat, /* visible */ true);

	bool notify;
	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		notify = cursor_process_button_press(seat, event->button,
			event->time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(seat->seat, event->time_msec,
				event->button, event->state);
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		notify = cursor_process_button_release(seat, event->button,
			event->time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(seat->seat, event->time_msec,
				event->button, event->state);
		}
		cursor_finish_button_release(seat, event->button);
		break;
	}
}

struct scroll_info {
	int direction;
	bool run_action;
};

static struct scroll_info
compare_delta(double delta, double delta_discrete, struct accumulated_scroll *accum)
{
	struct scroll_info info = {0};

	if (delta_discrete) {
		/* mice */
		info.direction = delta_discrete > 0 ? 1 : -1;
		accum->delta_discrete += delta_discrete;
		/*
		 * Non-hi-res mice produce delta_discrete of ±120 for every
		 * "click", so it always triggers actions. But for hi-res mice
		 * that produce smaller delta_discrete, we accumulate it and
		 * run actions after it exceeds 120(= 1 click).
		 */
		if (fabs(accum->delta_discrete) >= 120.0) {
			accum->delta_discrete = fmod(accum->delta_discrete, 120.0);
			info.run_action = true;
		}
	} else {
		/* 2-finger scrolling on touchpads */
		if (delta == 0) {
			/* delta=0 marks the end of a scroll */
			accum->delta = 0;
			return info;
		}
		info.direction = delta > 0 ? 1 : -1;
		accum->delta += delta;
		/*
		 * The threshold of 10 is inherited from various historic
		 * projects including weston.
		 *
		 * For historic context, see:
		 * https://lists.freedesktop.org/archives/wayland-devel/2019-April/040377.html
		 */
		if (fabs(accum->delta) >= 10.0) {
			accum->delta = fmod(accum->delta, 10.0);
			info.run_action = true;
		}
	}

	return info;
}

static bool
process_cursor_axis(struct server *server, enum wl_pointer_axis orientation,
		double delta, double delta_discrete)
{
	struct cursor_context ctx = get_cursor_context(server);
	uint32_t modifiers = keyboard_get_all_modifiers(&server->seat);

	enum direction direction = LAB_DIRECTION_INVALID;
	struct scroll_info info = compare_delta(delta, delta_discrete,
		&server->seat.accumulated_scrolls[orientation]);

	if (orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
		if (info.direction < 0) {
			direction = LAB_DIRECTION_LEFT;
		} else if (info.direction > 0) {
			direction = LAB_DIRECTION_RIGHT;
		}
	} else if (orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		if (info.direction < 0) {
			direction = LAB_DIRECTION_UP;
		} else if (info.direction > 0) {
			direction = LAB_DIRECTION_DOWN;
		}
	} else {
		wlr_log(WLR_DEBUG, "Failed to handle cursor axis event");
	}

	bool handled = false;
	if (direction != LAB_DIRECTION_INVALID) {
		struct mousebind *mousebind;
		wl_list_for_each(mousebind, &rc.mousebinds, link) {
			if (ctx.type == LAB_SSD_CLIENT
					&& view_inhibits_actions(ctx.view, &mousebind->actions)) {
				continue;
			}
			if (ssd_part_contains(mousebind->context, ctx.type)
					&& mousebind->direction == direction
					&& modifiers == mousebind->modifiers
					&& mousebind->mouse_event == MOUSE_ACTION_SCROLL) {
				handled = true;
				/*
				 * Action may not be executed if the accumulated scroll delta
				 * on touchpads or hi-res mice doesn't exceed the threshold
				 */
				if (info.run_action) {
					actions_run(ctx.view, server, &mousebind->actions, &ctx);
				}
			}
		}
	}

	/* Bindings swallow mouse events if activated */
	if (ctx.surface && !handled) {
		/* Make sure we are sending the events to the surface under the cursor */
		double sx, sy;
		cursor_update_common(server, &ctx, /*cursor_has_moved*/ false, &sx, &sy);

		return true;
	}

	return false;
}

static void
handle_axis(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an axis
	 * event, for example when you move the scroll wheel.
	 */
	struct seat *seat = wl_container_of(listener, seat, on_cursor.axis);
	struct server *server = seat->server;
	struct wlr_pointer_axis_event *event = data;
	idle_manager_notify_activity(seat->seat);
	cursor_set_visible(seat, /* visible */ true);

	/* input->scroll_factor is set for pointer/touch devices */
	assert(event->pointer->base.type == WLR_INPUT_DEVICE_POINTER
		|| event->pointer->base.type == WLR_INPUT_DEVICE_TOUCH);
	struct input *input = event->pointer->base.data;
	double scroll_factor = input->scroll_factor;

	bool notify = process_cursor_axis(server, event->orientation,
		event->delta, event->delta_discrete);

	if (notify) {
		/* Notify the client with pointer focus of the axis event. */
		wlr_seat_pointer_notify_axis(seat->seat, event->time_msec,
			event->orientation, scroll_factor * event->delta,
			round(scroll_factor * event->delta_discrete),
			event->source, event->relative_direction);
	}
}

static void
handle_frame(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen
	 * at the same time, in which case a frame event won't be sent in
	 * between.
	 */
	struct seat *seat = wl_container_of(listener, seat, on_cursor.frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat->seat);
}

void
cursor_emulate_axis(struct seat *seat, struct wlr_input_device *device,
		enum wl_pointer_axis orientation, double delta, double delta_discrete,
		enum wl_pointer_axis_source source, uint32_t time_msec)
{
	struct server *server = seat->server;
	struct input *input = device->data;

	double scroll_factor = 1.0;
	/* input->scroll_factor is set for pointer/touch devices */
	if (device->type == WLR_INPUT_DEVICE_POINTER
			|| device->type == WLR_INPUT_DEVICE_TOUCH) {
		scroll_factor = input->scroll_factor;
	}

	bool notify = process_cursor_axis(server, orientation, delta, delta_discrete);
	if (notify) {
		/* Notify the client with pointer focus of the axis event. */
		wlr_seat_pointer_notify_axis(seat->seat, time_msec,
			orientation, scroll_factor * delta,
			round(scroll_factor * delta_discrete),
			source, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
	}
	wlr_seat_pointer_notify_frame(seat->seat);
}

void
cursor_emulate_move(struct seat *seat, struct wlr_input_device *device,
		double dx, double dy, uint32_t time_msec)
{
	if (!dx && !dy) {
		wlr_log(WLR_DEBUG, "dropping useless cursor_emulate: %.10f,%.10f", dx, dy);
		return;
	}

	wlr_relative_pointer_manager_v1_send_relative_motion(
		seat->server->relative_pointer_manager,
		seat->seat, (uint64_t)time_msec * 1000,
		dx, dy, dx, dy);

	wlr_cursor_move(seat->cursor, device, dx, dy);
	double sx, sy;
	bool notify = cursor_process_motion(seat->server, time_msec, &sx, &sy);
	if (notify) {
		wlr_seat_pointer_notify_motion(seat->seat, time_msec, sx, sy);
	}
	wlr_seat_pointer_notify_frame(seat->seat);
}

void
cursor_emulate_move_absolute(struct seat *seat, struct wlr_input_device *device,
		double x, double y, uint32_t time_msec)
{
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(seat->cursor,
		device, x, y, &lx, &ly);

	double dx = lx - seat->cursor->x;
	double dy = ly - seat->cursor->y;

	cursor_emulate_move(seat, device, dx, dy, time_msec);
}

void
cursor_emulate_button(struct seat *seat, uint32_t button,
		enum wl_pointer_button_state state, uint32_t time_msec)
{
	bool notify;
	switch (state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		notify = cursor_process_button_press(seat, button, time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(seat->seat, time_msec, button, state);
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		notify = cursor_process_button_release(seat, button, time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(seat->seat, time_msec, button, state);
		}
		cursor_finish_button_release(seat, button);
		break;
	}
	wlr_seat_pointer_notify_frame(seat->seat);
}

static void
cursor_load(struct seat *seat)
{
	const char *xcursor_theme = getenv("XCURSOR_THEME");
	const char *xcursor_size = getenv("XCURSOR_SIZE");
	uint32_t size = xcursor_size ? atoi(xcursor_size) : 24;

	if (seat->xcursor_manager) {
		wlr_xcursor_manager_destroy(seat->xcursor_manager);
	}
	seat->xcursor_manager = wlr_xcursor_manager_create(xcursor_theme, size);
	wlr_xcursor_manager_load(seat->xcursor_manager, 1);

	/*
	 * Wlroots provides integrated fallback cursor icons using
	 * old-style X11 cursor names (cursors_x11) and additionally
	 * (since wlroots 0.16.2) aliases them to cursor-spec names
	 * (cursors_xdg).
	 *
	 * However, the aliasing does not include the "grab" cursor
	 * icon which labwc uses when dragging a window. To fix that,
	 * try to get the grab cursor icon from wlroots. If the user
	 * supplied an appropriate cursor theme which includes the
	 * "grab" cursor icon, we will keep using it.
	 *
	 * If no "grab" icon can be found we will fall back to the
	 * old style cursor names and use "grabbing" instead which
	 * is part of the X11 fallbacks and thus always available.
	 *
	 * Shipping the complete alias table for X11 cursor names
	 * (and not just the "grab" cursor alias) makes sure that
	 * this also works for wlroots versions before 0.16.2.
	 *
	 * See the cursor name alias table on the top of this file
	 * for the actual cursor names used.
	 */
	if (wlr_xcursor_manager_get_xcursor(seat->xcursor_manager,
			cursors_xdg[LAB_CURSOR_GRAB], 1)) {
		cursor_names = cursors_xdg;
	} else {
		wlr_log(WLR_INFO,
			"Cursor theme is missing cursor names, using fallback");
		cursor_names = cursors_x11;
	}
}

void
cursor_reload(struct seat *seat)
{
	cursor_load(seat);
#if HAVE_XWAYLAND
	xwayland_reset_cursor(seat->server);
#endif
	cursor_update_image(seat);
}

void
cursor_init(struct seat *seat)
{
	cursor_load(seat);

	/* Set the initial cursor image so the cursor is visible right away */
	cursor_set(seat, LAB_CURSOR_DEFAULT);

	dnd_init(seat);

	CONNECT_SIGNAL(seat->cursor, &seat->on_cursor, motion);
	CONNECT_SIGNAL(seat->cursor, &seat->on_cursor, motion_absolute);
	CONNECT_SIGNAL(seat->cursor, &seat->on_cursor, button);
	CONNECT_SIGNAL(seat->cursor, &seat->on_cursor, axis);
	CONNECT_SIGNAL(seat->cursor, &seat->on_cursor, frame);

	gestures_init(seat);
	touch_init(seat);
	tablet_init(seat);

	CONNECT_SIGNAL(seat->seat, seat, request_set_cursor);

	struct wlr_cursor_shape_manager_v1 *cursor_shape_manager =
		wlr_cursor_shape_manager_v1_create(seat->server->wl_display,
			LAB_CURSOR_SHAPE_V1_VERSION);
	if (!cursor_shape_manager) {
		wlr_log(WLR_ERROR, "unable to create cursor_shape interface");
		exit(EXIT_FAILURE);
	}

	CONNECT_SIGNAL(cursor_shape_manager, seat, request_set_shape);
	CONNECT_SIGNAL(seat->seat, seat, request_set_selection);
	CONNECT_SIGNAL(seat->seat, seat, request_set_primary_selection);
}

void cursor_finish(struct seat *seat)
{
	wl_list_remove(&seat->on_cursor.motion.link);
	wl_list_remove(&seat->on_cursor.motion_absolute.link);
	wl_list_remove(&seat->on_cursor.button.link);
	wl_list_remove(&seat->on_cursor.axis.link);
	wl_list_remove(&seat->on_cursor.frame.link);

	gestures_finish(seat);
	touch_finish(seat);

	tablet_finish(seat);

	wl_list_remove(&seat->request_set_cursor.link);
	wl_list_remove(&seat->request_set_shape.link);
	wl_list_remove(&seat->request_set_selection.link);
	wl_list_remove(&seat->request_set_primary_selection.link);

	wlr_xcursor_manager_destroy(seat->xcursor_manager);
	wlr_cursor_destroy(seat->cursor);

	dnd_finish(seat);
}
