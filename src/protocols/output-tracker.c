// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_output.h>
#include "common/mem.h"
#include "protocols/output-tracker.h"

struct output_tracker {
	void *object;
	struct wl_list *object_resources;
	struct wl_list entered_outputs; /* struct output_tracker_output.link */
	const struct output_tracker_impl *impl;
};

struct output_tracker_output {
	struct wlr_output *wlr_output;
	struct output_tracker *output_tracker;
	struct {
		struct wl_listener output_bind;
		struct wl_listener output_destroy;
	} on;
	struct wl_list link;
};

/* Internal helpers */
static bool
object_output_send_event(struct wl_list *object_resources, struct wl_list *output_resources,
		void (*notifier)(struct wl_resource *object, struct wl_resource *output))
{
	bool sent = false;
	struct wl_client *client;
	struct wl_resource *object_resource, *output_resource;
	wl_resource_for_each(object_resource, object_resources) {
		client = wl_resource_get_client(object_resource);
		wl_resource_for_each(output_resource, output_resources) {
			if (wl_resource_get_client(output_resource) == client) {
				notifier(object_resource, output_resource);
				sent = true;
			}
		}
	}
	return sent;
}

static void
_object_output_destroy(struct output_tracker_output *output)
{
	struct output_tracker *tracker = output->output_tracker;

	object_output_send_event(
		tracker->object_resources,
		&output->wlr_output->resources,
		tracker->impl->send_output_leave);

	wl_list_remove(&output->link);
	wl_list_remove(&output->on.output_bind.link);
	wl_list_remove(&output->on.output_destroy.link);

	if (tracker->impl->send_done) {
		tracker->impl->send_done(tracker->object, /*client*/ NULL);
	}

	free(output);
}

/* Internal handlers */
static void
handle_output_destroy(struct wl_listener *listener, void *data)
{
	struct output_tracker_output *output =
		wl_container_of(listener, output, on.output_destroy);
	_object_output_destroy(output);
}

static void
handle_output_bind(struct wl_listener *listener, void *data)
{
	struct output_tracker_output *output =
		wl_container_of(listener, output, on.output_bind);

	struct output_tracker *tracker = output->output_tracker;
	struct wlr_output_event_bind *event = data;
	struct wl_resource *output_resource = event->resource;
	struct wl_client *client = wl_resource_get_client(output_resource);

	bool sent = false;
	struct wl_resource *object_resource;
	wl_resource_for_each(object_resource, tracker->object_resources) {
		if (wl_resource_get_client(object_resource) == client) {
			tracker->impl->send_output_enter(
				object_resource, output_resource);
			sent = true;
		}
	}
	if (sent && tracker->impl->send_done) {
		tracker->impl->send_done(tracker->object, client);
	}
}

/* Public API */
void
output_tracker_send_initial_state_to_resource(struct output_tracker *tracker,
		struct wl_resource *object_resource)
{
	struct wl_client *client = wl_resource_get_client(object_resource);

	struct output_tracker_output *output;
	wl_list_for_each(output, &tracker->entered_outputs, link) {
		struct wl_resource *output_resource;
		wl_resource_for_each(output_resource, &output->wlr_output->resources) {
			if (wl_resource_get_client(output_resource) != client) {
				continue;
			}
			tracker->impl->send_output_enter(object_resource, output_resource);
		}
	}
}

struct output_tracker *
output_tracker_create(void *object, struct wl_list *object_resources,
		const struct output_tracker_impl *impl)
{
	assert(impl);
	assert(impl->send_output_enter);
	assert(impl->send_output_leave);

	struct output_tracker *output_tracker = znew(*output_tracker);
	output_tracker->impl = impl;
	output_tracker->object = object;
	output_tracker->object_resources = object_resources;
	wl_list_init(&output_tracker->entered_outputs);

	return output_tracker;
}

void
output_tracker_enter(struct output_tracker *tracker,
		struct wlr_output *wlr_output)
{
	struct output_tracker_output *output = znew(*output);
	output->wlr_output = wlr_output;
	output->output_tracker = tracker;
	wl_list_insert(&tracker->entered_outputs, &output->link);

	output->on.output_bind.notify = handle_output_bind;
	wl_signal_add(&wlr_output->events.bind, &output->on.output_bind);

	output->on.output_destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->on.output_destroy);

	bool sent = object_output_send_event(tracker->object_resources,
		&wlr_output->resources, tracker->impl->send_output_enter);

	if (sent && tracker->impl->send_done) {
		tracker->impl->send_done(tracker->object, /*client*/ NULL);
	}
}

void
output_tracker_destroy(struct output_tracker *tracker)
{
	struct output_tracker_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &tracker->entered_outputs, link) {
		_object_output_destroy(output);
	}
	free(tracker);
}

void
output_tracker_leave(struct output_tracker *tracker,
		struct wlr_output *wlr_output)
{
	struct output_tracker_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &tracker->entered_outputs, link) {
		if (output->wlr_output == wlr_output) {
			_object_output_destroy(output);
			return;
		}
	}
}
