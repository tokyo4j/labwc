// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "common/list.h"
#include "common/string-helpers.h"
#include "config/libinput.h"
#include "config/rcxml.h"

static void
libinput_category_init(struct libinput_category *l)
{
	l->type = LAB_LIBINPUT_DEVICE_DEFAULT;
	l->name = NULL;

	l->pointer_speed         = LAB_LIBINPUT_INVALID_FLOAT;
	l->natural_scroll        = LAB_LIBINPUT_INVALID_INT;
	l->left_handed           = LAB_LIBINPUT_INVALID_INT;
	l->tap                   = LAB_LIBINPUT_INVALID_ENUM;
	l->tap_button_map        = LAB_LIBINPUT_INVALID_ENUM;
	l->tap_and_drag          = LAB_LIBINPUT_INVALID_ENUM;
	l->drag_lock             = LAB_LIBINPUT_INVALID_ENUM;
	l->accel_profile         = LAB_LIBINPUT_INVALID_ENUM;
	l->middle_emu            = LAB_LIBINPUT_INVALID_ENUM;
	l->dwt                   = LAB_LIBINPUT_INVALID_ENUM;
	l->click_method          = LAB_LIBINPUT_INVALID_ENUM;
	l->send_events_mode      = LAB_LIBINPUT_INVALID_ENUM;
	l->calibration_matrix[0] = LAB_LIBINPUT_INVALID_FLOAT;
}

enum lab_libinput_device_type
get_device_type(const char *s)
{
	if (string_null_or_empty(s)) {
		return LAB_LIBINPUT_DEVICE_NONE;
	}
	if (!strcasecmp(s, "default")) {
		return LAB_LIBINPUT_DEVICE_DEFAULT;
	}
	if (!strcasecmp(s, "touch")) {
		return LAB_LIBINPUT_DEVICE_TOUCH;
	}
	if (!strcasecmp(s, "touchpad")) {
		return LAB_LIBINPUT_DEVICE_TOUCHPAD;
	}
	if (!strcasecmp(s, "non-touch")) {
		return LAB_LIBINPUT_DEVICE_NON_TOUCH;
	}
	return LAB_LIBINPUT_DEVICE_NONE;
}

struct libinput_category *
libinput_category_create(void)
{
	struct libinput_category *l = znew(*l);
	libinput_category_init(l);
	wl_list_append(&rc.libinput_categories, &l->link);
	return l;
}

static int get_libinput_category_priority(struct libinput_category *category)
{
	if (category->type == LAB_LIBINPUT_DEVICE_DEFAULT) {
		return 0;
	}
	if (category->type == LAB_LIBINPUT_DEVICE_NONE) {
		assert(category->name);
		return 2;
	}
	return 1;
}

static int libinput_category_compare(struct wl_list *link_a, struct wl_list *link_b)
{
	struct libinput_category *cat_a = wl_container_of(link_a, cat_a, link);
	struct libinput_category *cat_b = wl_container_of(link_b, cat_b, link);
	return get_libinput_category_priority(cat_a)
		- get_libinput_category_priority(cat_b);
}

/*
 * Sort categories by priority so we can merge categories that matches certain
 * devices by iterating from the first element.
 *
 * Category priority is: builtin -> "default" -> "touchpad" -> "SYNA32A0:00 06CB:CE14 Touchpad"
 */
void
libinput_post_processing(void)
{
	wl_list_sort(&rc.libinput_categories, libinput_category_compare);

	struct libinput_category *builtin_category = znew(*builtin_category);
	libinput_category_init(builtin_category);
	/* <tap> and <tapButtonMap> have default values */
	builtin_category->tap = LIBINPUT_CONFIG_TAP_ENABLED;
	builtin_category->tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
	wl_list_insert(&rc.libinput_categories, &builtin_category->link);
}

static enum lab_libinput_device_type
device_type_from_wlr_device(struct wlr_input_device *wlr_input_device)
{
	switch (wlr_input_device->type) {
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		return LAB_LIBINPUT_DEVICE_TOUCH;
	default:
		break;
	}

	if (wlr_input_device->type == WLR_INPUT_DEVICE_POINTER &&
			wlr_input_device_is_libinput(wlr_input_device)) {
		struct libinput_device *libinput_device =
			wlr_libinput_get_device_handle(wlr_input_device);

		if (libinput_device_config_tap_get_finger_count(libinput_device) > 0) {
			return LAB_LIBINPUT_DEVICE_TOUCHPAD;
		}
	}

	return LAB_LIBINPUT_DEVICE_NON_TOUCH;
}

static bool
category_matches(struct libinput_category *category,
		struct wlr_input_device *device) {
	if (category->name && strcasecmp(device->name, category->name)) {
		return false;
	}
	if (category->type != LAB_LIBINPUT_DEVICE_NONE
			&& category->type != LAB_LIBINPUT_DEVICE_DEFAULT
			&& category->type != device_type_from_wlr_device(device)) {
		return false;
	}
	return true;
}

static void
merge_category(struct libinput_category *dest, struct libinput_category *src)
{
	if (src->pointer_speed != LAB_LIBINPUT_INVALID_FLOAT) {
		dest->pointer_speed = src->pointer_speed;
	}
	if (src->natural_scroll != LAB_LIBINPUT_INVALID_INT) {
		dest->natural_scroll = src->natural_scroll;
	}
	if (src->left_handed != LAB_LIBINPUT_INVALID_INT) {
		dest->left_handed = src->left_handed;
	}
	if (src->tap != LAB_LIBINPUT_INVALID_ENUM) {
		dest->tap = src->tap;
	}
	if (src->tap_button_map != LAB_LIBINPUT_INVALID_ENUM) {
		dest->tap_button_map = src->tap_button_map;
	}
	if (src->tap_and_drag != LAB_LIBINPUT_INVALID_ENUM) {
		dest->tap_and_drag = src->tap_and_drag;
	}
	if (src->drag_lock != LAB_LIBINPUT_INVALID_ENUM) {
		dest->drag_lock = src->drag_lock;
	}
	if (src->accel_profile != LAB_LIBINPUT_INVALID_ENUM) {
		dest->accel_profile = src->accel_profile;
	}
	if (src->middle_emu != LAB_LIBINPUT_INVALID_ENUM) {
		dest->middle_emu = src->middle_emu;
	}
	if (src->dwt != LAB_LIBINPUT_INVALID_ENUM) {
		dest->dwt = src->dwt;
	}
	if (src->click_method != LAB_LIBINPUT_INVALID_ENUM) {
		dest->click_method = src->click_method;
	}
	if (src->send_events_mode != LAB_LIBINPUT_INVALID_ENUM) {
		dest->send_events_mode = src->send_events_mode;
	}
	if (src->calibration_matrix[0] != LAB_LIBINPUT_INVALID_FLOAT) {
		memcpy(dest->calibration_matrix, src->calibration_matrix,
			sizeof(src->calibration_matrix));
	}
}

void
libinput_configure_device(struct wlr_input_device *device)
{
	/*
	 * TODO: We do not check any return values for the various
	 *       libinput_device_config_*_set_*() calls. It would
	 *       be nice if we could inform the users via log file
	 *       that some libinput setting could not be applied.
	 */

	if (!device) {
		wlr_log(WLR_ERROR, "no wlr_input_device");
		return;
	}
	if (!wlr_input_device_is_libinput(device)) {
		return;
	}

	wlr_log(WLR_DEBUG, "Configuring libinput device: %s", device->name);

	struct libinput_category category;
	libinput_category_init(&category);

	struct libinput_category *l;
	int i = 1;
	wl_list_for_each(l, &rc.libinput_categories, link) {
		if (category_matches(l, device)) {
			wlr_log(WLR_DEBUG, "%dth <libinput><device> is applied", i);
			merge_category(&category, l);
		}
		i++;
	}

	struct libinput_device *libinput_dev =
		wlr_libinput_get_device_handle(device);
	if (!libinput_dev) {
		wlr_log(WLR_ERROR, "no libinput_dev");
		return;
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0) {
		wlr_log(WLR_INFO, "tap unavailable");
	} else {
		wlr_log(WLR_INFO, "tap configured");
		libinput_device_config_tap_set_enabled(libinput_dev, category.tap);
		libinput_device_config_tap_set_button_map(libinput_dev,
			category.tap_button_map);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| category.tap_and_drag == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "tap-and-drag not configured");
	} else {
		wlr_log(WLR_INFO, "tap-and-drag configured");
		libinput_device_config_tap_set_drag_enabled(
			libinput_dev, category.tap_and_drag);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| category.drag_lock == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "drag lock not configured");
	} else {
		wlr_log(WLR_INFO, "drag lock configured");
		libinput_device_config_tap_set_drag_lock_enabled(
			libinput_dev, category.drag_lock);
	}

	if (libinput_device_config_scroll_has_natural_scroll(libinput_dev) <= 0
			|| category.natural_scroll == LAB_LIBINPUT_INVALID_INT) {
		wlr_log(WLR_INFO, "natural scroll not configured");
	} else {
		wlr_log(WLR_INFO, "natural scroll configured");
		libinput_device_config_scroll_set_natural_scroll_enabled(
			libinput_dev, category.natural_scroll);
	}

	if (libinput_device_config_left_handed_is_available(libinput_dev) <= 0
			|| category.left_handed == LAB_LIBINPUT_INVALID_INT) {
		wlr_log(WLR_INFO, "left-handed mode not configured");
	} else {
		wlr_log(WLR_INFO, "left-handed mode configured");
		libinput_device_config_left_handed_set(libinput_dev,
			category.left_handed);
	}

	if (libinput_device_config_accel_is_available(libinput_dev) == 0) {
		wlr_log(WLR_INFO, "pointer acceleration unavailable");
	} else {
		wlr_log(WLR_INFO, "pointer acceleration configured");
		if (category.pointer_speed != LAB_LIBINPUT_INVALID_FLOAT) {
			libinput_device_config_accel_set_speed(libinput_dev,
				category.pointer_speed);
		}
		if (category.accel_profile != LAB_LIBINPUT_INVALID_ENUM) {
			libinput_device_config_accel_set_profile(libinput_dev,
				category.accel_profile);
		}
	}

	if (libinput_device_config_middle_emulation_is_available(libinput_dev) == 0
			|| category.middle_emu == LAB_LIBINPUT_INVALID_ENUM)  {
		wlr_log(WLR_INFO, "middle emulation not configured");
	} else {
		wlr_log(WLR_INFO, "middle emulation configured");
		libinput_device_config_middle_emulation_set_enabled(
			libinput_dev, category.middle_emu);
	}

	if (libinput_device_config_dwt_is_available(libinput_dev) == 0
			|| category.dwt == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "dwt not configured");
	} else {
		wlr_log(WLR_INFO, "dwt configured");
		libinput_device_config_dwt_set_enabled(libinput_dev, category.dwt);
	}

	if ((category.click_method != LIBINPUT_CONFIG_CLICK_METHOD_NONE
			&& (libinput_device_config_click_get_methods(libinput_dev)
				& category.click_method) == 0)
			|| category.click_method == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "click method not configured");
	} else {
		wlr_log(WLR_INFO, "click method configured");

		/*
		 * Note, the documentation claims that:
		 * > [...] The device may require changing to a neutral state
		 * > first before activating the new method.
		 *
		 * However, just setting the method seems to work without
		 * issues.
		 */

		libinput_device_config_click_set_method(libinput_dev, category.click_method);
	}

	if ((category.send_events_mode != LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
			&& (libinput_device_config_send_events_get_modes(libinput_dev)
				& category.send_events_mode) == 0)
			|| category.send_events_mode == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "send events mode not configured");
	} else {
		wlr_log(WLR_INFO, "send events mode configured");
		libinput_device_config_send_events_set_mode(
			libinput_dev, category.send_events_mode);
	}

	/* Non-zero if the device can be calibrated, zero otherwise. */
	if (libinput_device_config_calibration_has_matrix(libinput_dev) == 0
			|| category.calibration_matrix[0] == LAB_LIBINPUT_INVALID_FLOAT) {
		wlr_log(WLR_INFO, "calibration matrix not configured");
	} else {
		wlr_log(WLR_INFO, "calibration matrix configured");
		libinput_device_config_calibration_set_matrix(
			libinput_dev, category.calibration_matrix);
	}
}
