// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-scene-buffer.h"
#include "common/string-helpers.h"
#include "config.h"
#include "config/rcxml.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "node.h"
#include "view.h"

#if HAVE_LIBSFDO

static struct lab_data_buffer *
choose_best_icon_buffer(struct scaled_icon_buffer *self, int icon_size, double scale)
{
	int best_dist = INT_MIN;
	struct lab_data_buffer *best_buffer = NULL;

	struct lab_data_buffer **buffer;
	wl_array_for_each(buffer, &self->view->icon.buffers) {
		int curr_dist = (*buffer)->base.width - (int)(icon_size * scale);
		bool curr_is_better;
		if ((curr_dist < 0 && best_dist > 0)
				|| (curr_dist > 0 && best_dist < 0)) {
			/* prefer too big icon over too small icon */
			curr_is_better = curr_dist > 0;
		} else {
			curr_is_better = abs(curr_dist) < abs(best_dist);
		}
		if (curr_is_better) {
			best_dist = curr_dist;
			best_buffer = *buffer;
		}
	}
	return best_buffer;
}
#endif /* HAVE_LIBSFDO */

static struct lab_data_buffer *
_create_buffer(struct scaled_scene_buffer *scaled_buffer, double scale)
{
#if HAVE_LIBSFDO
	struct scaled_icon_buffer *self = scaled_buffer->data;
	int icon_size = MIN(self->width, self->height);

	if (self->img) {
		lab_img_destroy(self->img);
		self->img = NULL;
	}

	if (self->icon_name) {
		self->img = desktop_entry_load_icon(self->server,
			self->icon_name, icon_size, scale);
	} else if (self->view) {
		if (self->view->icon.name) {
			wlr_log(WLR_DEBUG, "loading icon by name: %s",
				self->view->icon.name);
			self->img = desktop_entry_load_icon(self->server,
				self->view->icon.name, icon_size, scale);
		}
		if (!self->img) {
			struct lab_data_buffer *buffer =
				choose_best_icon_buffer(self, icon_size, scale);
			if (buffer) {
				wlr_log(WLR_DEBUG, "loading icon by buffer");
				return buffer_resize(buffer,
					self->width, self->height, scale);
			}
		}
		if (!self->img) {
			wlr_log(WLR_DEBUG, "loading icon by app_id");
			const char *app_id = view_get_string_prop(self->view, "app_id");
			self->img = desktop_entry_load_icon_from_app_id(self->server,
				app_id, icon_size, scale);
		}
		if (!self->img) {
			wlr_log(WLR_DEBUG, "loading fallback icon");
			self->img = desktop_entry_load_icon(self->server,
				rc.fallback_app_icon_name, icon_size, scale);
		}
	}

	if (!self->img) {
		return NULL;
	}

	struct lab_data_buffer *buffer =
		lab_img_render(self->img, self->width, self->height, scale);

	return buffer;
#else
	return NULL;
#endif /* HAVE_LIBSFDO */
}

static void
_destroy(struct scaled_scene_buffer *scaled_buffer)
{
	struct scaled_icon_buffer *self = scaled_buffer->data;
	if (self->view) {
		wl_list_remove(&self->on_view.set_icon.link);
		wl_list_remove(&self->on_view.destroy.link);
	}
	if (self->img) {
		lab_img_destroy(self->img);
	}
	free(self->icon_name);
	free(self);
}

static struct scaled_scene_buffer_impl impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
};

struct scaled_icon_buffer *
scaled_icon_buffer_create(struct wlr_scene_tree *parent, struct server *server,
	int width, int height)
{
	assert(parent);
	assert(width >= 0 && height >= 0);

	struct scaled_scene_buffer *scaled_buffer = scaled_scene_buffer_create(
		parent, &impl, /* drop_buffer */ true);
	struct scaled_icon_buffer *self = znew(*self);
	self->scaled_buffer = scaled_buffer;
	self->scene_buffer = scaled_buffer->scene_buffer;
	self->server = server;
	self->width = width;
	self->height = height;

	scaled_buffer->data = self;

	return self;
}

static void
handle_view_set_icon(struct wl_listener *listener, void *data)
{
	struct scaled_icon_buffer *self =
		wl_container_of(listener, self, on_view.set_icon);

	scaled_scene_buffer_request_update(self->scaled_buffer,
		self->width, self->height);
}

static void
handle_view_destroy(struct wl_listener *listener, void *data)
{
	struct scaled_icon_buffer *self =
		wl_container_of(listener, self, on_view.destroy);
	wl_list_remove(&self->on_view.destroy.link);
	wl_list_remove(&self->on_view.set_icon.link);
	self->view = NULL;
}

void
scaled_icon_buffer_set_view(struct scaled_icon_buffer *self, struct view *view)
{
	assert(view);
	if (self->view == view) {
		return;
	}

	if (self->view) {
		wl_list_remove(&self->on_view.set_icon.link);
		wl_list_remove(&self->on_view.destroy.link);
	}
	self->view = view;
	self->on_view.set_icon.notify = handle_view_set_icon;
	wl_signal_add(&view->events.set_icon, &self->on_view.set_icon);
	self->on_view.destroy.notify = handle_view_destroy;
	wl_signal_add(&view->events.destroy, &self->on_view.destroy);

	handle_view_set_icon(&self->on_view.set_icon, NULL);
}

void
scaled_icon_buffer_set_icon_name(struct scaled_icon_buffer *self,
	const char *icon_name)
{
	assert(icon_name);
	if (str_equal(self->icon_name, icon_name)) {
		return;
	}
	xstrdup_replace(self->icon_name, icon_name);
	scaled_scene_buffer_request_update(self->scaled_buffer, self->width, self->height);
}

struct scaled_icon_buffer *
scaled_icon_buffer_from_node(struct wlr_scene_node *node)
{
	struct scaled_scene_buffer *scaled_buffer =
		node_scaled_scene_buffer_from_node(node);
	assert(scaled_buffer->impl == &impl);
	return scaled_buffer->data;
}
