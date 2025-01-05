// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-scene-buffer.h"
#include "common/scaled-corner-buffer.h"

/* TODO: duplicated */
static const double deg = 0.017453292519943295;

static struct lab_data_buffer *
_create_buffer(struct scaled_scene_buffer *scaled_buffer, double scale)
{
	struct scaled_corner_buffer *self = scaled_buffer->data;
	struct lab_data_buffer *buffer = buffer_create_cairo(
		self->width, self->height, scale);

	/*
	 * We need to precise buffer sizes to make sure the buffer is filled
	 * with the rounded rectangle when the scale is not an integer.
	 */
	double w = (double)buffer->base.width / scale;
	double h = (double)buffer->base.height / scale;
	double r = self->corner_radius;

	cairo_surface_t *surf = buffer->surface;
	cairo_t *cairo = cairo_create(surf);

	/* set transparent background */
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);

	/*
	 * Create outline path and fill. Illustration of top-left corner buffer:
	 *
	 *          _,,ooO"""""""""+
	 *        ,oO"'   ^        |
	 *      ,o"       |        |
	 *     o"         |r       |
	 *    o'          |        |
	 *    O     r     v        |
	 *    O<--------->+        |
	 *    O                    |
	 *    O                    |
	 *    O                    |
	 *    +--------------------+
	 */
	cairo_set_line_width(cairo, 0.0);
	cairo_new_sub_path(cairo);
	switch (self->corner) {
	case LAB_CORNER_TOP_LEFT:
		cairo_arc(cairo, r, r, r, 180 * deg, 270 * deg);
		cairo_line_to(cairo, w, 0);
		cairo_line_to(cairo, w, h);
		cairo_line_to(cairo, 0, h);
		break;
	case LAB_CORNER_TOP_RIGHT:
		cairo_arc(cairo, w - r, r, r, -90 * deg, 0 * deg);
		cairo_line_to(cairo, w, h);
		cairo_line_to(cairo, 0, h);
		cairo_line_to(cairo, 0, 0);
		break;
	default:
		wlr_log(WLR_ERROR, "unknown corner type");
	}
	cairo_close_path(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	set_cairo_color(cairo, self->fill_color);
	cairo_fill_preserve(cairo);
	cairo_stroke(cairo);

	/*
	 * Stroke horizontal and vertical borders, shown by Xs and Ys
	 * respectively in the figure below:
	 *
	 *          _,,ooO"XXXXXXXXX
	 *        ,oO"'            |
	 *      ,o"                |
	 *     o"                  |
	 *    o'                   |
	 *    O                    |
	 *    Y                    |
	 *    Y                    |
	 *    Y                    |
	 *    Y                    |
	 *    Y--------------------+
	 */
	cairo_set_line_cap(cairo, CAIRO_LINE_CAP_BUTT);
	set_cairo_color(cairo, self->border_color);
	cairo_set_line_width(cairo, self->border_width);
	double half_border_width = self->border_width / 2.0;
	switch (self->corner) {
	case LAB_CORNER_TOP_LEFT:
		cairo_move_to(cairo, half_border_width, h);
		cairo_line_to(cairo, half_border_width, r);
		cairo_move_to(cairo, r, half_border_width);
		cairo_line_to(cairo, w, half_border_width);
		break;
	case LAB_CORNER_TOP_RIGHT:
		cairo_move_to(cairo, 0, half_border_width);
		cairo_line_to(cairo, w - r, half_border_width);
		cairo_move_to(cairo, w - half_border_width, r);
		cairo_line_to(cairo, w - half_border_width, h);
		break;
	default:
		wlr_log(WLR_ERROR, "unknown corner type");
	}
	cairo_stroke(cairo);

	/*
	 * If radius==0 the borders stroked above go right up to (and including)
	 * the corners, so there is not need to do any more.
	 */
	if (!r) {
		goto out;
	}

	/*
	 * Stroke the arc section of the border of the corner piece.
	 *
	 * Note: This figure is drawn at a more zoomed in scale compared with
	 * those above.
	 *
	 *                 ,,ooooO""  ^
	 *            ,ooo""'      |  |
	 *         ,oOO"           |  | line-thickness
	 *       ,OO"              |  |
	 *     ,OO"         _,,ooO""  v
	 *    ,O"         ,oO"'
	 *   ,O'        ,o"
	 *  ,O'        o"
	 *  o'        o'
	 *  O         O
	 *  O---------O            +
	 *       <----------------->
	 *          radius
	 *
	 * We handle the edge-case where line-thickness > radius by merely
	 * setting line-thickness = radius and in effect drawing a quadrant of a
	 * circle. In this case the X and Y borders butt up against the arc and
	 * overlap each other (as their line-thicknesses are greater than the
	 * line-thickness of the arc). As a result, there is no inner rounded
	 * corners.
	 *
	 * So, in order to have inner rounded corners cornerRadius should be
	 * greater than border.width.
	 *
	 * Also, see diagrams in https://github.com/labwc/labwc/pull/990
	 */
	double line_width = MIN(self->border_width, r);
	cairo_set_line_width(cairo, line_width);
	half_border_width = line_width / 2.0;
	switch (self->corner) {
	case LAB_CORNER_TOP_LEFT:
		cairo_move_to(cairo, half_border_width, r);
		cairo_arc(cairo, r, r, r - half_border_width, 180 * deg, 270 * deg);
		break;
	case LAB_CORNER_TOP_RIGHT:
		cairo_move_to(cairo, w - r, half_border_width);
		cairo_arc(cairo, w - r, r, r - half_border_width, -90 * deg, 0 * deg);
		break;
	default:
		break;
	}
	cairo_stroke(cairo);

out:
	cairo_surface_flush(surf);
	cairo_destroy(cairo);

	return buffer;
}

static void
_destroy(struct scaled_scene_buffer *scaled_buffer)
{
	struct scaled_corner_buffer *self = scaled_buffer->data;
	scaled_buffer->data = NULL;
	free(self);
}

static bool
_equal(struct scaled_scene_buffer *scaled_buffer_a, struct scaled_scene_buffer *scaled_buffer_b)
{
	struct scaled_corner_buffer *a = scaled_buffer_a->data;
	struct scaled_corner_buffer *b = scaled_buffer_b->data;

	return a->width == b->width
		&& a->height == b->height
		&& a->border_width == b->border_width
		&& a->corner_radius == b->corner_radius
		&& a->corner == b->corner
		&& !memcmp(a->fill_color, b->fill_color, sizeof(a->fill_color))
		&& !memcmp(a->border_color, b->border_color, sizeof(a->border_color));
}

static const struct scaled_scene_buffer_impl impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
	.equal = _equal,
};

struct scaled_corner_buffer *scaled_corner_buffer_create(
	struct wlr_scene_tree *parent, int width, int height, int border_width,
	int corner_radius, enum lab_corner corner,
	float fill_color[4], float border_color[4])
{
	/* TODO: support rounded corners for menus and OSDs */

	assert(parent);
	struct scaled_corner_buffer *self = znew(*self);
	struct scaled_scene_buffer *scaled_buffer = scaled_scene_buffer_create(
		parent, &impl, /* drop_buffer */ true);
	scaled_buffer->data = self;
	self->scaled_buffer = scaled_buffer;
	self->scene_buffer = scaled_buffer->scene_buffer;
	self->width = MAX(width, 1);
	self->height = MAX(height, 1);
	self->border_width = border_width;
	self->corner_radius = corner_radius;
	self->corner = corner;
	memcpy(self->fill_color, fill_color, sizeof(self->fill_color));
	memcpy(self->border_color, border_color, sizeof(self->border_color));

	scaled_scene_buffer_request_update(scaled_buffer,
		self->width, self->height);

	return self;
}
