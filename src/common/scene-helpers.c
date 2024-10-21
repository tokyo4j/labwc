// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include "common/scene-helpers.h"
#include "labwc.h"
#include "magnifier.h"
#include "output-state.h"

struct wlr_surface *
lab_wlr_surface_from_node(struct wlr_scene_node *node)
{
	struct wlr_scene_buffer *buffer;
	struct wlr_scene_surface *scene_surface;

	if (node && node->type == WLR_SCENE_NODE_BUFFER) {
		buffer = wlr_scene_buffer_from_node(node);
		scene_surface = wlr_scene_surface_try_from_buffer(buffer);
		if (scene_surface) {
			return scene_surface->surface;
		}
	}
	return NULL;
}

struct wlr_scene_node *
lab_wlr_scene_get_prev_node(struct wlr_scene_node *node)
{
	assert(node);
	struct wlr_scene_node *prev;
	prev = wl_container_of(node->link.prev, node, link);
	if (&prev->link == &node->parent->children) {
		return NULL;
	}
	return prev;
}

/*
 * This is a slightly modified copy of scene_output_damage(),
 * required to properly add the magnifier damage to scene_output
 * ->damage_ring and scene_output->pending_commit_damage.
 *
 * The only difference is code style and removal of wlr_output_schedule_frame().
 */
static void
scene_output_damage(struct wlr_scene_output *scene_output,
		const pixman_region32_t *region)
{
	if (!wlr_damage_ring_add(&scene_output->damage_ring, region)) {
		return;
	}

	struct wlr_output *output = scene_output->output;
	enum wl_output_transform transform =
		wlr_output_transform_invert(scene_output->output->transform);

	int width = output->width;
	int height = output->height;
	if (transform & WL_OUTPUT_TRANSFORM_90) {
		width = output->height;
		height = output->width;
	}

	pixman_region32_t frame_damage;
	pixman_region32_init(&frame_damage);
	wlr_region_transform(&frame_damage, region, transform, width, height);

	pixman_region32_union(&scene_output->pending_commit_damage,
		&scene_output->pending_commit_damage, &frame_damage);
	pixman_region32_intersect_rect(&scene_output->pending_commit_damage,
		&scene_output->pending_commit_damage, 0, 0, output->width, output->height);
	pixman_region32_fini(&frame_damage);
}

/*
 * This is a copy of wlr_scene_output_commit()
 * as it doesn't use the pending state at all.
 */
bool
lab_wlr_scene_output_commit(struct wlr_scene_output *scene_output,
		struct wlr_output_state *state)
{
	assert(scene_output);
	assert(state);
	struct wlr_output *wlr_output = scene_output->output;
	struct output *output = wlr_output->data;

	if (!wlr_output->needs_frame
			&& !pixman_region32_not_empty(
				&scene_output->pending_commit_damage)
			&& !magnifier_needs_redraw(output)) {
		return true;
	}

	if (pixman_region32_not_empty(&output->magnifier_damage)) {
		scene_output_damage(scene_output, &output->magnifier_damage);
	}

	if (!wlr_scene_output_build_state(scene_output, state, NULL)) {
		wlr_log(WLR_ERROR, "Failed to build output state for %s",
			wlr_output->name);
		return false;
	}

	pixman_region32_clear(&output->magnifier_damage);

	if (is_magnify_on()) {
		magnify(output, state);
	}

	if (state == &output->pending) {
		if (!wlr_output_commit(wlr_output)) {
			wlr_log(WLR_INFO, "Failed to commit output %s",
				wlr_output->name);
			return false;
		}
	} else if (!wlr_output_commit_state(wlr_output, state)) {
		wlr_log(WLR_INFO, "Failed to commit state for output %s",
			wlr_output->name);
		return false;
	}

	return true;
}
