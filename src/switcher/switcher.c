// SPDX-License-Identifier: GPL-2.0-only
/* Implementation of window switcher, including OSD, previews, and outlines */
#include "switcher.h"
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/array.h"
#include "common/lab-scene-rect.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "ssd.h"
#include "theme.h"
#include "view.h"

static bool init_switcher(struct server *server);
static void update_switcher(struct server *server);

static void
destroy_switcher(struct server *server)
{
	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct switcher_osd_item *item, *tmp;
		wl_list_for_each_safe(item, tmp, &output->switcher_osd.items, link) {
			wl_list_remove(&item->link);
			free(item);
		}
		if (output->switcher_osd.tree) {
			wlr_scene_node_destroy(&output->switcher_osd.tree->node);
			output->switcher_osd.tree = NULL;
		}
	}

	wl_array_release(&server->switcher.views);
}

static int
idx_from_view(struct server *server, struct view *view)
{
	if (!view) {
		return 0;
	}
	struct switcher_state *switcher = &server->switcher;
	int nr_views = wl_array_len(&switcher->views);
	struct view ** views = switcher->views.data;
	for (int i = 0; i < nr_views; i++) {
		if (views[i] == view) {
			return i;
		}
	}
	return 0;
}

static struct view *
get_cycle_view(struct server *server)
{
	struct switcher_state *switcher = &server->switcher;
	if (wl_array_len(&switcher->views) <= 0) {
		return NULL;
	}
	struct view **views = switcher->views.data;
	return views[switcher->cycle_idx];
}

static void
update_preview_outlines(struct server *server)
{
	/* Create / Update preview outline tree */
	struct theme *theme = server->theme;
	struct lab_scene_rect *rect = server->switcher.preview_outline;
	struct view *view = get_cycle_view(server);
	assert(view);
	if (!rect) {
		struct lab_scene_rect_options opts = {
			.border_colors = (float *[3]) {
				theme->osd_window_switcher_preview_border_color[0],
				theme->osd_window_switcher_preview_border_color[1],
				theme->osd_window_switcher_preview_border_color[2],
			},
			.nr_borders = 3,
			.border_width = theme->osd_window_switcher_preview_border_width,
		};
		rect = lab_scene_rect_create(&server->scene->tree, &opts);
		wlr_scene_node_place_above(&rect->tree->node, &server->menu_tree->node);
		server->switcher.preview_outline = rect;
	}

	struct wlr_box geo = ssd_max_extents(view);
	lab_scene_rect_set_size(rect, geo.width, geo.height);
	wlr_scene_node_set_position(&rect->tree->node, geo.x, geo.y);
}

static void
update_cycle_idx(struct server *server, enum lab_cycle_dir direction)
{
	struct switcher_state *switcher = &server->switcher;
	if (direction == LAB_CYCLE_DIR_FORWARD) {
		switcher->cycle_idx++;
	} else {
		switcher->cycle_idx--;
	}
	/* Wrap in [0, nr_views-1] */
	int nr_views = wl_array_len(&switcher->views);
	switcher->cycle_idx = (switcher->cycle_idx + nr_views) % nr_views;
}

void
switcher_on_view_destroy(struct view *view)
{
	assert(view);
	struct server *server = view->server;
	struct switcher_state *switcher = &server->switcher;

	if (server->input_mode != LAB_INPUT_STATE_WINDOW_SWITCHER) {
		/* OSD not active, no need for clean up */
		return;
	}

	destroy_switcher(server);
	if (!init_switcher(server)) {
		switcher_finish(server, /*switch_focus*/ false);
		return;
	}
	switcher->cycle_idx = switcher->cycle_idx % wl_array_len(&switcher->views);
	update_switcher(server);

	if (view->scene_tree) {
		struct wlr_scene_node *node = &view->scene_tree->node;
		if (switcher->preview_anchor == node) {
			/*
			 * If we are the anchor for the current selected view,
			 * replace the anchor with the node before us.
			 */
			switcher->preview_anchor = lab_wlr_scene_get_prev_node(node);
		}
	}
}

void
switcher_on_cursor_release(struct server *server, struct wlr_scene_node *node)
{
	assert(server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER);

	struct switcher_osd_item *item = node_switcher_osd_item_from_node(node);
	server->switcher.cycle_idx = idx_from_view(server, item->view);
	switcher_finish(server, /*switch_focus*/ true);
}

static void
restore_preview_node(struct server *server)
{
	struct switcher_state *switcher = &server->switcher;
	if (switcher->preview_node) {
		wlr_scene_node_reparent(switcher->preview_node,
			switcher->preview_parent);

		if (switcher->preview_anchor) {
			wlr_scene_node_place_above(switcher->preview_node,
				switcher->preview_anchor);
		} else {
			/* Selected view was the first node */
			wlr_scene_node_lower_to_bottom(switcher->preview_node);
		}

		/* Node was disabled / minimized before, disable again */
		if (!switcher->preview_was_enabled) {
			wlr_scene_node_set_enabled(switcher->preview_node, false);
		}
		if (switcher->preview_was_shaded) {
			struct view *view = node_view_from_node(switcher->preview_node);
			view_set_shade(view, true);
		}
		switcher->preview_node = NULL;
		switcher->preview_parent = NULL;
		switcher->preview_anchor = NULL;
		switcher->preview_was_shaded = false;
	}
}

void
switcher_begin(struct server *server, enum lab_cycle_dir direction)
{
	if (server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	if (!init_switcher(server)) {
		return;
	}
	server->switcher.cycle_idx = idx_from_view(server, server->active_view);
	update_cycle_idx(server, direction);
	update_switcher(server);

	seat_focus_override_begin(&server->seat,
		LAB_INPUT_STATE_WINDOW_SWITCHER, LAB_CURSOR_DEFAULT);
}

void
switcher_cycle(struct server *server, enum lab_cycle_dir direction)
{
	assert(server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER);

	update_cycle_idx(server, direction);
	update_switcher(server);
}

void
switcher_finish(struct server *server, bool switch_focus)
{
	if (server->input_mode != LAB_INPUT_STATE_WINDOW_SWITCHER) {
		return;
	}

	restore_preview_node(server);
	/* FIXME: this sets focus to the old surface even with switch_focus=true */
	seat_focus_override_end(&server->seat);

	struct view *cycle_view = get_cycle_view(server);
	server->switcher.cycle_idx = 0;
	server->switcher.preview_node = NULL;
	server->switcher.preview_anchor = NULL;
	server->switcher.preview_was_shaded = false;
	destroy_switcher(server);

	if (server->switcher.preview_outline) {
		/* Destroy the whole multi_rect so we can easily react to new themes */
		wlr_scene_node_destroy(&server->switcher.preview_outline->tree->node);
		server->switcher.preview_outline = NULL;
	}

	/* Hiding OSD may need a cursor change */
	cursor_update_focus(server);

	if (switch_focus && cycle_view) {
		if (rc.window_switcher.unshade) {
			view_set_shade(cycle_view, false);
		}
		desktop_focus_view(cycle_view, /*raise*/ true);
	}
}

static void
preview_cycled_view(struct server *server)
{
	struct switcher_state *switcher = &server->switcher;
	struct view *view = get_cycle_view(server);
	assert(view);

	/* Move previous selected node back to its original place */
	restore_preview_node(server);

	/* Store some pointers so we can reset the preview later on */
	switcher->preview_node = &view->scene_tree->node;
	switcher->preview_parent = view->scene_tree->node.parent;

	/* Remember the sibling right before the selected node */
	switcher->preview_anchor = lab_wlr_scene_get_prev_node(
		switcher->preview_node);
	while (switcher->preview_anchor && !switcher->preview_anchor->data) {
		/* Ignore non-view nodes */
		switcher->preview_anchor = lab_wlr_scene_get_prev_node(
			switcher->preview_anchor);
	}

	/* Store node enabled / minimized state and force-enable if disabled */
	switcher->preview_was_enabled = switcher->preview_node->enabled;
	if (!switcher->preview_was_enabled) {
		wlr_scene_node_set_enabled(switcher->preview_node, true);
	}
	if (rc.window_switcher.unshade && view->shaded) {
		view_set_shade(view, false);
		switcher->preview_was_shaded = true;
	}

	/*
	 * FIXME: This abuses an implementation detail of the always-on-top tree.
	 *        Create a permanent server->osd_preview_tree instead that can
	 *        also be used as parent for the preview outlines.
	 */
	wlr_scene_node_reparent(switcher->preview_node,
		view->server->view_tree_always_on_top);

	/* Finally raise selected node to the top */
	wlr_scene_node_raise_to_top(switcher->preview_node);
}

static struct switcher_osd_impl *
get_impl(void)
{
	switch (rc.window_switcher.style) {
	case SWITCHER_OSD_STYLE_CLASSIC:
		return &switcher_osd_classic_impl;
	case SWITCHER_OSD_STYLE_THUMBNAIL:
		return &switcher_osd_thumbnail_impl;
	}
	return NULL;
}

static bool
init_switcher(struct server *server)
{
	wl_array_init(&server->switcher.views);
	view_array_append(server, &server->switcher.views, rc.window_switcher.criteria);
	if (wl_array_len(&server->switcher.views) <= 0) {
		wlr_log(WLR_DEBUG, "no views to switch between");
		wl_array_release(&server->switcher.views);
		wl_array_init(&server->switcher.views);
		return false;
	}

	if (rc.window_switcher.show) {
		/* Create OSD */
		switch (rc.window_switcher.output_criteria) {
		case SWITCHER_OSD_OUTPUT_ALL: {
			struct output *output;
			wl_list_for_each(output, &server->outputs, link) {
				get_impl()->create(output);
			}
			break;
		}
		case SWITCHER_OSD_OUTPUT_POINTER:
			get_impl()->create(output_nearest_to_cursor(server));
			break;
		case SWITCHER_OSD_OUTPUT_KEYBOARD: {
			struct output *output;
			if (server->active_view) {
				output = server->active_view->output;
			} else {
				/* Fallback to pointer, if there is no active_view */
				output = output_nearest_to_cursor(server);
			}
			get_impl()->create(output);
			break;
		}
		}
	}

	return true;
}

static void
update_switcher(struct server *server)
{
	if (rc.window_switcher.show) {
		struct output *output;
		wl_list_for_each(output, &server->outputs, link) {
			if (output->switcher_osd.tree) {
				get_impl()->update(output);
			}
		}
	}

	if (rc.window_switcher.preview) {
		preview_cycled_view(server);
	}

	/* Outline current window */
	if (rc.window_switcher.outlines) {
		update_preview_outlines(server);
	}
}
