/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_CORNER_BUFFER_H
#define LABWC_SCALED_CORNER_BUFFER_H

#include <stdint.h>

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct scaled_scene_buffer;

enum lab_corner {
	LAB_CORNER_TOP_LEFT,
	LAB_CORNER_TOP_RIGHT,
};

struct scaled_corner_buffer {
	struct wlr_scene_buffer *scene_buffer;
	struct scaled_scene_buffer *scaled_buffer;
	int width;
	int height;
	int border_width;
	int corner_radius;
	enum lab_corner corner;
	float fill_color[4];
	float border_color[4];
};

struct scaled_corner_buffer *scaled_corner_buffer_create(
	struct wlr_scene_tree *parent, int width, int height, int border_width,
	int corner_radius, enum lab_corner corner,
	float fill_color[4], float border_color[4]);

#endif /* LABWC_SCALED_CORNER_BUFFER_H */
