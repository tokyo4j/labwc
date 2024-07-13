// SPDX-License-Identifier: GPL-2.0-only

#include <wlr/types/wlr_scene.h>
#include "common/graphic-helpers.h"
#include "ssd.h"
#include "resize-outlines.h"
#include "labwc.h"

bool
resize_overlay_enabled(struct view *view)
{
	return (bool)view->resize_overlay.rect;
}

void
resize_overlay_update(struct view *view, struct wlr_box new_geo)
{
	struct resize_overlay *overlay = &view->resize_overlay;

	if (!overlay->rect) {
		overlay->rect = overlay_rect_create(
			view->scene_tree, &view->server->theme->resize_overlay);
	}

	struct border margin = ssd_get_margin(view->ssd);
	struct wlr_box box = {
		.x = new_geo.x - margin.left,
		.y = new_geo.y - margin.top,
		.width = new_geo.width + margin.left + margin.right,
		.height = new_geo.height + margin.top + margin.bottom,
	};
	overlay_rect_set_size(overlay->rect, box.width, box.height);
	wlr_scene_node_set_position(&overlay->rect->tree->node,
		box.x - view->current.x, box.y - view->current.y);
	wlr_scene_node_set_enabled(
		&view->resize_overlay.rect->tree->node, true);

	overlay->view_geo = new_geo;

	resize_indicator_update(view);
}

void
resize_overlay_finish(struct view *view)
{
	view_move_resize(view, view->resize_overlay.view_geo);
	wlr_scene_node_set_enabled(
		&view->resize_overlay.rect->tree->node, false);
}
