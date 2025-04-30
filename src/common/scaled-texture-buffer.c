// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-scene-buffer.h"
#include "common/scaled-texture-buffer.h"

static void
draw_pattern(cairo_t *cairo, int x0, int y0, int x1, int y1,
		float color[4], float color_to[4], int x_dir, int y_dir)
{
	assert(x1 > x0 && y1 > y0);
	assert(x_dir || y_dir);

	cairo_pattern_t *pattern = cairo_pattern_create_linear(0, 0, x_dir, y_dir);
	cairo_pattern_add_color_stop_rgba(pattern, 0,
		color[0], color[1], color[2], color[3]);
	cairo_pattern_add_color_stop_rgba(pattern, 1,
		color_to[0], color_to[1], color_to[2], color_to[3]);

	cairo_matrix_t matrix;
	cairo_matrix_init_scale(&matrix, 1.0 / (x1 - x0), 1.0 / (y1 - y0));
	cairo_matrix_translate(&matrix, -x0, -y0);
	if (x_dir < 0) {
		cairo_matrix_translate(&matrix, -(x1 - x0), 0);
	}
	if (y_dir < 0) {
		cairo_matrix_translate(&matrix, 0, -(y1 - y0));
	}
	cairo_pattern_set_matrix(pattern, &matrix);

	cairo_set_source(cairo, pattern);
	cairo_rectangle(cairo, x0, y0, x1 - x0, y1 - y0);
	cairo_fill(cairo);
	cairo_pattern_destroy(pattern);
}

static struct lab_data_buffer *
_create_buffer(struct scaled_scene_buffer *scaled_buffer, double scale)
{
	struct scaled_texture_buffer *self = scaled_buffer->data;
	struct texture_config *texture = &self->texture_conf;
	struct lab_data_buffer *buffer = buffer_create_cairo(
		self->width, self->height, scale);
	if (!buffer) {
		return NULL;
	}

	cairo_t *cairo = cairo_create(buffer->surface);
	int w = self->width;
	int h = self->height;

	switch (texture->grad_type) {
	case GRAD_TYPE_NONE:
		cairo_rectangle(cairo, 0, 0, w, h);
		cairo_set_source_rgba(cairo,
			texture->color[0], texture->color[1],
			texture->color[2], texture->color[3]);
		break;
	case GRAD_TYPE_DIAGONAL:
		draw_pattern(cairo, 0, 0, w, h,
			texture->color, texture->color_to,
			1, 1);
		break;
	case GRAD_TYPE_CROSS_DIAGONAL:
		draw_pattern(cairo, w, 0, 0, h,
			texture->color, texture->color_to,
			-1, 1);
		break;
	case GRAD_TYPE_PYRAMID:
		draw_pattern(cairo, 0, 0, w / 2, h / 2,
			texture->color, texture->color_to,
			1, 1);
		draw_pattern(cairo, w / 2, 0, w, h / 2,
			texture->color, texture->color_to,
			-1, 1);
		draw_pattern(cairo, 0, h / 2, w / 2, h,
			texture->color, texture->color_to,
			1, -1);
		draw_pattern(cairo, w / 2, h / 2, w, h,
			texture->color, texture->color_to,
			-1, -1);
		break;
	case GRAD_TYPE_HORIZONTAL:
		draw_pattern(cairo, 0, 0, w, h,
			texture->color, texture->color_to,
			1, 0);
		break;
	case GRAD_TYPE_MIRROR_HORIZONTAL:
		draw_pattern(cairo, 0, 0, w / 2, h,
			texture->color, texture->color_to,
			1, 0);
		draw_pattern(cairo, w / 2, 0, w, h,
			texture->color, texture->color_to,
			-1, 0);
		break;
	case GRAD_TYPE_VERTICAL:
		draw_pattern(cairo, 0, 0, w, h,
			texture->color, texture->color_to,
			0, 1);
		break;
	case GRAD_TYPE_SPLIT_VERTICAL:
		draw_pattern(cairo, 0, 0, w, h / 2,
			texture->color, texture->color_split_to,
			0, -1);
		draw_pattern(cairo, 0, h / 2, w, h,
			texture->color_to, texture->color_to_split_to,
			0, 1);
		break;
	}

	double bw = 1; /* border width */
	double bw2 = bw / 2;
	cairo_set_line_width(cairo, bw);

	switch (texture->border_type) {
	case BORDER_TYPE_NONE:
		break;
	case BORDER_TYPE_FLAT:
		cairo_rectangle(cairo, bw2, bw2, w - bw, h - bw);
		cairo_set_source_rgba(cairo,
			texture->border_color[0], texture->border_color[1],
			texture->border_color[1], texture->border_color[2]);
		cairo_stroke(cairo);
		break;
	case BORDER_TYPE_RAISED:
		cairo_move_to(cairo, bw2, h);
		cairo_line_to(cairo, bw2, bw2);
		cairo_line_to(cairo, w - bw, bw2);
		cairo_set_operator(cairo, CAIRO_OPERATOR_HARD_LIGHT);
		cairo_set_source_rgb(cairo, 0.75, 0.75, 0.75);
		cairo_stroke(cairo);

		cairo_move_to(cairo, w - bw2, 0);
		cairo_line_to(cairo, w - bw2, h - bw2);
		cairo_line_to(cairo, bw, h - bw2);
		cairo_set_operator(cairo, CAIRO_OPERATOR_HARD_LIGHT);
		cairo_set_source_rgb(cairo, 0.25, 0.25, 0.25);
		cairo_stroke(cairo);
		break;
	case BORDER_TYPE_SUNKEN:
		cairo_move_to(cairo, bw2, h);
		cairo_line_to(cairo, bw2, bw2);
		cairo_line_to(cairo, w - bw, bw2);
		cairo_set_operator(cairo, CAIRO_OPERATOR_HARD_LIGHT);
		cairo_set_source_rgb(cairo, 0.25, 0.25, 0.25);
		cairo_stroke(cairo);

		cairo_move_to(cairo, w - bw2, 0);
		cairo_line_to(cairo, w - bw2, h - bw2);
		cairo_line_to(cairo, bw, h - bw2);
		cairo_set_operator(cairo, CAIRO_OPERATOR_HARD_LIGHT);
		cairo_set_source_rgb(cairo, 0.75, 0.75, 0.75);
		cairo_stroke(cairo);
		break;
	}

	cairo_surface_flush(buffer->surface);
	cairo_destroy(cairo);

	return buffer;
}

static void
_destroy(struct scaled_scene_buffer *scaled_buffer)
{
	struct scaled_texture_buffer *self = scaled_buffer->data;
	scaled_buffer->data = NULL;
	free(self);
}

static const struct scaled_scene_buffer_impl impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
};

struct scaled_texture_buffer *scaled_texture_buffer_create(
	struct wlr_scene_tree *parent, int width, int height)
{
	assert(parent);
	struct scaled_texture_buffer *self = znew(*self);
	struct scaled_scene_buffer *scaled_buffer = scaled_scene_buffer_create(
		parent, &impl, /* drop_buffer */ true);
	scaled_buffer->data = self;
	self->scaled_buffer = scaled_buffer;
	self->scene_buffer = scaled_buffer->scene_buffer;
	self->width = MAX(width, 1);
	self->height = MAX(height, 1);
	self->texture_conf = (struct texture_config){
		.grad_type = GRAD_TYPE_PYRAMID,
		.border_type = BORDER_TYPE_RAISED,
		.color = {1, 0, 0, 1},
		.color_split_to = {0, 1, 0, 1},
		.color_to = {0, 0, 1, 1},
		.color_to_split_to = {1, 1, 0, 1},
		.border_color = {0, 1, 1},
	};

	scaled_scene_buffer_request_update(scaled_buffer,
		self->width, self->height);

	return self;
}
