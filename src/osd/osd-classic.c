// SPDX-License-Identifier: GPL-2.0-only
#include <pango/pangocairo.h>
#include <wayland-util.h>
#include "buffer.h"
#include "common/array.h"
#include "common/graphic-helpers.h"
#include "labwc.h"
#include "osd.h"
#include "workspaces.h"

static void
render_osd(struct server *server, cairo_t *cairo, int w, int h,
		bool show_workspace, const char *workspace_name,
		struct wl_array *views)
{
	struct view *cycle_view = server->osd_state.cycle_view;
	struct theme *theme = server->theme;

	cairo_surface_t *surf = cairo_get_target(cairo);

	/* Draw background */
	set_cairo_color(cairo, theme->osd_bg_color);
	cairo_rectangle(cairo, 0, 0, w, h);
	cairo_fill(cairo);

	/* Draw border */
	set_cairo_color(cairo, theme->osd_border_color);
	struct wlr_fbox fbox = {
		.width = w,
		.height = h,
	};
	draw_cairo_border(cairo, fbox, theme->osd_border_width);

	/* Set up text rendering */
	set_cairo_color(cairo, theme->osd_label_text_color);
	PangoLayout *layout = pango_cairo_create_layout(cairo);
	pango_context_set_round_glyph_positions(pango_layout_get_context(layout), false);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

	PangoFontDescription *desc = font_to_pango_desc(&rc.font_osd);
	pango_layout_set_font_description(layout, desc);

	pango_cairo_update_layout(cairo, layout);

	int y = theme->osd_border_width + theme->osd_window_switcher_padding;

	/* Draw workspace indicator */
	if (show_workspace) {
		/* Center workspace indicator on the x axis */
		int x = font_width(&rc.font_osd, workspace_name);
		x = (w - x) / 2;
		cairo_move_to(cairo, x, y + theme->osd_window_switcher_item_active_border_width);
		PangoWeight weight = pango_font_description_get_weight(desc);
		pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
		pango_layout_set_font_description(layout, desc);
		pango_layout_set_text(layout, workspace_name, -1);
		pango_cairo_show_layout(cairo, layout);
		pango_font_description_set_weight(desc, weight);
		pango_layout_set_font_description(layout, desc);
		y += theme->osd_window_switcher_item_height;
	}
	pango_font_description_free(desc);

	struct buf buf = BUF_INIT;

	/* This is the width of the area available for text fields */
	int available_width = w - 2 * theme->osd_border_width
		- 2 * theme->osd_window_switcher_padding
		- 2 * theme->osd_window_switcher_item_active_border_width;

	/* Draw text for each node */
	struct view **view;
	wl_array_for_each(view, views) {
		/*
		 *    OSD border
		 * +---------------------------------+
		 * |                                 |
		 * |  item border                    |
		 * |+-------------------------------+|
		 * ||                               ||
		 * ||padding between each field     ||
		 * ||| field-1 | field-2 | field-n |||
		 * ||                               ||
		 * ||                               ||
		 * |+-------------------------------+|
		 * |                                 |
		 * |                                 |
		 * +---------------------------------+
		 */
		int x = theme->osd_border_width
			+ theme->osd_window_switcher_padding
			+ theme->osd_window_switcher_item_active_border_width
			+ theme->osd_window_switcher_item_padding_x;

		int nr_fields = wl_list_length(&rc.window_switcher.fields);
		struct window_switcher_field *field;
		wl_list_for_each(field, &rc.window_switcher.fields, link) {
			buf_clear(&buf);
			cairo_move_to(cairo, x, y
				+ theme->osd_window_switcher_item_padding_y
				+ theme->osd_window_switcher_item_active_border_width);

			osd_field_get_content(field, &buf, *view);

			int field_width = (available_width - (nr_fields + 1)
				* theme->osd_window_switcher_item_padding_x)
				* field->width / 100.0;
			pango_layout_set_width(layout, field_width * PANGO_SCALE);
			pango_layout_set_text(layout, buf.data, -1);
			pango_cairo_show_layout(cairo, layout);
			x += field_width + theme->osd_window_switcher_item_padding_x;
		}

		if (*view == cycle_view) {
			/* Highlight current window */
			struct wlr_fbox fbox = {
				.x = theme->osd_border_width + theme->osd_window_switcher_padding,
				.y = y,
				.width = w
					- 2 * theme->osd_border_width
					- 2 * theme->osd_window_switcher_padding,
				.height = theme->osd_window_switcher_item_height,
			};
			draw_cairo_border(cairo, fbox,
				theme->osd_window_switcher_item_active_border_width);
			cairo_stroke(cairo);
		}

		y += theme->osd_window_switcher_item_height;
	}
	buf_reset(&buf);
	g_object_unref(layout);

	cairo_surface_flush(surf);
}

void
osd_display_classic(struct output *output, struct wl_array *views)
{
	struct server *server = output->server;
	struct theme *theme = server->theme;
	bool show_workspace = wl_list_length(&rc.workspace_config.workspaces) > 1;
	const char *workspace_name = server->workspaces.current->name;

	float scale = output->wlr_output->scale;
	int w = theme->osd_window_switcher_width;
	if (theme->osd_window_switcher_width_is_percent) {
		w = output->wlr_output->width / output->wlr_output->scale
			* theme->osd_window_switcher_width / 100;
	}
	int h = wl_array_len(views) * rc.theme->osd_window_switcher_item_height
		+ 2 * rc.theme->osd_border_width
		+ 2 * rc.theme->osd_window_switcher_padding;
	if (show_workspace) {
		/* workspace indicator */
		h += theme->osd_window_switcher_item_height;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(w, h, scale);
	if (!buffer) {
		wlr_log(WLR_ERROR, "Failed to allocate cairo buffer for the window switcher");
		return;
	}

	/* Render OSD image */
	cairo_t *cairo = cairo_create(buffer->surface);
	render_osd(server, cairo, w, h, show_workspace, workspace_name, views);
	cairo_destroy(cairo);

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_create(
		output->osd_tree, &buffer->base);
	wlr_buffer_drop(&buffer->base);
	wlr_scene_buffer_set_dest_size(scene_buffer, w, h);

	/* Center OSD */
	struct wlr_box usable = output_usable_area_in_layout_coords(output);
	int lx = usable.x + usable.width / 2 - w / 2;
	int ly = usable.y + usable.height / 2 - h / 2;
	wlr_scene_node_set_position(&scene_buffer->node, lx, ly);

	if (output->switcher_osd) {
		wlr_scene_node_destroy(output->switcher_osd);
	}
	output->switcher_osd = &scene_buffer->node;
}
