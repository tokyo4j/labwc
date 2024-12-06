// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/render/swapchain.h>
#include "buffer.h"
#include "common/array.h"
#include "common/box.h"
#include "common/scaled-font-buffer.h"
#include "desktop-entry.h"
#include "labwc.h"
#include "osd.h"

/* based on wlroots code */
static struct wlr_texture *scene_buffer_get_texture(
		struct wlr_scene_buffer *scene_buffer,
		struct wlr_renderer *renderer)
{
	if (!scene_buffer->buffer || scene_buffer->texture) {
		return scene_buffer->texture;
	}
	struct wlr_client_buffer *client_buffer =
		wlr_client_buffer_get(scene_buffer->buffer);
	if (client_buffer) {
		return client_buffer->texture;
	}
	return NULL;
}

static void
render_node(struct server *server, struct wlr_render_pass *pass,
		struct wlr_scene_node *node, int x, int y)
{
	switch (node->type) {
	case WLR_SCENE_NODE_TREE: {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &tree->children, link) {
			render_node(server, pass, child, x + node->x, y + node->y);
		}
		break;
	}
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_from_node(node);
		struct wlr_texture *texture = scene_buffer_get_texture(
			scene_buffer, server->renderer);
		if (!texture) {
			break;
		}
		wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
			.texture = texture,
			.src_box = scene_buffer->src_box,
			.dst_box = {
				.x = x,
				.y = y,
				.width = scene_buffer->dst_width,
				.height = scene_buffer->dst_height,
			},
			.transform = scene_buffer->transform,
		});
		break;
	}
	case WLR_SCENE_NODE_RECT:
		wlr_log(WLR_ERROR, "ignoring rect");
		break;
	}
}

static struct wlr_buffer *
render_thumb(struct output *output, struct view *view)
{
	struct server *server = output->server;
	struct wlr_box box;
	wlr_surface_get_extends(view->surface, &box);
	/* FIXME: reading from GBM BO can be very slow with pixman render */
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

const int thumb_item_width = 300;
const int thumb_item_height = 250;
const float thumb_bg_max_width_percent = 0.8;
int title_height;
const int thumb_item_padding = 10;
const float thumb_item_bg_active_color[4] = {.25, .58, .95, 1};
const float thumb_item_bg_color[4] = {1, 1, 1, 1};
const int thumb_bg_padding = 5;
const float thumb_bg_color[4] = {1, 1, 1, 1};
const float thumb_title_color[4] = {0, 0, 0, 1};
const float thumb_active_title_color[4] = {1, 1, 1, 1};
const float thumb_icon_size = 60;

static struct wlr_scene_tree *
create_thumbnail_item_scene(struct wlr_scene_tree *parent, struct view *view,
		struct output *output)
{
	struct wlr_scene_tree *tree = wlr_scene_tree_create(parent);
	struct server *server = output->server;

	const float *bg_color = (view == server->osd_state.cycle_view) ?
		thumb_item_bg_active_color : thumb_item_bg_color;
	const float *title_color = (view == server->osd_state.cycle_view) ?
		thumb_active_title_color : thumb_title_color;
	int title_y = thumb_item_height - thumb_item_padding - title_height;

	/* Background */
	wlr_scene_rect_create(tree, thumb_item_width, thumb_item_height,
		bg_color);

	/* Thumbnail */
	struct wlr_buffer *thumb_buffer = render_thumb(output, view);
	if (thumb_buffer) {
		struct wlr_scene_buffer *thumb_scene_buffer =
			wlr_scene_buffer_create(tree, thumb_buffer);
		wlr_buffer_drop(thumb_buffer);

		int thumb_width = thumb_item_width - 2 * thumb_item_padding;
		int thumb_height = title_y - 2 * thumb_item_padding;
		struct wlr_box thumb_box = box_fit_within(
			thumb_buffer->width, thumb_buffer->height,
			thumb_width, thumb_height);
		thumb_box.x += thumb_item_padding;
		thumb_box.y += thumb_item_padding;
		wlr_scene_buffer_set_dest_size(thumb_scene_buffer,
			thumb_box.width, thumb_box.height);
		wlr_scene_node_set_position(&thumb_scene_buffer->node,
			thumb_box.x, thumb_box.y);
	}

	/* Title */
	const char *title = view_get_string_prop(view, "title");
	if (title) {
		struct scaled_font_buffer *title_buffer =
			scaled_font_buffer_create(tree);
		assert(title_buffer);
		scaled_font_buffer_update(title_buffer, title,
			thumb_item_width - 2 * thumb_item_padding,
			&rc.font_osd, title_color, bg_color, NULL);
		wlr_scene_node_set_position(&title_buffer->scene_buffer->node,
			(thumb_item_width - title_buffer->width) / 2, title_y);
	}

	/* Icon */
	struct lab_data_buffer *icon_buffer = NULL;
	const char *app_id = view_get_string_prop(view, "app_id");
	if (app_id) {
		icon_buffer = desktop_entry_icon_lookup(server, app_id,
			thumb_icon_size, output->wlr_output->scale);
	}
	if (icon_buffer) {
		struct wlr_scene_buffer *icon_scene_buffer =
			wlr_scene_buffer_create(tree, &icon_buffer->base);
		wlr_scene_buffer_set_dest_size(icon_scene_buffer,
			thumb_icon_size, thumb_icon_size);
		int x = (thumb_item_width - thumb_icon_size) / 2;
		int y = title_y - thumb_item_padding - thumb_icon_size + 10;
		wlr_scene_node_set_position(&icon_scene_buffer->node, x, y);
		wlr_buffer_drop(&icon_buffer->base);
	}
	return tree;
}

static void
get_items_geometry(struct output *output, int nr_thumbs, int *nr_rows, int *nr_cols)
{
	int output_width = output->wlr_output->width / output->wlr_output->scale;
	int max_bg_width = output_width * thumb_bg_max_width_percent;
	*nr_rows = 1;
	*nr_cols = nr_thumbs;
	while (1) {
		assert(*nr_rows <= nr_thumbs);
		int bg_width = *nr_cols * thumb_item_width + 2 * thumb_bg_padding;
		if (bg_width < max_bg_width) {
			break;
		}
		(*nr_rows)++;
		*nr_cols = ceilf((float)nr_thumbs / *nr_rows);
	}
}

void
osd_display_thumbnails(struct output *output, struct wl_array *views)
{
	struct wlr_scene_tree *root = wlr_scene_tree_create(output->osd_tree);
	title_height = font_height(&rc.font_osd);

	int nr_views = wl_array_len(views);
	assert(nr_views > 0);
	int nr_rows, nr_cols;
	get_items_geometry(output, nr_views, &nr_rows, &nr_cols);

	struct view **view;
	int index = 0;
	wl_array_for_each(view, views) {
		struct wlr_scene_tree *item_tree =
			create_thumbnail_item_scene(root, *view, output);
		int x = (index % nr_cols) * thumb_item_width + thumb_bg_padding;
		int y = (index / nr_cols) * thumb_item_height + thumb_bg_padding;
		wlr_scene_node_set_position(&item_tree->node, x, y);
		index++;
	}

	int bg_height = nr_rows * thumb_item_height + 2 * thumb_bg_padding;
	int bg_width = nr_cols * thumb_item_width + 2 * thumb_bg_padding;
	struct wlr_scene_rect *bg = wlr_scene_rect_create(
		root, bg_width, bg_height, thumb_bg_color);
	wlr_scene_node_lower_to_bottom(&bg->node);

	struct wlr_box usable = output_usable_area_in_layout_coords(output);
	int lx = usable.x + (usable.width - bg_width) / 2;
	int ly = usable.y + (usable.height - bg_height) / 2;
	wlr_scene_node_set_position(&root->node, lx, ly);

	if (output->switcher_osd) {
		wlr_scene_node_destroy(output->switcher_osd);
	}
	output->switcher_osd = &root->node;
}
