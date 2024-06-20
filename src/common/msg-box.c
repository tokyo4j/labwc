// SPDX-License-Identifier: GPL-2.0-only
#include "common/list.h"
#include "common/mem.h"
#include "common/msg-box.h"
#include "common/scaled-font-buffer.h"
#include "labwc.h"
#include "node.h"

static void
arrange_msg_boxes(struct output *output)
{
	struct msg_box *msg_box;
	int y = 3;
	wl_list_for_each(msg_box, &output->msg_boxes, link) {
		wlr_scene_node_set_position(
			&msg_box->font_buffer->scene_buffer->node, 3, y);
		y += msg_box->font_buffer->height + 3;
	}
}

void
msg_box_create(struct server *server, const char *msg)
{
	struct msg_box *msg_box = znew(*msg_box);
	msg_box->output = output_nearest_to_cursor(server);
	msg_box->font_buffer = scaled_font_buffer_create(msg_box->output->msg_tree);
	node_descriptor_create(&msg_box->font_buffer->scene_buffer->node,
		LAB_NODE_DESC_MSGBOX, msg_box);

	struct theme *theme = server->theme;
	scaled_font_buffer_update(msg_box->font_buffer, msg,
		font_width(&rc.font_osd, msg), &rc.font_osd,
		theme->osd_label_text_color, theme->osd_bg_color, NULL);

	wl_list_append(&msg_box->output->msg_boxes, &msg_box->link);
	arrange_msg_boxes(msg_box->output);
}

void
msg_box_remove_from_node(struct wlr_scene_node *node)
{
	struct msg_box *msg_box = node_msg_box_from_node(node);

	wl_list_remove(&msg_box->link);
	arrange_msg_boxes(msg_box->output);

	wlr_scene_node_destroy(&msg_box->font_buffer->scene_buffer->node);
	free(msg_box);
}

void
msg_box_destroy(struct wl_list *msg_boxes)
{
	struct msg_box *msg_box, *tmp;
	wl_list_for_each_safe(msg_box, tmp, msg_boxes, link) {
		wlr_scene_node_destroy(&msg_box->font_buffer->scene_buffer->node);
		free(msg_box);
	}
}
