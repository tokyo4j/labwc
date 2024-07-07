/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RESIZE_OUTLINES_H
#define LABWC_RESIZE_OUTLINES_H

#include <wlr/util/box.h>

struct view;

void resize_preview_update(struct view *view, struct wlr_box new_geo);
void resize_preview_finish(struct view *view);
bool resize_preview_enabled(struct view *view);

#endif /* LABWC_RESIZE_OUTLINES_H */
