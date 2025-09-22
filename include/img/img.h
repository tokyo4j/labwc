/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_IMG_H
#define LABWC_IMG_H

#include <cairo.h>
#include <stdbool.h>
#include <wayland-util.h>
#include <librsvg/rsvg.h>
#include "config.h"

enum lab_img_type {
	LAB_IMG_PNG,
	LAB_IMG_SVG,
	LAB_IMG_XBM,
	LAB_IMG_XPM,
};

struct lab_img {
	enum lab_img_type type;
	/* lab_img_data is refcounted to be shared by multiple lab_imgs */
	int refcount;

	/* Handler for the loaded image file */
	struct lab_data_buffer *buffer; /* for PNG/XBM/XPM image */
#if HAVE_RSVG
	RsvgHandle *svg; /* for SVG image */
#endif
};

struct lab_img *lab_img_load_from_path(enum lab_img_type type, const char *path,
	float *xbm_color);

/**
 * lab_img_load_from_bitmap() - create button from monochrome bitmap
 * @bitmap: bitmap data array in hexadecimal xbm format
 * @rgba: color
 *
 * Example bitmap: char button[6] = { 0x3f, 0x3f, 0x21, 0x21, 0x21, 0x3f };
 */
struct lab_img *lab_img_load_from_bitmap(const char *bitmap, float *rgba);

/**
 * lab_img_render() - Render lab_img to a buffer
 * @img: source image
 * @width: width of the created buffer
 * @height: height of the created buffer
 * @scale: scale of the created buffer
 */
struct lab_data_buffer *lab_img_render(struct lab_img *img,
	int width, int height, double scale);

/**
 * lab_img_destroy() - destroy lab_img
 * @img: lab_img to destroy
 */
void lab_img_destroy(struct lab_img *img);

#endif /* LABWC_IMG_H */
