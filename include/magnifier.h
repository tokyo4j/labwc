/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_MAGNIFIER_H
#define LABWC_MAGNIFIER_H

#include <stdbool.h>

struct server;
struct output;
struct wlr_buffer;
struct wlr_box;
struct wlr_output_state;

enum magnify_dir {
	MAGNIFY_INCREASE,
	MAGNIFY_DECREASE
};

void magnify_toggle(struct server *server);
void magnify_set_scale(struct server *server, enum magnify_dir dir);
bool magnifier_needs_redraw(struct output *output);
void magnify(struct output *output, struct wlr_output_state *state);
bool is_magnify_on(void);
void magnify_reset(void);

#endif /* LABWC_MAGNIFIER_H */
