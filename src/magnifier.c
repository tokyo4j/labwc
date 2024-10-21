// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/swapchain.h>
#include "common/box.h"
#include "labwc.h"
#include "magnifier.h"
#include "theme.h"

static bool magnifier_enabled = false;

#define MAGNIFIER_SCALE_UNINITIALIZED -1.0
static double mag_scale = MAGNIFIER_SCALE_UNINITIALIZED;

/* Reuse a single scratch buffer */
static struct wlr_buffer *cached_buffer = NULL;

static struct wlr_buffer *get_buffer(struct output *output, int width, int height)
{
	if (cached_buffer && (cached_buffer->width != width
			|| cached_buffer->height != height)) {
		wlr_log(WLR_DEBUG, "tmp magnifier buffer size changed, dropping");
		wlr_buffer_drop(cached_buffer);
		cached_buffer = NULL;
	}

	if (!cached_buffer) {
		cached_buffer = wlr_allocator_create_buffer(
			output->server->allocator, width, height,
			&output->wlr_output->swapchain->format);
	}
	return cached_buffer;
}

void
magnifier_reset(void)
{
	wlr_buffer_drop(cached_buffer);
	cached_buffer = NULL;
}

void
magnifier_draw(struct output *output, struct wlr_output_state *state)
{
	struct server *server = output->server;
	struct theme *theme = server->theme;
	bool fullscreen = (rc.mag_width == -1 || rc.mag_height == -1);
	struct wlr_buffer *output_buffer = state->buffer;
	if (!output_buffer) {
		return;
	}

	struct wlr_box output_box = {
		.x = 0,
		.y = 0,
		.width = output_buffer->width,
		.height = output_buffer->height,
	};

	if (mag_scale == MAGNIFIER_SCALE_UNINITIALIZED) {
		mag_scale = rc.mag_scale;
	}
	assert(mag_scale > 1.0);

	/* Cursor position in unscaled output coordinate */
	double cursor_x = server->seat.cursor->x;
	double cursor_y = server->seat.cursor->y;
	wlr_output_layout_output_coords(server->output_layout, output->wlr_output,
		&cursor_x, &cursor_y);
	cursor_x *= output->wlr_output->scale;
	cursor_y *= output->wlr_output->scale;

	bool cursor_in_output = wlr_box_contains_point(&output_box,
		cursor_x, cursor_y);
	if (fullscreen && !cursor_in_output) {
		return;
	} else if (!fullscreen && cursor_in_output) {
		/*
		 * Schedule frame for other outputs so the magnifier is redrawn
		 * also on them
		 */
		struct output *o;
		wl_list_for_each(o, &server->outputs, link) {
			if (o != output) {
				wlr_output_schedule_frame(o->wlr_output);
			}
		}
	}

	/* Magnifier geometry in unscaled output coordinate */
	struct wlr_box mag_box;
	if (fullscreen) {
		mag_box = output_box;
	} else {
		mag_box.width = rc.mag_width;
		mag_box.height = rc.mag_height;
		mag_box.x = cursor_x - (rc.mag_width / 2.0);
		mag_box.y = cursor_y - (rc.mag_height / 2.0);
		if (!box_intersects(&mag_box, &output_box)) {
			return;
		}
	}

	/* Allocate all the resources before beginning render passes */
	struct wlr_buffer *tmp_buffer =
		get_buffer(output, mag_box.width, mag_box.height);
	if (!tmp_buffer) {
		wlr_log(WLR_ERROR, "Failed to allocate temporary magnifier buffer");
		return;
	}
	struct wlr_texture *tmp_texture =
		wlr_texture_from_buffer(server->renderer, tmp_buffer);
	if (!tmp_texture) {
		wlr_log(WLR_ERROR, "Failed to allocate temporary magnifier texture");
		return;
	}
	struct wlr_texture *output_texture = wlr_texture_from_buffer(
		server->renderer, output_buffer);
	if (!output_texture) {
		wlr_log(WLR_ERROR, "Failed to allocate output texture");
		goto cleanup;
	}

	/* First render pass to copy output content to the temporary buffer */
	struct wlr_render_pass *render_pass = wlr_renderer_begin_buffer_pass(
		server->renderer, tmp_buffer, NULL);
	if (!render_pass) {
		wlr_log(WLR_ERROR, "Failed to begin first render pass");
		goto cleanup;
	}

	/* Source/destination geometries for copying */
	struct wlr_box src_box_for_copy;
	wlr_box_intersection(&src_box_for_copy, &mag_box, &output_box);

	struct wlr_box dst_box_for_copy = src_box_for_copy;
	dst_box_for_copy.x -= mag_box.x;
	dst_box_for_copy.y -= mag_box.y;

	/* Execute copy */
	struct wlr_render_texture_options opts = {
		.texture = output_texture,
		.src_box = box_to_fbox(&src_box_for_copy),
		.dst_box = dst_box_for_copy,
	};
	wlr_render_pass_add_texture(render_pass, &opts);
	if (!wlr_render_pass_submit(render_pass)) {
		wlr_log(WLR_ERROR, "Failed to submit first render pass");
		goto cleanup;
	}

	/* Second render pass to paste the copied content back to the output */
	render_pass = wlr_renderer_begin_buffer_pass(server->renderer,
		output_buffer, NULL);
	if (!render_pass) {
		wlr_log(WLR_ERROR, "Failed to begin second render pass");
		goto cleanup;
	}

	struct wlr_box damage_box;
	if (fullscreen) {
		/* Store the output geometry for the later damage update */
		damage_box = output_box;
		/* Border is not rendered for fullscreened magnifier */
	} else {
		/* Draw borders */
		struct wlr_box border_box = {
			.x = cursor_x - (mag_box.width / 2
				+ theme->mag_border_width),
			.y = cursor_y - (mag_box.height / 2
				+ theme->mag_border_width),
			.width = mag_box.width + theme->mag_border_width * 2,
			.height = mag_box.height + theme->mag_border_width * 2,
		};
		struct wlr_render_rect_options bg_opts = {
			.box = border_box,
			.color = (struct wlr_render_color) {
				.r = theme->mag_border_color[0],
				.g = theme->mag_border_color[1],
				.b = theme->mag_border_color[2],
				.a = theme->mag_border_color[3]
			},
		};
		wlr_render_pass_add_rect(render_pass, &bg_opts);

		wlr_box_intersection(&damage_box, &border_box, &output_box);
	}

	/* Source geometry for pasting */
	struct wlr_fbox src_box_for_paste;
	if (fullscreen) {
		src_box_for_paste.x = cursor_x - (cursor_x / mag_scale);
		src_box_for_paste.y = cursor_y - (cursor_y / mag_scale);
	} else {
		src_box_for_paste.x =
			mag_box.width * (mag_scale - 1.0) / (2.0 * mag_scale);
		src_box_for_paste.y =
			mag_box.height * (mag_scale - 1.0) / (2.0 * mag_scale);
	}
	src_box_for_paste.width = mag_box.width / mag_scale;
	src_box_for_paste.height = mag_box.height / mag_scale;

	/* Execute paste */
	opts = (struct wlr_render_texture_options) {
		.texture = tmp_texture,
		.src_box = src_box_for_paste,
		.dst_box = mag_box,
		.filter_mode = rc.mag_filter ? WLR_SCALE_FILTER_BILINEAR
			: WLR_SCALE_FILTER_NEAREST,
	};
	wlr_render_pass_add_texture(render_pass, &opts);
	if (!wlr_render_pass_submit(render_pass)) {
		wlr_log(WLR_ERROR, "Failed to submit second render pass");
		goto cleanup;
	}

	/* And finally mark the extra damage */
	pixman_region32_union_rect(&output->magnifier_damage,
		&output->magnifier_damage, damage_box.x, damage_box.y,
		damage_box.width, damage_box.height);
	pixman_region32_union(&state->damage, &state->damage,
		&output->magnifier_damage);

cleanup:
	wlr_texture_destroy(tmp_texture);
	wlr_texture_destroy(output_texture);
}

static void
enable_magnifier(struct server *server, bool enable)
{
	if (magnifier_enabled == enable) {
		return;
	}

	magnifier_enabled = enable;
	server->scene->direct_scanout = enable ? false
		: server->direct_scanout_enabled;

	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_lock_software_cursors(output->wlr_output, enable);
	}
}

/* Toggles magnification on and off */
void
magnifier_toggle(struct server *server)
{
	enable_magnifier(server, !magnifier_enabled);

	struct output *output = output_nearest_to_cursor(server);
	if (output) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

/* Increases and decreases magnification scale */
void
magnifier_set_scale(struct server *server, enum magnify_dir dir)
{
	struct output *output = output_nearest_to_cursor(server);

	if (dir == MAGNIFY_INCREASE) {
		if (magnifier_enabled) {
			mag_scale += rc.mag_increment;
		} else {
			enable_magnifier(server, true);
			mag_scale = 1.0 + rc.mag_increment;
		}
	} else {
		if (magnifier_enabled && mag_scale > 1.0 + rc.mag_increment) {
			mag_scale -= rc.mag_increment;
		} else {
			enable_magnifier(server, false);
		}
	}

	if (output) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

/* Report whether magnification is enabled */
bool
magnifier_is_enabled(void)
{
	return magnifier_enabled;
}
