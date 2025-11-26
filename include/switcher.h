/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SWITCHER_H
#define LABWC_SWITCHER_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct output;

enum lab_cycle_dir {
	LAB_CYCLE_DIR_NONE,
	LAB_CYCLE_DIR_FORWARD,
	LAB_CYCLE_DIR_BACKWARD,
};

/* TODO: add field with keyboard layout? */
enum switcher_osd_field_content {
	LAB_FIELD_NONE = 0,
	LAB_FIELD_TYPE,
	LAB_FIELD_TYPE_SHORT,
	LAB_FIELD_IDENTIFIER,
	LAB_FIELD_TRIMMED_IDENTIFIER,
	LAB_FIELD_ICON,
	LAB_FIELD_DESKTOP_ENTRY_NAME,
	LAB_FIELD_TITLE,
	LAB_FIELD_TITLE_SHORT,
	LAB_FIELD_WORKSPACE,
	LAB_FIELD_WORKSPACE_SHORT,
	LAB_FIELD_WIN_STATE,
	LAB_FIELD_WIN_STATE_ALL,
	LAB_FIELD_OUTPUT,
	LAB_FIELD_OUTPUT_SHORT,
	LAB_FIELD_CUSTOM,

	LAB_FIELD_COUNT
};

struct switcher_osd_field {
	enum switcher_osd_field_content content;
	int width;
	char *format;
	struct wl_list link; /* struct rcxml.window_switcher.fields */
};

struct buf;
struct view;
struct server;
struct wlr_scene_node;

/* Begin window switcher */
void switcher_begin(struct server *server, enum lab_cycle_dir direction);

/* Cycle the selected view in the window switcher */
void switcher_cycle(struct server *server, enum lab_cycle_dir direction);

/* Closes the window switcher */
void switcher_finish(struct server *server, bool switch_focus);

/* Notify window switcher about a destroying view */
void switcher_on_view_destroy(struct view *view);

/* Focus the clicked window and close window switcher */
void switcher_on_cursor_release(struct server *server, struct wlr_scene_node *node);

/* Used by switcher OSD modules internally to render window switcher fields */
void switcher_osd_field_get_content(struct switcher_osd_field *field,
	struct buf *buf, struct view *view);
/* Sets view info to buf according to format */
void switcher_osd_field_set_custom(struct buf *buf, struct view *view,
	const char *format);

/* Used by rcxml.c when parsing the config */
void switcher_osd_field_arg_from_xml_node(struct switcher_osd_field *field,
	const char *nodename, const char *content);
bool switcher_osd_field_is_valid(struct switcher_osd_field *field);
void switcher_osd_field_free(struct switcher_osd_field *field);

/* Internal API */
struct switcher_osd_item {
	struct view *view;
	struct wlr_scene_tree *tree;
	struct wl_list link;
};

struct switcher_osd_impl {
	/*
	 * Create a scene-tree of switcher OSD for an output.
	 * This sets output->switcher_osd.{items,tree}.
	 */
	void (*create)(struct output *output, struct wl_array *views);
	/*
	 * Update output->switcher_osd.tree to highlight
	 * server->switcher.cycle_view.
	 */
	void (*update)(struct output *output);
};

extern struct switcher_osd_impl switcher_osd_classic_impl;
extern struct switcher_osd_impl switcher_osd_thumbnail_impl;

#endif // LABWC_SWITCHER_H
