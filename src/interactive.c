// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include "edges.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "regions.h"
#include "resize-indicator.h"
#include "snap.h"
#include "view.h"
#include "window-rules.h"

static int
max_move_scale(double pos_cursor, double pos_current,
	double size_current, double size_orig)
{
	double anchor_frac = (pos_cursor - pos_current) / size_current;
	int pos_new = pos_cursor - (size_orig * anchor_frac);
	if (pos_new < pos_current) {
		/* Clamp by using the old offsets of the maximized window */
		pos_new = pos_current;
	}
	return pos_new;
}

void
interactive_begin(struct view *view, enum input_mode mode, uint32_t edges)
{
	/*
	 * This function sets up an interactive move or resize operation, where
	 * the compositor stops propagating pointer events to clients and
	 * instead consumes them itself, to move or resize windows.
	 */
	struct server *server = view->server;
	struct seat *seat = &server->seat;
	struct wlr_box geometry = view->current;

	if (server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	/* Prevent moving/resizing fixed-position and panel-like views */
	if (window_rules_get_property(view, "fixedPosition") == LAB_PROP_TRUE
			|| view_has_strut_partial(view)) {
		return;
	}

	switch (mode) {
	case LAB_INPUT_STATE_MOVE:
		if (view->fullscreen) {
			/**
			 * We don't allow moving fullscreen windows.
			 *
			 * If you think there is a good reason to allow
			 * it, feel free to open an issue explaining
			 * your use-case.
			 */
			return;
		}
		if (!view_is_floating(view)) {
			/*
			 * Un-maximize, unshade and restore natural
			 * width/height.
			 * Don't reset tiled state yet since we may want
			 * to keep it (in the snap-to-maximize case).
			 */
			geometry = view->natural_geometry;
			geometry.x = max_move_scale(seat->cursor->x,
				view->current.x, view->current.width,
				geometry.width);
			geometry.y = max_move_scale(seat->cursor->y,
				view->current.y, view->current.height,
				geometry.height);

			view_set_shade(view, false);
			view_set_untiled(view);
			view_restore_to(view, geometry);
		} else {
			/* Store natural geometry at start of move */
			view_store_natural_geometry(view);
			view_invalidate_last_layout_geometry(view);
		}

		/* Prevent region snapping when just moving via A-Left mousebind */
		struct wlr_keyboard *keyboard = &seat->keyboard_group->keyboard;
		seat->region_prevent_snap = keyboard_any_modifiers_pressed(keyboard);

		cursor_set(seat, LAB_CURSOR_GRAB);
		break;
	case LAB_INPUT_STATE_RESIZE:
		if (view->shaded || view->fullscreen ||
				view->maximized == VIEW_AXIS_BOTH) {
			/*
			 * We don't allow resizing while shaded,
			 * fullscreen or maximized in both directions.
			 */
			return;
		}

		/*
		 * Resizing overrides any attempt to restore window
		 * geometries altered by layout changes.
		 */
		view_invalidate_last_layout_geometry(view);

		/*
		 * If tiled or maximized in only one direction, reset
		 * tiled/maximized state but keep the same geometry as
		 * the starting point for the resize.
		 */
		view_set_untiled(view);
		view_restore_to(view, view->pending);
		cursor_set(seat, cursor_get_from_edge(edges));
		break;
	default:
		/* Should not be reached */
		return;
	}

	server->input_mode = mode;
	server->grabbed_view = view;
	/* Remember view and cursor positions at start of move/resize */
	server->grab_x = seat->cursor->x;
	server->grab_y = seat->cursor->y;
	server->grab_box = geometry;
	server->resize_edges = edges;
	if (rc.resize_indicator) {
		resize_indicator_show(view);
	}
	if (rc.window_edge_strength) {
		edges_calculate_visibility(server, view);
	}
}

struct edge_snap_info
get_edge_snap_info(struct seat *seat)
{
	struct output *output = output_nearest_to_cursor(seat->server);
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "output at cursor is unusable");
		return (struct edge_snap_info){};
	}

	/* Translate into output local coordinates */
	double cursor_x = seat->cursor->x;
	double cursor_y = seat->cursor->y;
	wlr_output_layout_output_coords(seat->server->output_layout,
		output->wlr_output, &cursor_x, &cursor_y);

	struct wlr_box *area = &output->usable_area;
	int corner_range_x = area->width * 0.1;
	int corner_range_y = area->height * 0.1;

	bool is_left = false;
	bool is_right = false;
	bool is_up = false;
	bool is_down = false;
	if (rc.snap_edge_range[VIEW_EDGE_LEFT]) {
		is_left = cursor_x <= area->x
				+ rc.snap_edge_range[VIEW_EDGE_LEFT];
	}
	if (rc.snap_edge_range[VIEW_EDGE_RIGHT]) {
		is_right = cursor_x >= area->x + area->width
				- rc.snap_edge_range[VIEW_EDGE_RIGHT];
	}
	if (rc.snap_edge_range[VIEW_EDGE_UP]) {
		is_up = cursor_y <= area->y
				+ rc.snap_edge_range[VIEW_EDGE_UP];
	}
	if (rc.snap_edge_range[VIEW_EDGE_DOWN]) {
		is_down = cursor_y >= area->y + area->height
				- rc.snap_edge_range[VIEW_EDGE_DOWN];
	}

	bool is_far_left  = cursor_x <= area->x + corner_range_x;
	bool is_far_right = cursor_x >= area->x + area->width - corner_range_x;
	bool is_far_up    = cursor_y <= area->y + corner_range_y;
	bool is_far_down  = cursor_y >= area->y + area->height - corner_range_y;

	enum wlr_direction primary_dir;
	enum view_tiled_state tiled_state;
	if (is_left) {
		primary_dir = WLR_DIRECTION_LEFT;
		if (is_far_up) {
			tiled_state = VIEW_TILED_UPLEFT;
		} else if (is_far_down) {
			tiled_state = VIEW_TILED_DOWNLEFT;
		} else {
			tiled_state = VIEW_TILED_LEFT;
		}
	} else if (is_right) {
		primary_dir = WLR_DIRECTION_RIGHT;
		if (is_far_up) {
			tiled_state = VIEW_TILED_UPRIGHT;
		} else if (is_far_down) {
			tiled_state = VIEW_TILED_DOWNRIGHT;
		} else {
			tiled_state = VIEW_TILED_RIGHT;
		}
	} else if (is_up) {
		primary_dir = WLR_DIRECTION_UP;
		if (is_far_left) {
			tiled_state = VIEW_TILED_UPLEFT;
		} else if (is_far_right) {
			tiled_state = VIEW_TILED_UPRIGHT;
		} else {
			tiled_state = VIEW_TILED_UP;
		}
	} else if (is_down) {
		primary_dir = WLR_DIRECTION_DOWN;
		if (is_far_left) {
			tiled_state = VIEW_TILED_DOWNLEFT;
		} else if (is_far_right) {
			tiled_state = VIEW_TILED_DOWNRIGHT;
		} else {
			tiled_state = VIEW_TILED_DOWN;
		}
	} else {
		/* Not close to any edge */
		return (struct edge_snap_info){};
	}

	if (rc.snap_top_maximize && tiled_state == VIEW_TILED_UP) {
		tiled_state = VIEW_TILED_CENTER;
	}

	return (struct edge_snap_info) {
		.tiled_state = tiled_state,
		.primary_direction = primary_dir,
		.output = output,
	};
}

/* Returns true if view was snapped to any edge */
static bool
snap_to_edge(struct view *view)
{
	struct edge_snap_info info = get_edge_snap_info(&view->server->seat);
	if (info.tiled_state == VIEW_TILED_NONE) {
		return false;
	}

	/*
	 * Don't store natural geometry here (it was
	 * stored already in interactive_begin())
	 */
	if (info.tiled_state == VIEW_TILED_CENTER) {
		/* <topMaximize> */
		view_maximize(view, VIEW_AXIS_BOTH,
			/*store_natural_geometry*/ false);
	} else {
		view_snap_to_edge(view, info.tiled_state, info.output,
			/*store_natural_geometry*/ false);
	}

	return true;
}

static bool
snap_to_region(struct view *view)
{
	if (!regions_should_snap(view->server)) {
		return false;
	}

	struct region *region = regions_from_cursor(view->server);
	if (region) {
		view_snap_to_region(view, region,
			/*store_natural_geometry*/ false);
		return true;
	}
	return false;
}

void
interactive_finish(struct view *view)
{
	if (view->server->grabbed_view != view) {
		return;
	}

	if (view->server->input_mode == LAB_INPUT_STATE_MOVE) {
		if (!snap_to_region(view)) {
			snap_to_edge(view);
		}
	}

	interactive_cancel(view);
}

/*
 * Cancels interactive move/resize without changing the state of the of
 * the view in any way. This may leave the tiled state inconsistent with
 * the actual geometry of the view.
 */
void
interactive_cancel(struct view *view)
{
	if (view->server->grabbed_view != view) {
		return;
	}

	overlay_hide(&view->server->seat);

	resize_indicator_hide(view);

	view->server->input_mode = LAB_INPUT_STATE_PASSTHROUGH;
	view->server->grabbed_view = NULL;

	/* Update focus/cursor image */
	cursor_update_focus(view->server);
}
