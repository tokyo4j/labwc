/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RESIZE_FRAME_H
#define LABWC_RESIZE_FRAME_H

#include "view.h"
#include <wlr/util/box.h>

void resize_overlay_update(struct view *view, struct wlr_box new_geo);
void resize_overlay_finish(struct view *view);

#endif /* LABWC_RESIZE_FRAME_H */
