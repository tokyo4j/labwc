/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_MESSAGE_BOX_H
#define LABWC_MESSAGE_BOX_H

#include <wayland-server-core.h>

struct server;
struct wlr_scene_node;

struct msg_box {
	struct scaled_font_buffer *font_buffer;
	struct wl_list link;
	struct output *output;
};

void msg_box_create(struct server *server, const char *msg);
void msg_box_remove_from_node(struct wlr_scene_node *node);
void msg_box_destroy(struct wl_list *msg_boxes);

#endif /* LABWC_MESSAGE_BOX_H */