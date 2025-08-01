// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wlr/render/pixman.h>
#include "buffer.h"
#include "config.h"
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

#define FOR_EACH_STATE(ssd, tmp) FOR_EACH(tmp, \
	&(ssd)->titlebar.active, \
	&(ssd)->titlebar.inactive)

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

	struct wlr_scene_tree *parent;
	struct wlr_buffer *titlebar_fill;
	struct wlr_buffer *corner_top_left;
	struct wlr_buffer *corner_top_right;
	int active;

	ssd->titlebar.tree = wlr_scene_tree_create(ssd->tree);

	struct ssd_sub_tree *subtree;
	FOR_EACH_STATE(ssd, subtree) {
		subtree->tree = wlr_scene_tree_create(ssd->titlebar.tree);
		parent = subtree->tree;
		active = (subtree == &ssd->titlebar.active) ?
			THEME_ACTIVE : THEME_INACTIVE;
		titlebar_fill = &theme->window[active].titlebar_fill->base;
		corner_top_left = &theme->window[active].corner_top_left_normal->base;
		corner_top_right = &theme->window[active].corner_top_right_normal->base;
		wlr_scene_node_set_enabled(&parent->node, active);
		wlr_scene_node_set_position(&parent->node, 0, -theme->titlebar_height);
		wl_list_init(&subtree->parts);

		/* Background */
		struct wlr_scene_buffer *bg_scene_buffer =
			wlr_scene_buffer_create(parent, titlebar_fill);
		/*
		 * Work around the wlroots/pixman bug that widened 1px buffer
		 * becomes translucent when bilinear filtering is used.
		 * TODO: remove once https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3990
		 * is solved
		 */
		if (wlr_renderer_is_pixman(view->server->renderer)) {
			wlr_scene_buffer_set_filter_mode(
				bg_scene_buffer, WLR_SCALE_FILTER_NEAREST);
		}
		struct ssd_part *bg_part =
			add_scene_part(&subtree->parts, LAB_SSD_PART_TITLEBAR);
		bg_part->node = &bg_scene_buffer->node;
		wlr_scene_node_set_position(bg_part->node, corner_width, 0);

		add_scene_buffer(&subtree->parts, LAB_SSD_PART_TITLEBAR_CORNER_LEFT, parent,
			corner_top_left, -rc.theme->border_width, -rc.theme->border_width);
		add_scene_buffer(&subtree->parts, LAB_SSD_PART_TITLEBAR_CORNER_RIGHT, parent,
			corner_top_right, width - corner_width,
			-rc.theme->border_width);

		/* Buttons */
		struct title_button *b;
		int x = theme->window_titlebar_padding_width;

		/* Center vertically within titlebar */
		int y = (theme->titlebar_height - theme->window_button_height) / 2;

		wl_list_for_each(b, &rc.title_buttons_left, link) {
			struct lab_img **imgs =
				theme->window[active].button_imgs[b->type];
			add_scene_button(&subtree->parts, b->type, parent,
				imgs, x, y, view);
			x += theme->window_button_width + theme->window_button_spacing;
		}

		x = width - theme->window_titlebar_padding_width + theme->window_button_spacing;
		wl_list_for_each_reverse(b, &rc.title_buttons_right, link) {
			x -= theme->window_button_width + theme->window_button_spacing;
			struct lab_img **imgs =
				theme->window[active].button_imgs[b->type];
			add_scene_button(&subtree->parts, b->type, parent,
				imgs, x, y, view);
		}
	} FOR_EACH_END

	update_visible_buttons(ssd);

	ssd_update_title(ssd);

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

	struct ssd_part *part;
	struct ssd_sub_tree *subtree;
	int x = enable ? 0 : corner_width;

	FOR_EACH_STATE(ssd, subtree) {
		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLEBAR);
		wlr_scene_node_set_position(part->node, x, 0);
		wlr_scene_buffer_set_dest_size(
			wlr_scene_buffer_from_node(part->node),
			MAX(width - 2 * x, 0), theme->titlebar_height);

		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLEBAR_CORNER_LEFT);
		wlr_scene_node_set_enabled(part->node, !enable);

		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLEBAR_CORNER_RIGHT);
		wlr_scene_node_set_enabled(part->node, !enable);

		/* (Un)round the corner buttons */
		struct title_button *title_button;
		wl_list_for_each(title_button, &rc.title_buttons_left, link) {
			part = ssd_get_part(&subtree->parts, title_button->type);
			struct ssd_button *button = node_ssd_button_from_node(part->node);
			update_button_state(button, LAB_BS_ROUNDED, !enable);
			break;
		}
		wl_list_for_each_reverse(title_button, &rc.title_buttons_right, link) {
			part = ssd_get_part(&subtree->parts, title_button->type);
			struct ssd_button *button = node_ssd_button_from_node(part->node);
			update_button_state(button, LAB_BS_ROUNDED, !enable);
			break;
		}
	} FOR_EACH_END
}

static void
set_alt_button_icon(struct ssd *ssd, enum ssd_part_type type, bool enable)
{
	struct ssd_part *part;
	struct ssd_button *button;
	struct ssd_sub_tree *subtree;

	FOR_EACH_STATE(ssd, subtree) {
		part = ssd_get_part(&subtree->parts, type);
		if (!part) {
			return;
		}

		button = node_ssd_button_from_node(part->node);
		update_button_state(button, LAB_BS_TOGGLED, enable);
	} FOR_EACH_END
}

/*
 * Usually this function just enables all the nodes for buttons, but some
 * buttons can be hidden for small windows (e.g. xterm -geometry 1x1).
 */
static void
update_visible_buttons(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = view->server->theme;
	int width = MAX(view->current.width - 2 * theme->window_titlebar_padding_width, 0);
	int button_width = theme->window_button_width;
	int button_spacing = theme->window_button_spacing;
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
	struct ssd_part *part;
	struct ssd_sub_tree *subtree;
	struct title_button *b;
	FOR_EACH_STATE(ssd, subtree) {
		button_count = 0;
		wl_list_for_each(b, &rc.title_buttons_left, link) {
			part = ssd_get_part(&subtree->parts, b->type);
			wlr_scene_node_set_enabled(part->node,
				button_count < button_count_left);
			button_count++;
		}

		button_count = 0;
		wl_list_for_each_reverse(b, &rc.title_buttons_right, link) {
			part = ssd_get_part(&subtree->parts, b->type);
			wlr_scene_node_set_enabled(part->node,
				button_count < button_count_right);
			button_count++;
		}
	} FOR_EACH_END
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
	struct ssd_part *part;
	struct ssd_sub_tree *subtree;
	struct title_button *b;
	int bg_offset = maximized || squared ? 0 : corner_width;
	FOR_EACH_STATE(ssd, subtree) {
		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLEBAR);
		wlr_scene_buffer_set_dest_size(
			wlr_scene_buffer_from_node(part->node),
			MAX(width - bg_offset * 2, 0), theme->titlebar_height);

		x = theme->window_titlebar_padding_width;
		wl_list_for_each(b, &rc.title_buttons_left, link) {
			part = ssd_get_part(&subtree->parts, b->type);
			wlr_scene_node_set_position(part->node, x, y);
			x += theme->window_button_width + theme->window_button_spacing;
		}

		x = width - corner_width;
		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLEBAR_CORNER_RIGHT);
		wlr_scene_node_set_position(part->node, x, -rc.theme->border_width);

		x = width - theme->window_titlebar_padding_width + theme->window_button_spacing;
		wl_list_for_each_reverse(b, &rc.title_buttons_right, link) {
			part = ssd_get_part(&subtree->parts, b->type);
			x -= theme->window_button_width + theme->window_button_spacing;
			wlr_scene_node_set_position(part->node, x, y);
		}
	} FOR_EACH_END

	ssd_update_title(ssd);
}

void
ssd_titlebar_destroy(struct ssd *ssd)
{
	if (!ssd->titlebar.tree) {
		return;
	}

	struct ssd_sub_tree *subtree;
	FOR_EACH_STATE(ssd, subtree) {
		ssd_destroy_parts(&subtree->parts);
		wlr_scene_node_destroy(&subtree->tree->node);
		subtree->tree = NULL;
	} FOR_EACH_END

	if (ssd->state.title.text) {
		zfree(ssd->state.title.text);
	}

	wlr_scene_node_destroy(&ssd->titlebar.tree->node);
	ssd->titlebar.tree = NULL;
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
	struct ssd_part *part;
	struct ssd_sub_tree *subtree;
	FOR_EACH_STATE(ssd, subtree) {
		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLE);
		if (!part || !part->node) {
			/* view->surface never been mapped */
			/* Or we somehow failed to allocate a scaled titlebar buffer */
			continue;
		}

		buffer_width = part->buffer ? part->buffer->width : 0;
		buffer_height = part->buffer ? part->buffer->height : 0;
		x = offset_left;
		y = (theme->titlebar_height - buffer_height) / 2;

		if (title_bg_width <= 0) {
			wlr_scene_node_set_enabled(part->node, false);
			continue;
		}
		wlr_scene_node_set_enabled(part->node, true);

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
		wlr_scene_node_set_position(part->node, x, y);
	} FOR_EACH_END
}

/*
 * Get left/right offsets of the title area based on visible/hidden states of
 * buttons set in update_visible_buttons().
 */
static void
get_title_offsets(struct ssd *ssd, int *offset_left, int *offset_right)
{
	struct ssd_sub_tree *subtree = &ssd->titlebar.active;
	int button_width = ssd->view->server->theme->window_button_width;
	int button_spacing = ssd->view->server->theme->window_button_spacing;
	int padding_width = ssd->view->server->theme->window_titlebar_padding_width;
	*offset_left = padding_width;
	*offset_right = padding_width;

	struct title_button *b;
	wl_list_for_each(b, &rc.title_buttons_left, link) {
		struct ssd_part *part = ssd_get_part(&subtree->parts, b->type);
		if (part->node->enabled) {
			*offset_left += button_width + button_spacing;
		}
	}
	wl_list_for_each_reverse(b, &rc.title_buttons_right, link) {
		struct ssd_part *part = ssd_get_part(&subtree->parts, b->type);
		if (part->node->enabled) {
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
	const float bg_color[4] = {0, 0, 0, 0}; /* ignored */
	struct font *font = NULL;
	struct ssd_part *part;
	struct ssd_sub_tree *subtree;
	struct ssd_state_title_width *dstate;
	int active;

	int offset_left, offset_right;
	get_title_offsets(ssd, &offset_left, &offset_right);
	int title_bg_width = view->current.width - offset_left - offset_right;

	FOR_EACH_STATE(ssd, subtree) {
		active = (subtree == &ssd->titlebar.active) ?
			THEME_ACTIVE : THEME_INACTIVE;
		dstate = active ? &state->active : &state->inactive;
		text_color = theme->window[active].label_text_color;
		font = active ?  &rc.font_activewindow : &rc.font_inactivewindow;

		if (title_bg_width <= 0) {
			dstate->truncated = true;
			continue;
		}

		if (title_unchanged
				&& !dstate->truncated && dstate->width < title_bg_width) {
			/* title the same + we don't need to resize title */
			continue;
		}

		part = ssd_get_part(&subtree->parts, LAB_SSD_PART_TITLE);
		if (!part) {
			/* Initialize part and wlr_scene_buffer without attaching a buffer */
			part = add_scene_part(&subtree->parts, LAB_SSD_PART_TITLE);
			part->buffer = scaled_font_buffer_create_for_titlebar(
				subtree->tree, theme->titlebar_height,
				theme->window[active].titlebar_pattern);
			if (part->buffer) {
				part->node = &part->buffer->scene_buffer->node;
			} else {
				wlr_log(WLR_ERROR, "Failed to create title node");
			}
		}

		if (part->buffer) {
			scaled_font_buffer_update(part->buffer, title,
				title_bg_width, font,
				text_color, bg_color);
		}

		/* And finally update the cache */
		dstate->width = part->buffer ? part->buffer->width : 0;
		dstate->truncated = title_bg_width <= dstate->width;

	} FOR_EACH_END

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

	if (node && node->data) {
		struct node_descriptor *desc = node->data;
		if (desc->type == LAB_NODE_DESC_SSD_BUTTON) {
			button = node_ssd_button_from_node(node);
			if (button == hover_state->button) {
				/* Cursor is still on the same button */
				return;
			}
		}
	}

	/* Disable old hover */
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

#undef FOR_EACH_STATE
