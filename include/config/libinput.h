/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_LIBINPUT_H
#define LABWC_LIBINPUT_H

#include <float.h>
#include <libinput.h>
#include <limits.h>
#include <string.h>
#include <wayland-server-core.h>

#define LAB_LIBINPUT_INVALID_INT INT_MAX
#define LAB_LIBINPUT_INVALID_FLOAT FLT_MAX
#define LAB_LIBINPUT_INVALID_ENUM UINT32_MAX

struct wlr_input_device;

enum lab_libinput_device_type {
	LAB_LIBINPUT_DEVICE_NONE = 0,
	LAB_LIBINPUT_DEVICE_DEFAULT,
	LAB_LIBINPUT_DEVICE_TOUCH,
	LAB_LIBINPUT_DEVICE_TOUCHPAD,
	LAB_LIBINPUT_DEVICE_NON_TOUCH,
};

struct libinput_category {
	enum lab_libinput_device_type type;
	char *name;
	struct wl_list link;

	float pointer_speed;
	int natural_scroll;
	int left_handed;
	uint32_t tap;                /* enum libinput_config_tap_state tap */
	uint32_t tap_button_map;     /* enum libinput_config_tap_button_map */
	uint32_t tap_and_drag;       /* enum libinput_config_drag_state */
	uint32_t drag_lock;          /* enum libinput_config_drag_lock_state */
	uint32_t accel_profile;      /* enum libinput_config_accel_profile */
	uint32_t middle_emu;         /* enum libinput_config_middle_emulation_state */
	uint32_t dwt;                /* enum libinput_config_dwt_state */
	uint32_t click_method;       /* enum libinput_config_click_method */
	uint32_t send_events_mode;   /* enum libinput_config_send_events_mode */
	float calibration_matrix[6]; /* first element can be LAB_LIBINPUT_INVALID_FLOAT */
};

enum lab_libinput_device_type get_device_type(const char *s);
struct libinput_category *libinput_category_create(void);
void libinput_post_processing(void);
void libinput_configure_device(struct wlr_input_device *device);

#endif /* LABWC_LIBINPUT_H */
