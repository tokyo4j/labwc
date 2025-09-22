// SPDX-License-Identifier: GPL-2.0-only

#include "img/img.h"
#include <assert.h>
#include "buffer.h"
#include "config.h"
#include "common/box.h"
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "img/img-png.h"
#if HAVE_RSVG
#include "img/img-svg.h"
#endif
#include "img/img-xbm.h"
#include "img/img-xpm.h"
#include "labwc.h"
#include "theme.h"

struct lab_img *
lab_img_load_from_path(enum lab_img_type type, const char *path, float *xbm_color)
{
	if (string_null_or_empty(path)) {
		return NULL;
	}

	struct lab_img *img = znew(*img);
	img->type = type;

	switch (type) {
	case LAB_IMG_PNG:
		img->buffer = img_png_load(path);
		break;
	case LAB_IMG_XBM:
		assert(xbm_color);
		img->buffer = img_xbm_load(path, xbm_color);
		break;
	case LAB_IMG_XPM:
		img->buffer = img_xpm_load(path);
		break;
	case LAB_IMG_SVG:
#if HAVE_RSVG
		img->svg = img_svg_load(path);
#endif
		break;
	}

	bool img_is_loaded = (bool)img->buffer;
#if HAVE_RSVG
	img_is_loaded |= (bool)img->svg;
#endif

	if (!img_is_loaded) {
		free(img);
		return NULL;
	}

	return img;
}

struct lab_img *
lab_img_load_from_bitmap(const char *bitmap, float *rgba)
{
	struct lab_data_buffer *buffer = img_xbm_load_from_bitmap(bitmap, rgba);
	if (!buffer) {
		return NULL;
	}

	struct lab_img *img = znew(*img);
	img->type = LAB_IMG_XBM;
	img->buffer = buffer;

	return img;
}

struct lab_data_buffer *
lab_img_render(struct lab_img *img, int width, int height, double scale)
{
	struct lab_data_buffer *buffer = NULL;

	/* Render the image into the buffer for the given size */
	switch (img->type) {
	case LAB_IMG_PNG:
	case LAB_IMG_XBM:
	case LAB_IMG_XPM:
		buffer = buffer_resize(img->buffer, width, height, scale);
		break;
#if HAVE_RSVG
	case LAB_IMG_SVG:
		buffer = img_svg_render(img->svg, width, height, scale);
		break;
#endif
	default:
		break;
	}

	if (!buffer) {
		return NULL;
	}

	cairo_surface_flush(buffer->surface);

	return buffer;
}

void
lab_img_destroy(struct lab_img *img)
{
	if (!img) {
		return;
	}

	img->refcount--;
	if (img->refcount == 0) {
		if (img->buffer) {
			wlr_buffer_drop(&img->buffer->base);
		}
#if HAVE_RSVG
		if (img->svg) {
			g_object_unref(img->svg);
		}
#endif
		free(img);
	}
}
