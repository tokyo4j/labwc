// SPDX-License-Identifier: GPL-2.0-only
#include "config.h"
#include <assert.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>
#include <wlr/render/swapchain.h>
#include "buffer.h"
#include "common/array.h"
#include "common/buf.h"
#include "common/font.h"
#include "common/graphic-helpers.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "osd.h"
#include "theme.h"
#include "view.h"
#include "window-rules.h"
#include "workspaces.h"

static void
destroy_osd_nodes(struct output *output)
{
	struct wlr_scene_node *child, *next;
	struct wl_list *children = &output->osd_tree->children;
	wl_list_for_each_safe(child, next, children, link) {
		wlr_scene_node_destroy(child);
	}
}

static void
osd_update_preview_outlines(struct view *view)
{
	/* Create / Update preview outline tree */
	struct server *server = view->server;
	struct multi_rect *rect = view->server->osd_state.preview_outline;
	if (!rect) {
		int line_width = server->theme->osd_window_switcher_preview_border_width;
		float *colors[] = {
			server->theme->osd_window_switcher_preview_border_color[0],
			server->theme->osd_window_switcher_preview_border_color[1],
			server->theme->osd_window_switcher_preview_border_color[2],
		};
		rect = multi_rect_create(&server->scene->tree, colors, line_width);
		wlr_scene_node_place_above(&rect->tree->node, &server->menu_tree->node);
		server->osd_state.preview_outline = rect;
	}

	struct wlr_box geo = ssd_max_extents(view);
	multi_rect_set_size(rect, geo.width, geo.height);
	wlr_scene_node_set_position(&rect->tree->node, geo.x, geo.y);
}

void
osd_on_view_destroy(struct view *view)
{
	assert(view);
	struct osd_state *osd_state = &view->server->osd_state;

	if (!osd_state->cycle_view) {
		/* OSD not active, no need for clean up */
		return;
	}

	if (osd_state->cycle_view == view) {
		/*
		 * If we are the current OSD selected view, cycle
		 * to the next because we are dying.
		 */

		/* Also resets preview node */
		osd_state->cycle_view = desktop_cycle_view(view->server,
			osd_state->cycle_view, LAB_CYCLE_DIR_BACKWARD);

		/*
		 * If we cycled back to ourselves, then we have no more windows.
		 * Just close the OSD for good.
		 */
		if (osd_state->cycle_view == view || !osd_state->cycle_view) {
			/* osd_finish() additionally resets cycle_view to NULL */
			osd_finish(view->server);
		}
	}

	if (osd_state->cycle_view) {
		/* Update the OSD to reflect the view has now gone. */
		osd_update(view->server);
	}

	if (view->scene_tree) {
		struct wlr_scene_node *node = &view->scene_tree->node;
		if (osd_state->preview_anchor == node) {
			/*
			 * If we are the anchor for the current OSD selected view,
			 * replace the anchor with the node before us.
			 */
			osd_state->preview_anchor = lab_wlr_scene_get_prev_node(node);
		}
	}
}

void
osd_finish(struct server *server)
{
	server->osd_state.preview_node = NULL;
	server->osd_state.preview_anchor = NULL;

	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		destroy_osd_nodes(output);
		wlr_scene_node_set_enabled(&output->osd_tree->node, false);
	}
	if (server->osd_state.preview_outline) {
		/* Destroy the whole multi_rect so we can easily react to new themes */
		wlr_scene_node_destroy(&server->osd_state.preview_outline->tree->node);
		server->osd_state.preview_outline = NULL;
	}

	/* Hiding OSD may need a cursor change */
	cursor_update_focus(server);

	/*
	 * We delay resetting cycle_view until after cursor_update_focus()
	 * has been called to allow A-Tab keyboard focus switching even if
	 * followMouse has been configured and the cursor is on a different
	 * surface. Otherwise cursor_update_focus() would automatically
	 * refocus the surface the cursor is currently on.
	 */
	server->osd_state.cycle_view = NULL;
}

void
osd_preview_restore(struct server *server)
{
	struct osd_state *osd_state = &server->osd_state;
	if (osd_state->preview_node) {
		wlr_scene_node_reparent(osd_state->preview_node,
			osd_state->preview_parent);

		if (osd_state->preview_anchor) {
			wlr_scene_node_place_above(osd_state->preview_node,
				osd_state->preview_anchor);
		} else {
			/* Selected view was the first node */
			wlr_scene_node_lower_to_bottom(osd_state->preview_node);
		}

		/* Node was disabled / minimized before, disable again */
		if (!osd_state->preview_was_enabled) {
			wlr_scene_node_set_enabled(osd_state->preview_node, false);
		}
		osd_state->preview_node = NULL;
		osd_state->preview_parent = NULL;
		osd_state->preview_anchor = NULL;
	}
}

static void
preview_cycled_view(struct view *view)
{
	assert(view);
	assert(view->scene_tree);
	struct osd_state *osd_state = &view->server->osd_state;

	/* Move previous selected node back to its original place */
	osd_preview_restore(view->server);

	/* Store some pointers so we can reset the preview later on */
	osd_state->preview_node = &view->scene_tree->node;
	osd_state->preview_parent = view->scene_tree->node.parent;

	/* Remember the sibling right before the selected node */
	osd_state->preview_anchor = lab_wlr_scene_get_prev_node(
		osd_state->preview_node);
	while (osd_state->preview_anchor && !osd_state->preview_anchor->data) {
		/* Ignore non-view nodes */
		osd_state->preview_anchor = lab_wlr_scene_get_prev_node(
			osd_state->preview_anchor);
	}

	/* Store node enabled / minimized state and force-enable if disabled */
	osd_state->preview_was_enabled = osd_state->preview_node->enabled;
	if (!osd_state->preview_was_enabled) {
		wlr_scene_node_set_enabled(osd_state->preview_node, true);
	}

	/*
	 * FIXME: This abuses an implementation detail of the always-on-top tree.
	 *        Create a permanent server->osd_preview_tree instead that can
	 *        also be used as parent for the preview outlines.
	 */
	wlr_scene_node_reparent(osd_state->preview_node,
		view->server->view_tree_always_on_top);

	/* Finally raise selected node to the top */
	wlr_scene_node_raise_to_top(osd_state->preview_node);
}

/* based on wlroots code */
static struct wlr_texture *scene_buffer_get_texture(
		struct wlr_scene_buffer *scene_buffer,
		struct wlr_renderer *renderer) {
	if (!scene_buffer->buffer || scene_buffer->texture) {
		return scene_buffer->texture;
	}
	struct wlr_client_buffer *client_buffer =
		wlr_client_buffer_get(scene_buffer->buffer);
	assert(client_buffer);
	return client_buffer->texture;
}

static void
render_node(struct server *server, struct wlr_render_pass *pass,
		struct wlr_scene_node *node, int x, int y)
{
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:;
		struct wlr_scene_node *child;
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		wl_list_for_each(child, &tree->children, link) {
			render_node(server, pass, child, x + node->x, y + node->y);
		}
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_from_node(node);
		struct wlr_texture *texture = scene_buffer_get_texture(
			scene_buffer, server->renderer);
		if (!texture) {
			break;
		}
		/* TODO: transform */
		wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
			.texture = texture,
			.dst_box = {
				.x = x,
				.y = y,
				.width = scene_buffer->buffer_width,
				.height = scene_buffer->buffer_height,
			}
		});
		break;
	case WLR_SCENE_NODE_RECT:
		wlr_log(WLR_ERROR, "ignoring rect");
	}
}

static struct wlr_buffer *
render_snapshot(struct output *output, struct view *view)
{
	struct server *server = output->server;
	struct wlr_box box;
	wlr_surface_get_extends(view->surface, &box);
	struct wlr_buffer *buffer = wlr_allocator_create_buffer(server->allocator,
		box.width, box.height, &output->wlr_output->swapchain->format);
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(
		server->renderer, buffer, NULL);
	render_node(server, pass, view->scene_node, 0, 0);
	if (!wlr_render_pass_submit(pass)) {
		wlr_log(WLR_ERROR, "failed to submit render pass");
		wlr_buffer_drop(buffer);
		return NULL;
	}
	return buffer;
}

#define SNAPSHOT_ITEM_PADDING 10
#define SNAPSHOT_ITEM_WIDTH 220
#define SNAPSHOT_ITEM_HEIGHT 220
#define SNAPSHOT_WIDTH 200
#define SNAPSHOT_HEIGHT 200

static void
display_osd(struct output *output, struct wl_array *views)
{
	struct server *server = output->server;

	struct view **v;
	int item_x = SNAPSHOT_ITEM_PADDING;
	wl_array_for_each(v, views) {
		struct view *view = *v;
		struct wlr_scene_tree *tree = wlr_scene_tree_create(output->osd_tree);
		wlr_scene_node_set_position(&tree->node, item_x, 0);

		float bg_color[4];
		if (view == server->osd_state.cycle_view) {
			memcpy(bg_color, (float[4]){.25, .58, .95, 1}, sizeof(bg_color));
		} else {
			memcpy(bg_color, (float[4]){1, 1, 1, 1}, sizeof(bg_color));
		}

		struct wlr_scene_rect *bg = wlr_scene_rect_create(
			tree, SNAPSHOT_ITEM_WIDTH,
			SNAPSHOT_ITEM_HEIGHT, bg_color);
		(void)bg;

		struct wlr_buffer *snapshot_buffer = render_snapshot(output, view);
		if (!snapshot_buffer) {
			continue;
		}
		struct wlr_scene_buffer *snapshot_scene_buffer =
			wlr_scene_buffer_create(tree, snapshot_buffer);
		wlr_buffer_drop(snapshot_buffer);

		/* TODO: duplicate with get_scale_box() */
		int snapshot_width = snapshot_buffer->width;
		int snapshot_height = snapshot_buffer->height;
		double scale = MIN((double)SNAPSHOT_WIDTH / (double)snapshot_width,
			(double)SNAPSHOT_HEIGHT / (double)snapshot_height);
		if (scale < 1.0) {
			snapshot_width = (double)snapshot_width * scale;
			snapshot_height = (double)snapshot_height * scale;
		}
		wlr_scene_buffer_set_dest_size(snapshot_scene_buffer,
			snapshot_width, snapshot_height);
		wlr_scene_node_set_position(&snapshot_scene_buffer->node,
			(SNAPSHOT_ITEM_WIDTH - snapshot_width) / 2,
			(SNAPSHOT_ITEM_HEIGHT - snapshot_height) / 2);

		item_x += SNAPSHOT_ITEM_WIDTH + SNAPSHOT_ITEM_PADDING;
	}

	wlr_scene_node_set_position(&output->osd_tree->node,
		(output->wlr_output->width - item_x) / 2,
		(output->wlr_output->height - SNAPSHOT_ITEM_HEIGHT) / 2);

	wlr_scene_node_set_enabled(&output->osd_tree->node, true);
}

void
osd_update(struct server *server)
{
	struct wl_array views;
	wl_array_init(&views);
	view_array_append(server, &views, rc.window_switcher.criteria);

	if (!wl_array_len(&views) || !server->osd_state.cycle_view) {
		osd_finish(server);
		goto out;
	}

	if (rc.window_switcher.show && rc.theme->osd_window_switcher_width > 0) {
		/* Display the actual OSD */
		struct output *output;
		wl_list_for_each(output, &server->outputs, link) {
			destroy_osd_nodes(output);
			if (output_is_usable(output)) {
				display_osd(output, &views);
			}
		}
	}

	/* Outline current window */
	if (rc.window_switcher.outlines) {
		if (view_is_focusable(server->osd_state.cycle_view)) {
			osd_update_preview_outlines(server->osd_state.cycle_view);
		}
	}

	if (rc.window_switcher.preview) {
		preview_cycled_view(server->osd_state.cycle_view);
	}
out:
	wl_array_release(&views);
}
