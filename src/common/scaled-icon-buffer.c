// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include "common/list.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-scene-buffer.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "node.h"

static struct wl_list cached_buffers = WL_LIST_INIT(&cached_buffers);

static struct lab_data_buffer *
_create_buffer(struct scaled_scene_buffer *scaled_buffer, double scale)
{
	struct scaled_icon_buffer *self = scaled_buffer->data;
	if (!self->app_id) {
		return NULL;
	}

	int icon_size = MIN(self->width - 2 * self->padding,
				self->height - 2 * self->padding);
	struct lab_img *img = desktop_entry_icon_lookup(self->server,
		self->app_id, icon_size, scale);
	if (!img) {
		/* Fall back to labwc icon */
		img = desktop_entry_icon_lookup(self->server,
			"labwc", icon_size, scale);
	}
	if (!img) {
		return NULL;
	}

	struct lab_data_buffer *buffer = lab_img_render(img,
		self->width, self->height, self->padding, scale);
	lab_img_destroy(img);

	return buffer;
}

static void
_destroy(struct scaled_scene_buffer *scaled_buffer)
{
	struct scaled_icon_buffer *self = scaled_buffer->data;
	free(self);
}

static bool
_equal(struct scaled_scene_buffer *scaled_buffer_a,
	struct scaled_scene_buffer *scaled_buffer_b)
{
	struct scaled_icon_buffer *a = scaled_buffer_a->data;
	struct scaled_icon_buffer *b = scaled_buffer_b->data;

	return (a->app_id == b->app_id || !strcmp(a->app_id, b->app_id))
		&& a->width == b->width
		&& a->height == b->height
		&& a->padding == b->padding;
}

static struct scaled_scene_buffer_impl impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
	.equal = _equal,
};

struct scaled_icon_buffer *
scaled_icon_buffer_create(struct wlr_scene_tree *parent, struct server *server,
	const char *app_id, int width, int height, int padding)
{
	struct scaled_scene_buffer *scaled_buffer = scaled_scene_buffer_create(
		parent, &impl, &cached_buffers, /* drop_buffer */ true);
	struct scaled_icon_buffer *self = znew(*self);
	self->scaled_buffer = scaled_buffer;
	self->scene_buffer = scaled_buffer->scene_buffer;
	self->server = server;
	self->app_id = app_id ? xstrdup(app_id) : NULL;
	self->width = width;
	self->height = height;
	self->padding = padding;

	scaled_buffer->data = self;

	if (app_id) {
		scaled_scene_buffer_request_update(scaled_buffer, width, height);
	}

	return self;
}

void
scaled_icon_buffer_update_app_id(struct scaled_icon_buffer *self,
	const char *app_id)
{
	free(self->app_id);
	self->app_id = xstrdup(app_id);
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
