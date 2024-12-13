// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include "common/mem.h"
#include "common/scaled-font-buffer.h"
#include "foreign-toplevel.h"
#include "labwc.h"
#include "node.h"
#include "prompt.h"
#include "view-impl-common.h"
#include "workspaces.h"

#define PADDING 5

static void
prompt_view_configure(struct view *view, struct wlr_box geo)
{
	struct prompt_view *prompt_view =
		wl_container_of(view, prompt_view, base);
	wlr_scene_rect_set_size(prompt_view->background, geo.width, geo.height);
	scaled_font_buffer_set_max_width(prompt_view->text_buffer,
		geo.width - 2 * PADDING);
	/* TODO: reposition buttons */
	view->current = geo;
	view->pending = geo;

	view_moved(view);
	view_impl_apply_geometry(view, geo.width, geo.height);
}

static const char *
prompt_view_get_string_prop(struct view *view, const char *prop)
{
	struct prompt_view *prompt_view =
		wl_container_of(view, prompt_view, base);

	if (!strcmp(prop, "title")) {
		return "Prompt";
	}
	if (!strcmp(prop, "app_id")) {
		return "labwc";
	}
	return "";
}

static const struct view_impl prompt_view_impl = {
	.configure = prompt_view_configure,
	.move_to_front = view_impl_move_to_front,
	.move_to_back = view_impl_move_to_back,
	.get_string_prop = prompt_view_get_string_prop,
};

struct prompt_view *
prompt_create(struct server *server, const char *text, const char **answers, int nr_answers)
{
	struct prompt_view *prompt_view = znew(*prompt_view);
	struct view *view = &prompt_view->base;

	view->server = server;
	view->type = LAB_PROMPT_VIEW;
	view->impl = &prompt_view_impl;
	view->workspace = server->workspaces.current;
	view->scene_tree = wlr_scene_tree_create(view->workspace->tree);
	node_descriptor_create(&view->scene_tree->node, LAB_NODE_DESC_VIEW, view);
	view_init(view);
	wl_list_insert(&server->views, &view->link);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(view->scene_tree);
	view->scene_node = &tree->node;

	const float bg_color[4] = {1, 1, 1, 1};

	const float button_color[4] = {0.35, 0.610, 0.85, 1};
	const float text_color[4] = {0, 0, 0, 1};
	struct font *font = &rc.font_osd;
	int text_height = font_height(font);
	int width = 200;
	int height = 60;

	prompt_view->background =
		wlr_scene_rect_create(tree, width, height, bg_color);

	int x = PADDING;
	int y = PADDING;
	prompt_view->text_buffer = scaled_font_buffer_create(tree);
	scaled_font_buffer_update(prompt_view->text_buffer, text,
	width - 2 * PADDING, &rc.font_osd, text_color, bg_color, NULL);
	wlr_scene_node_set_position(&prompt_view->text_buffer->scene_buffer->node, x, y);

	x = 200;
	y += text_height + PADDING;
	for (int i = nr_answers - 1; i >= 0; i--) {
		struct wlr_scene_tree *button_tree = wlr_scene_tree_create(tree);
		int button_width = font_width(font, answers[i]) + 2 * PADDING;
		x -= button_width + PADDING;
		wlr_scene_node_set_position(&button_tree->node, x, y);
		/* button background */
		wlr_scene_rect_create(button_tree, button_width, text_height,
			button_color);
		/* button text */
		struct scaled_font_buffer *text_buffer =
			scaled_font_buffer_create(button_tree);
		scaled_font_buffer_update(text_buffer, answers[i], 1000,
			&rc.font_osd, text_color, button_color, NULL);
		wlr_scene_node_set_position(&text_buffer->scene_buffer->node, PADDING, 0);
	}

	view_set_ssd_mode(view, LAB_SSD_MODE_FULL);
	view->mapped = true;
	view->foreign_toplevel = foreign_toplevel_create(view);
	view_impl_map(view);
	view->been_mapped = true;
	prompt_view_configure(view, (struct wlr_box){.width = width, .height = height});
	view_place_by_policy(view, /* allow_cursor */ true, rc.placement_policy);

	return prompt_view;
}
