// SPDX-License-Identifier: GPL-2.0-only

#include <wlr/types/wlr_scene.h>
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "ssd.h"
#include "resize-preview.h"
#include "labwc.h"

bool
resize_preview_enabled(struct view *view)
{
	return !wlr_box_empty(&view->resize_preview.view_geo);
}

static void
update_fillters(struct view *view, int width, int height)
{
	struct resize_preview *preview = &view->resize_preview;
	struct wlr_box *current = &view->current;
	/* TODO: sync with border color? */
	const float color[4] = {1.0, 1.0, 1.0, 1.0};

	bool need_filler_right = current->width < width;
	bool need_filler_bottom = current->height < height;

	if (need_filler_right || need_filler_bottom) {
		if (!preview->tree) {
			preview->tree = wlr_scene_tree_create(view->scene_tree);
		}
		wlr_scene_node_set_enabled(&preview->tree->node, true);
	}

	if (need_filler_right) {
		int rect_width = width - current->width;
		int rect_height = MIN(current->height, height);
		if (!preview->filler_right) {
			preview->filler_right = wlr_scene_rect_create(
				preview->tree, rect_width, rect_height, color);
		}
		wlr_scene_node_set_enabled(&preview->filler_right->node, true);
		wlr_scene_node_set_position(&preview->filler_right->node,
			current->width, 0);
		wlr_scene_rect_set_size(preview->filler_right,
			rect_width, rect_height);
	} else {
		if (preview->filler_right) {
			wlr_scene_node_set_enabled(&preview->filler_right->node, false);
		}
	}

	if (need_filler_bottom) {
		int rect_width = width;
		int rect_height = height - current->height;
		if (!preview->filler_bottom) {
			preview->filler_bottom = wlr_scene_rect_create(
				preview->tree, rect_width, rect_height, color);
		}
		wlr_scene_node_set_enabled(&preview->filler_bottom->node, true);
		wlr_scene_node_set_position(&preview->filler_bottom->node,
			0, current->height);
		wlr_scene_rect_set_size(preview->filler_bottom,
			rect_width, rect_height);
	} else {
		if (preview->filler_bottom) {
			wlr_scene_node_set_enabled(&preview->filler_bottom->node, false);
		}
	}
}

void
resize_preview_update(struct view *view, struct wlr_box new_geo)
{
	struct resize_preview *outlines = &view->resize_preview;

	wlr_scene_node_set_position(&view->scene_tree->node,
		new_geo.x, new_geo.y);

	update_fillters(view, new_geo.width, new_geo.height);
	wlr_scene_subsurface_tree_set_clip(view->scene_node, &(struct wlr_box){
		.width = new_geo.width,
		.height = new_geo.height,
	});

	outlines->view_geo = new_geo;

	/* FIXME: temporarily overwriting view->current seems too hacky */
	struct wlr_box tmp_current = view->current;
	view->current = new_geo;
	ssd_update_geometry(view->ssd);
	view->current = tmp_current;

	resize_indicator_update(view);
}

static int
handle_timeout(void *data)
{
	struct view *view = data;
	struct resize_preview *preview = &view->resize_preview;

	if (preview->tree) {
		wlr_scene_node_set_enabled(
			&preview->tree->node, false);
	}
	wlr_scene_subsurface_tree_set_clip(view->scene_node, NULL);
	return 0;
}

void
resize_preview_cancel_timer(struct view *view)
{
	handle_timeout(view);
	if (view->resize_preview.timer) {
		wl_event_source_timer_update(view->resize_preview.timer, 0);
	}
}

void
resize_preview_finish(struct view *view)
{
	struct resize_preview *preview = &view->resize_preview;

	view_move_resize(view, preview->view_geo);
	preview->view_geo = (struct wlr_box){0};

	if (!preview->timer) {
		preview->timer = wl_event_loop_add_timer(
			view->server->wl_event_loop, handle_timeout, view);
	}
	wl_event_source_timer_update(preview->timer, 100);
}
