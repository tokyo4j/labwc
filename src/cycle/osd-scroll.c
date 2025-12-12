// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/lab-scene-rect.h"
#include "labwc.h"
#include "cycle.h"
#include "output.h"

void
cycle_osd_scroll_init(struct output *output, int x, int y, int h, int item_height,
		int nr_cols, int nr_rows, int nr_visible_rows,
		float *border_color, float *bg_color)
{
	if (nr_visible_rows >= nr_rows) {
		/* OSD doesn't have so many windows to scroll through */
		return;
	}

	struct cycle_osd_scroll_context *scroll = &output->cycle_osd.scroll;
	scroll->nr_cols = nr_cols;
	scroll->nr_rows = nr_rows;
	scroll->nr_visible_rows = nr_visible_rows;
	scroll->top_row_idx = 0;
	scroll->bar_area_height = h;
	scroll->item_height = item_height;
	scroll->bar_tree = wlr_scene_tree_create(output->cycle_osd.tree);
	wlr_scene_node_set_position(&scroll->bar_tree->node, x, y);

	struct lab_scene_rect_options scrollbar_opts = {
		.border_colors = (float *[1]){ border_color },
		.nr_borders = 1,
		.border_width = 1,
		.bg_color = bg_color,
		.width = SCROLLBAR_W,
		.height = h * nr_visible_rows / nr_rows,
	};
	scroll->bar = lab_scene_rect_create(scroll->bar_tree, &scrollbar_opts);
}

static int
get_cycle_idx(struct output *output)
{
	struct server *server = output->server;

	int idx = 0;
	struct cycle_osd_item *item;
	wl_list_for_each(item, &output->cycle_osd.items, link) {
		if (item->view == server->cycle.selected_view) {
			return idx;
		}
		idx++;
	}
	/* should be unreachable */
	wlr_log(WLR_DEBUG, "cycle view not found");
	return -1;
}

void
cycle_osd_scroll_update(struct output *output)
{
	struct cycle_osd_scroll_context *scroll = &output->cycle_osd.scroll;
	if (!scroll->bar) {
		return;
	}

	int cycle_idx = get_cycle_idx(output);
	if (cycle_idx < 0) {
		return;
	}

	/* Scroll the items if the selection goes out of the visible area */
	int bottom_row_idx = scroll->top_row_idx + scroll->nr_visible_rows;
	while (cycle_idx < scroll->top_row_idx * scroll->nr_cols) {
		scroll->top_row_idx--;
		bottom_row_idx--;
	}
	while (cycle_idx >= bottom_row_idx * scroll->nr_cols) {
		scroll->top_row_idx++;
		bottom_row_idx++;
	}

	/* Move scrollbar */
	wlr_scene_node_set_position(&scroll->bar->tree->node, 0,
		scroll->bar_area_height * scroll->top_row_idx / scroll->nr_rows);
	/* Move items */
	wlr_scene_node_set_position(&output->cycle_osd.items_tree->node, 0,
		-scroll->item_height * scroll->top_row_idx);

	/* Hide views outside of visible area */
	int idx = 0;
	struct cycle_osd_item *item;
	wl_list_for_each(item, &output->cycle_osd.items, link) {
		bool visible = idx >= scroll->top_row_idx * scroll->nr_cols
			&& idx < bottom_row_idx * scroll->nr_cols;
		wlr_scene_node_set_enabled(&item->tree->node, visible);
		idx++;
	}
}
