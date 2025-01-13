// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include "buffer.h"
#include "config.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-font-buffer.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-img-buffer.h"
#include "common/scene-helpers.h"
#include "common/string-helpers.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "labwc.h"
#include "node.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

static void set_squared_corners(struct ssd *ssd, bool enable);
static void set_alt_button_icon(struct ssd *ssd, enum ssd_part_type type, bool enable);
static void update_visible_buttons(struct ssd *ssd);

void
ssd_titlebar_create(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = view->server->theme;
	int width = view->current.width;
	int corner_width = ssd_get_corner_width();

	const float *color;
	struct wlr_buffer *corner_top_left;
	struct wlr_buffer *corner_top_right;

	ssd->titlebar.tree = wlr_scene_tree_create(ssd->tree);

	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		color = theme->window[active].title_bg_color;
		corner_top_left = &theme->window[active].corner_top_left_normal->base;
		corner_top_right = &theme->window[active].corner_top_right_normal->base;

		struct ssd_titlebar_subtree *subtree =
			&ssd->titlebar.subtrees[active];
		subtree->tree = wlr_scene_tree_create(ssd->titlebar.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		wlr_scene_node_set_position(&parent->node, 0, -theme->titlebar_height);

		/* Background */
		subtree->bar = wlr_scene_rect_create(parent,
			width - corner_width * 2, theme->titlebar_height, color);
		ssd_node_descriptor_create(&subtree->bar->node,
			LAB_SSD_PART_TITLEBAR);
		wlr_scene_node_set_position(&subtree->bar->node, corner_width, 0);

		subtree->corner_left = wlr_scene_buffer_create(parent, corner_top_left);
		ssd_node_descriptor_create(&subtree->corner_left->node,
			LAB_SSD_PART_TITLEBAR);
		wlr_scene_node_set_position(&subtree->corner_left->node,
			-rc.theme->border_width, -rc.theme->border_width);

		subtree->corner_right = wlr_scene_buffer_create(parent, corner_top_right);
		ssd_node_descriptor_create(&subtree->corner_right->node,
			LAB_SSD_PART_TITLEBAR);
		wlr_scene_node_set_position(&subtree->corner_right->node,
			width - corner_width, -rc.theme->border_width);

		/* Buttons */
		struct title_button *b;
		int x = theme->window_titlebar_padding_width;

		/* Center vertically within titlebar */
		int y = (theme->titlebar_height - theme->window_button_height) / 2;

		wl_list_init(&subtree->buttons_left);
		wl_list_for_each(b, &rc.title_buttons_left, link) {
			struct lab_img **imgs =
				theme->window[active].button_imgs[b->type];
			add_scene_button(b->type, parent, imgs, x, y, view,
				&subtree->buttons_left);
			x += theme->window_button_width + theme->window_button_spacing;
		}

		x = width - theme->window_titlebar_padding_width + theme->window_button_spacing;
		wl_list_init(&subtree->buttons_right);
		wl_list_for_each_reverse(b, &rc.title_buttons_right, link) {
			x -= theme->window_button_width + theme->window_button_spacing;
			struct lab_img **imgs =
				theme->window[active].button_imgs[b->type];
			add_scene_button(b->type, parent, imgs, x, y,
				view, &subtree->buttons_right);
		}
	}

	update_visible_buttons(ssd);

	ssd_update_title(ssd);
	ssd_update_window_icon(ssd);

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);
	if (maximized) {
		set_alt_button_icon(ssd, LAB_SSD_BUTTON_MAXIMIZE, true);
		ssd->state.was_maximized = true;
	}
	if (squared) {
		ssd->state.was_squared = true;
	}
	set_squared_corners(ssd, maximized || squared);

	if (view->shaded) {
		set_alt_button_icon(ssd, LAB_SSD_BUTTON_SHADE, true);
	}

	if (view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_SSD_BUTTON_OMNIPRESENT, true);
	}
}

static void
update_button_state(struct ssd_button *button, enum lab_button_state state,
		bool enable)
{
	if (enable) {
		button->state_set |= state;
	} else {
		button->state_set &= ~state;
	}
	/* Switch the displayed icon buffer to the new one */
	for (uint8_t state_set = LAB_BS_DEFAULT;
			state_set <= LAB_BS_ALL; state_set++) {
		struct scaled_img_buffer *buffer = button->img_buffers[state_set];
		if (!buffer) {
			continue;
		}
		wlr_scene_node_set_enabled(&buffer->scene_buffer->node,
			state_set == button->state_set);
	}
}

static void
set_squared_corners(struct ssd *ssd, bool enable)
{
	struct view *view = ssd->view;
	int width = view->current.width;
	int corner_width = ssd_get_corner_width();
	struct theme *theme = view->server->theme;

	struct ssd_titlebar_subtree *subtree;
	int x = enable ? 0 : corner_width;

	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];

		wlr_scene_node_set_position(&subtree->bar->node, x, 0);
		wlr_scene_rect_set_size(subtree->bar,
			width - 2 * x, theme->titlebar_height);

		wlr_scene_node_set_enabled(&subtree->corner_left->node, !enable);
		wlr_scene_node_set_enabled(&subtree->corner_right->node, !enable);

		/* (Un)round the corner buttons */
		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			update_button_state(button, LAB_BS_ROUNDED, !enable);
			break;
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			update_button_state(button, LAB_BS_ROUNDED, !enable);
			break;
		}
	}
}

static void
set_alt_button_icon(struct ssd *ssd, enum ssd_part_type type, bool enable)
{
	struct ssd_button *button;
	struct ssd_titlebar_subtree *subtree;

	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];

		wl_list_for_each(button, &subtree->buttons_left, link) {
			if (button->type == type) {
				update_button_state(button, LAB_BS_TOGGLED, enable);
			}
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			if (button->type == type) {
				update_button_state(button, LAB_BS_TOGGLED, enable);
			}
		}
	}
}

/*
 * Usually this function just enables all the nodes for buttons, but some
 * buttons can be hidden for small windows (e.g. xterm -geometry 1x1).
 */
static void
update_visible_buttons(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int width = view->current.width - (2 * view->server->theme->window_titlebar_padding_width);
	int button_width = view->server->theme->window_button_width;
	int button_spacing = view->server->theme->window_button_spacing;
	int button_count_left = wl_list_length(&rc.title_buttons_left);
	int button_count_right = wl_list_length(&rc.title_buttons_right);

	/* Make sure infinite loop never occurs */
	assert(button_width > 0);

	/*
	 * The corner-left button is lastly removed as it's usually a window
	 * menu button (or an app icon button in the future).
	 *
	 * There is spacing to the inside of each button, including between the
	 * innermost buttons and the window title. See also get_title_offsets().
	 */
	while (width < ((button_width + button_spacing)
			* (button_count_left + button_count_right))) {
		if (button_count_left > button_count_right) {
			button_count_left--;
		} else {
			button_count_right--;
		}
	}

	int button_count;
	struct ssd_titlebar_subtree *subtree;
	struct ssd_button *button;
	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];

		button_count = 0;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			wlr_scene_node_set_enabled(&button->tree->node,
				button_count < button_count_left);
			button_count++;
		}

		button_count = 0;
		wl_list_for_each(button, &subtree->buttons_right, link) {
			wlr_scene_node_set_enabled(&button->tree->node,
				button_count < button_count_right);
			button_count++;
		}
	}
}

void
ssd_titlebar_update(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int width = view->current.width;
	int corner_width = ssd_get_corner_width();
	struct theme *theme = view->server->theme;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);

	if (ssd->state.was_maximized != maximized
			|| ssd->state.was_squared != squared) {
		set_squared_corners(ssd, maximized || squared);
		if (ssd->state.was_maximized != maximized) {
			set_alt_button_icon(ssd, LAB_SSD_BUTTON_MAXIMIZE, maximized);
		}
		ssd->state.was_maximized = maximized;
		ssd->state.was_squared = squared;
	}

	if (ssd->state.was_shaded != view->shaded) {
		set_alt_button_icon(ssd, LAB_SSD_BUTTON_SHADE, view->shaded);
		ssd->state.was_shaded = view->shaded;
	}

	if (ssd->state.was_omnipresent != view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_SSD_BUTTON_OMNIPRESENT,
			view->visible_on_all_workspaces);
		ssd->state.was_omnipresent = view->visible_on_all_workspaces;
	}

	if (width == ssd->state.geometry.width) {
		return;
	}

	update_visible_buttons(ssd);

	/* Center buttons vertically within titlebar */
	int y = (theme->titlebar_height - theme->window_button_height) / 2;
	int x;
	struct ssd_titlebar_subtree *subtree;
	int bg_offset = maximized || squared ? 0 : corner_width;
	struct ssd_button *button;

	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];

		wlr_scene_rect_set_size(subtree->bar,
			width - bg_offset * 2, theme->titlebar_height);

		x = theme->window_titlebar_padding_width;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			wlr_scene_node_set_position(&button->tree->node, x, y);
			x += theme->window_button_width + theme->window_button_spacing;
		}

		x = width - corner_width;
		wlr_scene_node_set_position(&subtree->corner_right->node,
			x, -rc.theme->border_width);

		x = width - theme->window_titlebar_padding_width + theme->window_button_spacing;
		wl_list_for_each(button, &subtree->buttons_right, link) {
			x -= theme->window_button_width + theme->window_button_spacing;
			wlr_scene_node_set_position(&button->tree->node, x, y);
		}
	}

	ssd_update_title(ssd);
	ssd_update_window_icon(ssd);
}

void
ssd_titlebar_destroy(struct ssd *ssd)
{
	if (!ssd->titlebar.tree) {
		return;
	}

	zfree(ssd->state.title.text);
	zfree(ssd->state.app_id);
	wlr_scene_node_destroy(&ssd->titlebar.tree->node);
	ssd->titlebar = (struct ssd_titlebar_scene){0};
}

/*
 * For ssd_update_title* we do not early out because
 * .active and .inactive may result in different sizes
 * of the title (font family/size) or background of
 * the title (different button/border width).
 *
 * Both, wlr_scene_node_set_enabled() and wlr_scene_node_set_position()
 * check for actual changes and return early if there is no change in state.
 * Always using wlr_scene_node_set_enabled(node, true) will thus not cause
 * any unnecessary screen damage and makes the code easier to follow.
 */

static void
ssd_update_title_positions(struct ssd *ssd, int offset_left, int offset_right)
{
	struct view *view = ssd->view;
	struct theme *theme = view->server->theme;
	int width = view->current.width;
	int title_bg_width = width - offset_left - offset_right;

	int x, y;
	int buffer_height, buffer_width;
	struct ssd_titlebar_subtree *subtree;
	struct scaled_font_buffer *title;
	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];
		title = subtree->title;

		if (!title) {
			/* view->surface never been mapped */
			/* Or we somehow failed to allocate a scaled titlebar buffer */
			continue;
		}

		buffer_width = title->width;
		buffer_height = title->height;
		x = offset_left;
		y = (theme->titlebar_height - buffer_height) / 2;

		if (title_bg_width <= 0) {
			wlr_scene_node_set_enabled(&title->scene_buffer->node, false);
			continue;
		}
		wlr_scene_node_set_enabled(&title->scene_buffer->node, true);

		if (theme->window_label_text_justify == LAB_JUSTIFY_CENTER) {
			if (buffer_width + MAX(offset_left, offset_right) * 2 <= width) {
				/* Center based on the full width */
				x = (width - buffer_width) / 2;
			} else {
				/*
				 * Center based on the width between the buttons.
				 * Title jumps around once this is hit but its still
				 * better than to hide behind the buttons on the right.
				 */
				x += (title_bg_width - buffer_width) / 2;
			}
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_RIGHT) {
			x += title_bg_width - buffer_width;
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_LEFT) {
			/* TODO: maybe add some theme x padding here? */
		}
		wlr_scene_node_set_position(&title->scene_buffer->node, x, y);
	}
}

/*
 * Get left/right offsets of the title area based on visible/hidden states of
 * buttons set in update_visible_buttons().
 */
static void
get_title_offsets(struct ssd *ssd, int *offset_left, int *offset_right)
{
	struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[THEME_INACTIVE];
	int button_width = ssd->view->server->theme->window_button_width;
	int button_spacing = ssd->view->server->theme->window_button_spacing;
	int padding_width = ssd->view->server->theme->window_titlebar_padding_width;
	*offset_left = padding_width;
	*offset_right = padding_width;

	struct ssd_button *button;
	wl_list_for_each(button, &subtree->buttons_left, link) {
		if (button->tree->node.enabled) {
			*offset_left += button_width + button_spacing;
		}
	}
	wl_list_for_each(button, &subtree->buttons_right, link) {
		if (button->tree->node.enabled) {
			*offset_right += button_width + button_spacing;
		}
	}
}

void
ssd_update_title(struct ssd *ssd)
{
	if (!ssd || !rc.show_title) {
		return;
	}

	struct view *view = ssd->view;
	char *title = (char *)view_get_string_prop(view, "title");
	if (string_null_or_empty(title)) {
		return;
	}

	struct theme *theme = view->server->theme;
	struct ssd_state_title *state = &ssd->state.title;
	bool title_unchanged = state->text && !strcmp(title, state->text);

	const float *text_color;
	const float *bg_color;
	struct font *font = NULL;
	struct ssd_titlebar_subtree *subtree;
	struct ssd_state_title_width *dstate;

	int offset_left, offset_right;
	get_title_offsets(ssd, &offset_left, &offset_right);
	int title_bg_width = view->current.width - offset_left - offset_right;

	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];
		dstate = &state->dstates[active];
		text_color = theme->window[active].label_text_color;
		bg_color = theme->window[active].title_bg_color;
		font = active ? &rc.font_activewindow : &rc.font_inactivewindow;

		if (title_bg_width <= 0) {
			dstate->truncated = true;
			continue;
		}

		if (title_unchanged
				&& !dstate->truncated && dstate->width < title_bg_width) {
			/* title the same + we don't need to resize title */
			continue;
		}

		if (!subtree->title) {
			/* Initialize part and wlr_scene_buffer without attaching a buffer */
			subtree->title = scaled_font_buffer_create(subtree->tree);
			ssd_node_descriptor_create(&subtree->title->scene_buffer->node,
				LAB_SSD_PART_TITLE);
			if (!subtree->title) {
				wlr_log(WLR_ERROR, "Failed to create title node");
			}
		}

		if (subtree->title) {
			scaled_font_buffer_update(subtree->title, title,
				title_bg_width, font,
				text_color, bg_color);
		}

		/* And finally update the cache */
		dstate->width = subtree->title ? subtree->title->width : 0;
		dstate->truncated = title_bg_width <= dstate->width;
	}

	if (!title_unchanged) {
		if (state->text) {
			free(state->text);
		}
		state->text = xstrdup(title);
	}
	ssd_update_title_positions(ssd, offset_left, offset_right);
}

void
ssd_update_button_hover(struct wlr_scene_node *node,
		struct ssd_hover_state *hover_state)
{
	struct ssd_button *button = NULL;
	if (!node || !node->data) {
		goto disable_old_hover;
	}

	struct node_descriptor *desc = node->data;
	if (desc->type == LAB_NODE_DESC_SSD_BUTTON) {
		button = node_ssd_button_from_node(node);
		if (button == hover_state->button) {
			/* Cursor is still on the same button */
			return;
		}
	}

disable_old_hover:
	if (hover_state->button) {
		update_button_state(hover_state->button, LAB_BS_HOVERD, false);
		hover_state->view = NULL;
		hover_state->button = NULL;
	}
	if (button) {
		update_button_state(button, LAB_BS_HOVERD, true);
		hover_state->view = button->view;
		hover_state->button = button;
	}
}

bool
ssd_should_be_squared(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int corner_width = ssd_get_corner_width();

	return (view_is_tiled_and_notify_tiled(view)
			|| view->current.width < corner_width * 2)
		&& view->maximized != VIEW_AXIS_BOTH;
}

void
ssd_update_window_icon(struct ssd *ssd)
{
#if HAVE_LIBSFDO
	if (!ssd) {
		return;
	}

	const char *app_id = view_get_string_prop(ssd->view, "app_id");
	if (string_null_or_empty(app_id)) {
		return;
	}
	if (ssd->state.app_id && !strcmp(ssd->state.app_id, app_id)) {
		return;
	}

	free(ssd->state.app_id);
	ssd->state.app_id = xstrdup(app_id);

	struct ssd_titlebar_subtree *subtree;
	for (int active = THEME_INACTIVE; active <= THEME_ACTIVE; active++) {
		subtree = &ssd->titlebar.subtrees[active];

		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			if (button->type == LAB_SSD_BUTTON_WINDOW_ICON) {
				scaled_icon_buffer_set_app_id(
					button->window_icon, app_id);
			}
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			if (button->type == LAB_SSD_BUTTON_WINDOW_ICON) {
				scaled_icon_buffer_set_app_id(
					button->window_icon, app_id);
			}
		}
	}
#endif
}
