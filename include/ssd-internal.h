/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_INTERNAL_H
#define LABWC_SSD_INTERNAL_H

#include <wlr/util/box.h>
#include "ssd.h"
#include "view.h"

struct ssd_button {
	struct wlr_scene_tree *tree;
	struct view *view;
	enum ssd_part_type type;
	/*
	 * Bitmap of lab_button_state that represents a combination of
	 * hover/toggled/rounded states.
	 */
	uint8_t state_set;
	/*
	 * Image buffers for each combination of hover/toggled/rounded states.
	 * img_buffers[state_set] is displayed. Some of these can be NULL
	 * (e.g. img_buffers[LAB_BS_ROUNDED] is set only for corner buttons).
	 *
	 * When "type" is LAB_SSD_BUTTON_WINDOW_ICON, these are all NULL and
	 * window_icon is used instead.
	 */
	struct scaled_img_buffer *img_buffers[LAB_BS_ALL + 1];

	struct scaled_icon_buffer *window_icon;

	struct wl_listener destroy;

	struct wl_list link; /* ssd_titlebar_subtree.buttons_{left,right} */
};

struct ssd_state_title_width {
	int width;
	bool truncated;
};

struct ssd {
	struct view *view;
	struct wlr_scene_tree *tree;

	/*
	 * Cache for current values.
	 * Used to detect actual changes so we
	 * don't update things we don't have to.
	 */
	struct {
		/* Button icons need to be swapped on shade or omnipresent toggles */
		bool was_shaded;
		bool was_omnipresent;

		/*
		 * Corners need to be (un)rounded and borders need be shown/hidden
		 * when toggling maximization, and the button needs to be swapped on
		 * maximization toggles.
		 */
		bool was_maximized;

		/*
		 * Corners need to be (un)rounded but borders should be kept shown when
		 * the window is (un)tiled and notified about it or when the window may
		 * become so small that only a squared scene-rect can be used to render
		 * such a small titlebar.
		 */
		bool was_squared;

		struct wlr_box geometry;
		struct ssd_state_title {
			char *text;
			/* indexed by THEME_(IN)ACTIVE */
			struct ssd_state_title_width dstates[2];
		} title;

		char *app_id;
	} state;

	/* An invisible area around the view which allows resizing */
	struct ssd_extents_scene {
		struct wlr_scene_tree *tree;
		struct wlr_scene_rect *top, *bottom, *left, *right,
			*topleft, *topright, *bottomleft, *bottomright;
	} extents;

	/* The top of the view, containing buttons, title, .. */
	struct ssd_titlebar_scene {
		int height;
		struct wlr_scene_tree *tree;
		struct ssd_titlebar_subtree {
			struct wlr_scene_tree *tree;
			struct wlr_scene_buffer *corner_left;
			struct wlr_scene_buffer *corner_right;
			struct wlr_scene_rect *bar;
			struct wl_list buttons_left; /* struct ssd_buttons.link */
			struct wl_list buttons_right; /* struct ssd_buttons.link */
			struct scaled_font_buffer *title;
		} subtrees[2]; /* indexed by THEME_(IN)ACTIVE */
	} titlebar;

	/* Borders allow resizing as well */
	struct ssd_border_scene {
		struct wlr_scene_tree *tree;
		struct ssd_border_subtree {
			struct wlr_scene_tree *tree;
			struct wlr_scene_rect *top, *bottom, *left, *right;
		} subtrees[2]; /* indexed by THEME_(IN)ACTIVE */
	} border;

	struct ssd_shadow_scene {
		struct wlr_scene_tree *tree;
		struct ssd_shadow_subtree {
			struct wlr_scene_tree *tree;
			struct wlr_scene_buffer *top, *bottom, *left, *right,
				*topleft, *topright, *bottomleft, *bottomright;
		} subtrees[2]; /* indexed by THEME_(IN)ACTIVE */
	} shadow;

	/*
	 * Space between the extremities of the view's wlr_surface
	 * and the max extents of the server-side decorations.
	 * For xdg-shell views with CSD, this margin is zero.
	 */
	struct border margin;
};

struct ssd_hover_state {
	struct view *view;
	struct ssd_button *button;
};

struct wlr_buffer;
struct wlr_scene_tree;

struct ssd_button *add_scene_button(enum ssd_part_type type,
	struct wlr_scene_tree *parent, struct lab_img *imgs[LAB_BS_ALL + 1],
	int x, int y, struct view *view, struct wl_list *list);

/* SSD internal */
void ssd_titlebar_create(struct ssd *ssd);
void ssd_titlebar_update(struct ssd *ssd);
void ssd_titlebar_destroy(struct ssd *ssd);
bool ssd_should_be_squared(struct ssd *ssd);

void ssd_border_create(struct ssd *ssd);
void ssd_border_update(struct ssd *ssd);
void ssd_border_destroy(struct ssd *ssd);

void ssd_extents_create(struct ssd *ssd);
void ssd_extents_update(struct ssd *ssd);
void ssd_extents_destroy(struct ssd *ssd);

void ssd_shadow_create(struct ssd *ssd);
void ssd_shadow_update(struct ssd *ssd);
void ssd_shadow_destroy(struct ssd *ssd);

void ssd_node_descriptor_create(struct wlr_scene_node *node,
	enum ssd_part_type type);

#endif /* LABWC_SSD_INTERNAL_H */
