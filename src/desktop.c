// SPDX-License-Identifier: GPL-2.0-only
#include "config.h"
#include <assert.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "common/scene-helpers.h"
#include "common/surface-helpers.h"
#include "dnd.h"
#include "labwc.h"
#include "layers.h"
#include "node.h"
#include "osd.h"
#include "output.h"
#include "ssd.h"
#include "view.h"
#include "window-rules.h"
#include "workspaces.h"
#include "xwayland.h"

#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

void
desktop_arrange_all_views(struct server *server)
{
	/*
	 * Adjust window positions/sizes. Skip views with no size since
	 * we can't do anything useful with them; they will presumably
	 * be initialized with valid positions/sizes later.
	 *
	 * We do not simply check view->mapped/been_mapped here because
	 * views can have maximized/fullscreen geometry applied while
	 * still unmapped. We do want to adjust the geometry of those
	 * views.
	 */
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!wlr_box_empty(&view->pending)) {
			view_adjust_for_layout_change(view);
		}
	}
}

static void
set_or_offer_focus(struct view *view)
{
	struct seat *seat = &view->server->seat;
	switch (view_wants_focus(view)) {
	case VIEW_WANTS_FOCUS_ALWAYS:
		if (view->surface != seat->seat->keyboard_state.focused_surface) {
			seat_focus_surface(seat, view->surface);
		}
		break;
	case VIEW_WANTS_FOCUS_LIKELY:
	case VIEW_WANTS_FOCUS_UNLIKELY:
		view_offer_focus(view);
		break;
	case VIEW_WANTS_FOCUS_NEVER:
		break;
	}
}

void
desktop_focus_view(struct view *view, bool raise)
{
	assert(view);
	/*
	 * Guard against views with no mapped surfaces when handling
	 * 'request_activate' and 'request_minimize'.
	 */
	if (!view->surface) {
		return;
	}

	if (view->server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER) {
		wlr_log(WLR_DEBUG, "not focusing window while window switching");
		return;
	}

	if (view->minimized) {
		/*
		 * Unminimizing will map the view which triggers a call to this
		 * function again (with raise=true).
		 */
		view_minimize(view, false);
		return;
	}

	if (!view->mapped) {
		return;
	}

	/*
	 * Switch workspace if necessary to make the view visible
	 * (unnecessary for "always on {top,bottom}" views).
	 */
	if (!view_is_always_on_top(view) && !view_is_always_on_bottom(view)) {
		workspaces_switch_to(view->workspace, /*update_focus*/ false);
	}

	if (raise) {
		view_move_to_front(view);
	}

	/*
	 * If any child/sibling of the view is a modal dialog, focus
	 * the dialog instead. It does not need to be raised separately
	 * since view_move_to_front() raises all sibling views together.
	 */
	struct view *dialog = view_get_modal_dialog(view);
	set_or_offer_focus(dialog ? dialog : view);
}

/* TODO: focus layer-shell surfaces also? */
void
desktop_focus_view_or_surface(struct seat *seat, struct view *view,
		struct wlr_surface *surface, bool raise)
{
	assert(view || surface);
	if (view) {
		desktop_focus_view(view, raise);
#if HAVE_XWAYLAND
	} else {
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_try_from_wlr_surface(surface);
		if (xsurface && wlr_xwayland_surface_override_redirect_wants_focus(xsurface)) {
			seat_focus_surface(seat, surface);
		}
#endif
	}
}

struct view *
desktop_topmost_focusable_view(struct server *server)
{
	struct view *view;
	struct wl_list *node_list;
	struct wlr_scene_node *node;
	node_list = &server->workspaces.current->tree->children;
	wl_list_for_each_reverse(node, node_list, link) {
		if (!node->data) {
			/* We found some non-view, most likely the region overlay */
			continue;
		}
		view = node_view_from_node(node);
		if (view->mapped && view_is_focusable(view)) {
			return view;
		}
	}
	return NULL;
}

void
desktop_focus_topmost_view(struct server *server)
{
	struct view *view = desktop_topmost_focusable_view(server);
	if (view) {
		desktop_focus_view(view, /*raise*/ true);
	} else {
		/*
		 * Defocus previous focused surface/view if no longer
		 * focusable (e.g. unmapped or on a different workspace).
		 */
		seat_focus_surface(&server->seat, NULL);
	}
}

void
desktop_focus_output(struct output *output)
{
	if (!output_is_usable(output) || output->server->input_mode
			!= LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}
	struct view *view;
	struct wlr_scene_node *node;
	struct wlr_output_layout *layout = output->server->output_layout;
	struct wl_list *list_head =
		&output->server->workspaces.current->tree->children;
	wl_list_for_each_reverse(node, list_head, link) {
		if (!node->data) {
			continue;
		}
		view = node_view_from_node(node);
		if (!view_is_focusable(view)) {
			continue;
		}
		if (wlr_output_layout_intersects(layout,
				output->wlr_output, &view->current)) {
			desktop_focus_view(view, /*raise*/ false);
			wlr_cursor_warp(view->server->seat.cursor, NULL,
				view->current.x + view->current.width / 2,
				view->current.y + view->current.height / 2);
			cursor_update_focus(view->server);
			return;
		}
	}
	/* No view found on desired output */
	struct wlr_box layout_box;
	wlr_output_layout_get_box(output->server->output_layout,
		output->wlr_output, &layout_box);
	wlr_cursor_warp(output->server->seat.cursor, NULL,
		layout_box.x + output->usable_area.x + output->usable_area.width / 2,
		layout_box.y + output->usable_area.y + output->usable_area.height / 2);
	cursor_update_focus(output->server);
}

void
desktop_update_top_layer_visibility(struct server *server)
{
	struct view *view;
	struct output *output;
	uint32_t top = ZWLR_LAYER_SHELL_V1_LAYER_TOP;

	/* Enable all top layers */
	wl_list_for_each(output, &server->outputs, link) {
		if (!output_is_usable(output)) {
			continue;
		}
		wlr_scene_node_set_enabled(&output->layer_tree[top]->node, true);
	}

	/*
	 * And disable them again when there is a fullscreen view without
	 * any views above it
	 */
	uint64_t outputs_covered = 0;
	for_each_view(view, &server->views, LAB_VIEW_CRITERIA_CURRENT_WORKSPACE) {
		if (view->minimized) {
			continue;
		}
		if (!output_is_usable(view->output)) {
			continue;
		}
		if (view->fullscreen && !(view->outputs & outputs_covered)) {
			wlr_scene_node_set_enabled(
				&view->output->layer_tree[top]->node, false);
		}
		outputs_covered |= view->outputs;
	}
}

static struct wlr_surface *
get_surface_from_layer_node(struct wlr_scene_node *node)
{
	assert(node->data);
	struct node_descriptor *desc = (struct node_descriptor *)node->data;
	if (desc->type == LAB_NODE_DESC_LAYER_SURFACE) {
		struct lab_layer_surface *surface;
		surface = node_layer_surface_from_node(node);
		return surface->scene_layer_surface->layer_surface->surface;
	} else if (desc->type == LAB_NODE_DESC_LAYER_POPUP) {
		struct lab_layer_popup *popup;
		popup = node_layer_popup_from_node(node);
		return popup->wlr_popup->base->surface;
	}
	return NULL;
}

/* TODO: make this less big and scary */
struct cursor_context
get_cursor_context(struct server *server)
{
	struct cursor_context ret = {.type = LAB_SSD_NONE};
	struct wlr_cursor *cursor = server->seat.cursor;

	/* Prevent drag icons to be on top of the hitbox detection */
	if (server->seat.drag.active) {
		dnd_icons_show(&server->seat, false);
	}

	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node,
			cursor->x, cursor->y, &ret.sx, &ret.sy);

	if (server->seat.drag.active) {
		dnd_icons_show(&server->seat, true);
	}

	ret.node = node;
	if (!node) {
		ret.type = LAB_SSD_ROOT;
		return ret;
	}

#if HAVE_XWAYLAND
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_surface *surface = lab_wlr_surface_from_node(node);
		if (node->parent == server->unmanaged_tree) {
			ret.type = LAB_SSD_UNMANAGED;
			ret.surface = surface;
			return ret;
		}
	}
#endif
	while (node) {
		struct node_descriptor *desc = node->data;
		if (desc) {
			switch (desc->type) {
			case LAB_NODE_DESC_VIEW:
			case LAB_NODE_DESC_XDG_POPUP:
				ret.view = desc->data;
				ret.type = ssd_get_part_type(
					ret.view->ssd, ret.node, cursor);
				if (ret.type == LAB_SSD_CLIENT) {
					ret.surface = lab_wlr_surface_from_node(ret.node);
				}
				return ret;
			case LAB_NODE_DESC_SSD_BUTTON: {
				/*
				 * Always return the top scene node for SSD
				 * buttons
				 */
				struct ssd_button *button =
					node_ssd_button_from_node(node);
				ret.node = node;
				ret.type = ssd_button_get_type(button);
				ret.view = ssd_button_get_view(button);
				return ret;
			}
			case LAB_NODE_DESC_LAYER_SURFACE:
				ret.node = node;
				ret.type = LAB_SSD_LAYER_SURFACE;
				ret.surface = get_surface_from_layer_node(node);
				return ret;
			case LAB_NODE_DESC_LAYER_POPUP:
				ret.node = node;
				ret.type = LAB_SSD_CLIENT;
				ret.surface = get_surface_from_layer_node(node);
				return ret;
			case LAB_NODE_DESC_SESSION_LOCK_SURFACE: /* fallthrough */
			case LAB_NODE_DESC_IME_POPUP:
				ret.type = LAB_SSD_CLIENT;
				ret.surface = lab_wlr_surface_from_node(ret.node);
				return ret;
			case LAB_NODE_DESC_MENUITEM:
				/* Always return the top scene node for menu items */
				ret.node = node;
				ret.type = LAB_SSD_MENU;
				return ret;
			case LAB_NODE_DESC_NODE:
			case LAB_NODE_DESC_TREE:
			case LAB_NODE_DESC_SCALED_SCENE_BUFFER:
				break;
			}
		}

		/* Edge-case nodes without node-descriptors */
		if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_surface *surface = lab_wlr_surface_from_node(node);

			/*
			 * Handle layer-shell subsurfaces
			 *
			 * These don't have node-descriptors, but need to be
			 * able to receive pointer actions so we have to process
			 * them here.
			 *
			 * Test by running `gtk-layer-demo -k exclusive`, then
			 * open the 'set margin' dialog and try setting the
			 * margin with the pointer.
			 */
			if (surface && wlr_subsurface_try_from_wlr_surface(surface)
					&& subsurface_parent_layer(surface)) {
				ret.surface = surface;
				ret.type = LAB_SSD_LAYER_SUBSURFACE;
				return ret;
			}
		}

		/* node->parent is always a *wlr_scene_tree */
		node = node->parent ? &node->parent->node : NULL;
	}

	/*
	 * TODO: add node descriptors for the OSDs and reinstate
	 *       wlr_log(WLR_DEBUG, "Unknown node detected");
	 */
	return ret;
}

