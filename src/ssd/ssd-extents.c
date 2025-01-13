// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <pixman.h>
#include "common/macros.h"
#include "common/scene-helpers.h"
#include "labwc.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

void
ssd_extents_create(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = view->server->theme;
	int extended_area = SSD_EXTENDED_AREA;

	ssd->extents.tree = wlr_scene_tree_create(ssd->tree);
	struct wlr_scene_tree *parent = ssd->extents.tree;
	if (view->fullscreen || view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&parent->node, false);
	}
	wlr_scene_node_set_position(&parent->node,
		-(theme->border_width + extended_area),
		-(ssd->titlebar.height + theme->border_width + extended_area));

	float invisible[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	/* Top */
	ssd->extents.topleft =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.topleft->node,
		LAB_SSD_PART_CORNER_TOP_LEFT);

	ssd->extents.top =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.top->node,
		LAB_SSD_PART_TOP);

	ssd->extents.topright =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.topright->node,
		LAB_SSD_PART_CORNER_TOP_RIGHT);

	/* Sides */
	ssd->extents.left =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.left->node,
		LAB_SSD_PART_LEFT);

	ssd->extents.right =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.right->node,
		LAB_SSD_PART_RIGHT);

	/* Bottom */
	ssd->extents.bottomleft =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.bottomleft->node,
		LAB_SSD_PART_CORNER_BOTTOM_LEFT);

	ssd->extents.bottom =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.bottom->node,
		LAB_SSD_PART_BOTTOM);

	ssd->extents.bottomright =
		wlr_scene_rect_create(ssd->extents.tree, 0, 0, invisible);
	ssd_node_descriptor_create(&ssd->extents.bottomright->node,
		LAB_SSD_PART_CORNER_BOTTOM_RIGHT);

	/* Initial manual update to keep X11 applications happy */
	ssd_extents_update(ssd);
}

static void
update_extent_part(int base_x, int base_y, pixman_region32_t *usable,
		struct wlr_scene_rect *rect, struct wlr_box target)
{
	/* Get layout geometry of what the part *should* be */
	struct wlr_box part_box = {
		.x = base_x + target.x,
		.y = base_y + target.y,
		.width = target.width,
		.height = target.height,
	};

	pixman_region32_t intersection;
	pixman_region32_init(&intersection);

	/* Constrain part to output->usable_area */
	pixman_region32_clear(&intersection);
	pixman_region32_intersect_rect(&intersection, usable,
		part_box.x, part_box.y, part_box.width, part_box.height);
	int nrects;
	const pixman_box32_t *inter_rects =
		pixman_region32_rectangles(&intersection, &nrects);

	if (nrects == 0) {
		/* Not visible */
		wlr_scene_node_set_enabled(&rect->node, false);
		goto out;
	}

	/*
	 * For each edge, the invisible grab area is resized
	 * to not cover layer-shell clients such as panels.
	 * However, only one resize operation is used per edge,
	 * so if a window is in the unlikely position that it
	 * is near a panel but also overspills onto another screen,
	 * the invisible grab-area on the other screen would be
	 * smaller than would normally be the case.
	 *
	 * Thus only use the first intersecting rect, this is
	 * a compromise as it doesn't require us to create
	 * multiple scene rects for a given extent edge
	 * and still works in 95% of the cases.
	 */
	struct wlr_box result_box = {
		.x = inter_rects[0].x1,
		.y = inter_rects[0].y1,
		.width = inter_rects[0].x2 - inter_rects[0].x1,
		.height = inter_rects[0].y2 - inter_rects[0].y1
	};

	wlr_scene_node_set_enabled(&rect->node, true);

	if (part_box.width != result_box.width
			|| part_box.height != result_box.height) {
		/* Partly visible */
		wlr_scene_rect_set_size(rect, result_box.width,
			result_box.height);
		wlr_scene_node_set_position(&rect->node,
			target.x + (result_box.x - part_box.x),
			target.y + (result_box.y - part_box.y));
	} else {
		/* Fully visible */
		wlr_scene_node_set_position(&rect->node, target.x, target.y);
		wlr_scene_rect_set_size(rect, target.width, target.height);
	}
out:
	pixman_region32_fini(&intersection);
}

void
ssd_extents_update(struct ssd *ssd)
{
	struct view *view = ssd->view;
	if (view->fullscreen || view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&ssd->extents.tree->node, false);
		return;
	}
	if (!ssd->extents.tree->node.enabled) {
		wlr_scene_node_set_enabled(&ssd->extents.tree->node, true);
	}

	if (!view->output) {
		return;
	}

	struct theme *theme = view->server->theme;

	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_height = height + theme->border_width * 2 + ssd->titlebar.height;
	int full_width = width + 2 * theme->border_width;
	int extended_area = SSD_EXTENDED_AREA;
	int corner_width = ssd_get_corner_width();
	int corner_size = extended_area + theme->border_width +
		MIN(corner_width, width) / 2;
	int side_width = full_width + extended_area * 2 - corner_size * 2;
	int side_height = full_height + extended_area * 2 - corner_size * 2;

	/* Make sure we update the y offset based on titlebar shown / hidden */
	wlr_scene_node_set_position(&ssd->extents.tree->node,
		-(theme->border_width + extended_area),
		-(ssd->titlebar.height + theme->border_width + extended_area));

	/*
	 * Convert all output usable areas that the
	 * view is currently on into a pixman region
	 */
	pixman_region32_t usable;
	pixman_region32_init(&usable);
	struct output *output;
	wl_list_for_each(output, &view->server->outputs, link) {
		if (!view_on_output(view, output)) {
			continue;
		}
		struct wlr_box usable_area =
			output_usable_area_in_layout_coords(output);
		pixman_region32_union_rect(&usable, &usable, usable_area.x,
			usable_area.y, usable_area.width, usable_area.height);
	}

	/* Remember base layout coordinates */
	int base_x, base_y;
	wlr_scene_node_coords(&ssd->extents.tree->node, &base_x, &base_y);

	update_extent_part(base_x, base_y, &usable, ssd->extents.topleft,
		(struct wlr_box){
			.x = 0,
			.y = 0,
			.width = corner_size,
			.height = corner_size
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.top,
		(struct wlr_box){
			.x = corner_size,
			.y = 0,
			.width = side_width,
			.height = extended_area,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.topright,
		(struct wlr_box){
			.x = corner_size + side_width,
			.y = 0,
			.width = corner_size,
			.height = corner_size,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.left,
		(struct wlr_box){
			.x = 0,
			.y = corner_size,
			.width = extended_area,
			.height = side_height,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.right,
		(struct wlr_box){
			.x = extended_area + full_width,
			.y = corner_size,
			.width = extended_area,
			.height = side_height,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.bottomleft,
		(struct wlr_box){
			.x = 0,
			.y = corner_size + side_height,
			.width = corner_size,
			.height = corner_size,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.bottom,
		(struct wlr_box){
			.x = corner_size,
			.y = extended_area + full_height,
			.width = side_width,
			.height = extended_area,
		});
	update_extent_part(base_x, base_y, &usable, ssd->extents.bottomright,
		(struct wlr_box){
			.x = corner_size + side_width,
			.y = corner_size + side_height,
			.width = corner_size,
			.height = corner_size,
		});

	pixman_region32_fini(&usable);
}

void
ssd_extents_destroy(struct ssd *ssd)
{
	if (!ssd->extents.tree) {
		return;
	}

	wlr_scene_node_destroy(&ssd->extents.tree->node);
	ssd->extents = (struct ssd_extents_scene) {0};
}
