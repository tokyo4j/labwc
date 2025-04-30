/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_RECT_BUFFER_H
#define LABWC_SCALED_RECT_BUFFER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct scaled_scene_buffer;

struct texture_config {
	enum gradient_type {
		GRAD_TYPE_NONE = 0,
		GRAD_TYPE_DIAGONAL,
		GRAD_TYPE_CROSS_DIAGONAL,
		GRAD_TYPE_PYRAMID,
		GRAD_TYPE_MIRROR_HORIZONTAL,
		GRAD_TYPE_HORIZONTAL,
		GRAD_TYPE_SPLIT_VERTICAL,
		GRAD_TYPE_VERTICAL,
	} grad_type;
	enum border_type {
		BORDER_TYPE_NONE = 0, /* no border */
		BORDER_TYPE_FLAT,
		BORDER_TYPE_RAISED,
		BORDER_TYPE_SUNKEN,
	} border_type;
	// bool bevel2;
	// bool interfaced;
	float color[4];
	float color_split_to[4];
	float color_to[4];
	float color_to_split_to[4];
	float border_color[4];
};

struct scaled_texture_buffer {
	struct wlr_scene_buffer *scene_buffer;
	struct scaled_scene_buffer *scaled_buffer;
	int width;
	int height;
	struct texture_config texture_conf;
};

struct scaled_texture_buffer *scaled_texture_buffer_create(
	struct wlr_scene_tree *parent, int width, int height);

#endif /* LABWC_SCALED_RECT_BUFFER_H */
