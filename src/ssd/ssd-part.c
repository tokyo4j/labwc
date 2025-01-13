// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include "common/list.h"
#include "common/mem.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-img-buffer.h"
#include "node.h"
#include "ssd-internal.h"

/* Internal helpers */
static void
ssd_button_destroy_notify(struct wl_listener *listener, void *data)
{
	struct ssd_button *button = wl_container_of(listener, button, destroy);
	wl_list_remove(&button->destroy.link);
	wl_list_remove(&button->link);
	free(button);
}

/*
 * Create a new node_descriptor containing a link to a new ssd_button struct.
 * Both will be destroyed automatically once the scene_node they are attached
 * to is destroyed.
 */
static struct ssd_button *
ssd_button_descriptor_create(struct wlr_scene_node *node, struct wl_list *list)
{
	/* Create new ssd_button */
	struct ssd_button *button = znew(*button);

	/* Let it destroy automatically when the scene node destroys */
	button->destroy.notify = ssd_button_destroy_notify;
	wl_signal_add(&node->events.destroy, &button->destroy);

	wl_list_append(list, &button->link);

	/* And finally attach the ssd_button to a node descriptor */
	node_descriptor_create(node, LAB_NODE_DESC_SSD_BUTTON, button);
	return button;
}

struct ssd_button *
add_scene_button(enum ssd_part_type type,
		struct wlr_scene_tree *parent,
		struct lab_img *imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view, struct wl_list *list)
{
	struct wlr_scene_tree *button_root = wlr_scene_tree_create(parent);
	wlr_scene_node_set_position(&button_root->node, x, y);
	struct ssd_button *button = ssd_button_descriptor_create(&button_root->node, list);
	button->tree = button_root;
	button->type = type;
	button->view = view;

	/* Hitbox */
	float invisible[4] = { 0, 0, 0, 0 };
	wlr_scene_rect_create(button_root, rc.theme->window_button_width,
		rc.theme->window_button_height, invisible);

	/* Icons */
	int button_width = rc.theme->window_button_width;
	int button_height = rc.theme->window_button_height;
	/*
	 * Ensure a small amount of horizontal padding within the button
	 * area (2px on each side with the default 26px button width).
	 * A new theme setting could be added to configure this. Using
	 * an existing setting (padding.width or window.button.spacing)
	 * was considered, but these settings have distinct purposes
	 * already and are zero by default.
	 */
	int icon_padding = button_width / 10;

	if (type == LAB_SSD_BUTTON_WINDOW_ICON) {
		struct scaled_icon_buffer *icon_buffer =
			scaled_icon_buffer_create(button_root, view->server,
				button_width - 2 * icon_padding, button_height);
		assert(icon_buffer);
		wlr_scene_node_set_position(
			&icon_buffer->scene_buffer->node, icon_padding, 0);
		button->window_icon = icon_buffer;
	} else {
		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set]) {
				continue;
			}
			struct scaled_img_buffer *img_buffer = scaled_img_buffer_create(
				button_root, imgs[state_set], rc.theme->window_button_width,
				rc.theme->window_button_height);
			assert(img_buffer);
			wlr_scene_node_set_enabled(
				&img_buffer->scene_buffer->node, false);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}

	return button;
}
