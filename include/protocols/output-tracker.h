/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PROTOCOLS_OUTPUT_TRACKER_H
#define LABWC_PROTOCOLS_OUTPUT_TRACKER_H

struct output_tracker;
struct wl_client;
struct wl_list;
struct wl_resource;
struct wlr_output;

struct output_tracker_impl {
	void (*send_output_enter)(struct wl_resource *object, struct wl_resource *output);
	void (*send_output_leave)(struct wl_resource *object, struct wl_resource *output);
	/* If only_to_client is NULL, broadcast done event to all resources of object */
	void (*send_done)(void *object, struct wl_client *only_to_client);
};

struct output_tracker *output_tracker_create(void *object,
	struct wl_list *object_resources,
	const struct output_tracker_impl *impl);

void output_tracker_enter(struct output_tracker *output_tracker,
	struct wlr_output *wlr_output);

void output_tracker_leave(struct output_tracker *output_tracker,
	struct wlr_output *wlr_output);

void output_tracker_send_initial_state_to_resource(
	struct output_tracker *output_tracker,
	struct wl_resource *object_resource);

void output_tracker_destroy(struct output_tracker *output_tracker);

#endif  // LABWC_PROTOCOLS_OUTPUT_TRACKER_H
