/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_ICON_BUFFER_H
#define LABWC_SCALED_ICON_BUFFER_H

#include <stdbool.h>

struct wlr_scene_tree;
struct wlr_scene_node;
struct wlr_scene_buffer;

struct scaled_icon_buffer {
	struct scaled_scene_buffer *scaled_buffer;
	struct wlr_scene_buffer *scene_buffer;
	struct server *server;
	char *app_id;
	int width;
	int height;
	int padding;
};

/*
 * Create an auto scaling icon buffer, providing a wlr_scene_buffer node for
 * display. It gets destroyed automatically when the backing scaled_scene_buffer
 * is being destroyed which in turn happens automatically when the backing
 * wlr_scene_buffer (or one of its parents) is being destroyed.
 */
struct scaled_icon_buffer *scaled_icon_buffer_create(struct wlr_scene_tree *parent,
	struct server *server, const char *app_id, int width, int height, int padding);

void scaled_icon_buffer_update_app_id(struct scaled_icon_buffer *self,
	const char *app_id);

/* Obtain scaled_icon_buffer from wlr_scene_node */
struct scaled_icon_buffer *scaled_icon_buffer_from_node(struct wlr_scene_node *node);

#endif /* LABWC_SCALED_ICON_BUFFER_H */
