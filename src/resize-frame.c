// SPDX-License-Identifier: GPL-2.0-only

#include <wlr/types/wlr_scene.h>
#include "common/graphic-helpers.h"
#include "config/rcxml.h"
#include "ssd.h"
#include "resize-frame.h"
#include "labwc.h"

void
resize_frame_update(struct view *view, struct wlr_box new_geo)
{
	struct resize_frame *resize_frame = &view->resize_frame;

	if (!resize_frame->rect) {
		float *colors[3] = {
			view->server->theme->resize_frame_color[0],
			view->server->theme->resize_frame_color[1],
			view->server->theme->resize_frame_color[2],
		};
		int width = view->server->theme->resize_frame_thickness;
		resize_frame->rect = multi_rect_create(
			view->scene_tree, colors, width);
	}

	struct border margin = ssd_get_margin(view->ssd);
	struct wlr_box box = {
		.x = new_geo.x - margin.left,
		.y = new_geo.y - margin.top,
		.width = new_geo.width + margin.left + margin.right,
		.height = new_geo.height + margin.top + margin.bottom,
	};
	multi_rect_set_size(resize_frame->rect, box.width, box.height);
	wlr_scene_node_set_position(&resize_frame->rect->tree->node,
		box.x - view->current.x, box.y - view->current.y);
	wlr_scene_node_set_enabled(
		&view->resize_frame.rect->tree->node, true);

	resize_frame->view_geo = new_geo;
}

void
resize_frame_finish(struct view *view)
{
	struct resize_frame *resize_frame = &view->resize_frame;
	if (!resize_frame->rect || !resize_frame->rect->tree->node.enabled) {
		return;
	}

	view_move_resize(view, view->resize_frame.view_geo);
	wlr_scene_node_set_enabled(
		&view->resize_frame.rect->tree->node, false);
}
