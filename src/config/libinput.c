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

/* After rcxml_read(), a default category always exists. */
struct libinput_category *
libinput_category_get_default(void)
{
	struct libinput_category *l;
	/*
	 * Iterate in reverse to get the last one added in case multiple
	 * 'default' profiles were created.
	 */
	wl_list_for_each_reverse(l, &rc.libinput_categories, link) {
		if (l->type == LAB_LIBINPUT_DEVICE_DEFAULT) {
			return l;
		}
	}
	return NULL;
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

/*
 * Get applicable profile (category) by matching first by name and secondly be
 * type (e.g. 'touch' and 'non-touch'). If not suitable match is found based on
 * those two criteria we fallback on 'default'.
 */
static struct libinput_category *
get_category(struct wlr_input_device *device)
{
	/* By name */
	struct libinput_category *category;
	wl_list_for_each_reverse(category, &rc.libinput_categories, link) {
		if (category->name) {
			if (!strcasecmp(device->name, category->name)) {
				return category;
			}
		}
	}

	/* By type */
	enum lab_libinput_device_type type = device_type_from_wlr_device(device);
	wl_list_for_each_reverse(category, &rc.libinput_categories, link) {
		if (category->type == type) {
			return category;
		}
	}

	/* Use default profile as a fallback */
	return libinput_category_get_default();
}

void
libinput_configure_device(struct wlr_input_device *wlr_input_device)
{
	/*
	 * TODO: We do not check any return values for the various
	 *       libinput_device_config_*_set_*() calls. It would
	 *       be nice if we could inform the users via log file
	 *       that some libinput setting could not be applied.
	 */

	if (!wlr_input_device) {
		wlr_log(WLR_ERROR, "no wlr_input_device");
		return;
	}
	if (!wlr_input_device_is_libinput(wlr_input_device)) {
		return;
	}

	struct libinput_device *libinput_dev =
		wlr_libinput_get_device_handle(wlr_input_device);
	if (!libinput_dev) {
		wlr_log(WLR_ERROR, "no libinput_dev");
		return;
	}

	struct libinput_category *dc = get_category(wlr_input_device);

	/*
	 * The above logic should have always matched SOME category
	 * (the default category if none other took precedence)
	 */
	assert(dc);

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0) {
		wlr_log(WLR_INFO, "tap unavailable");
	} else {
		wlr_log(WLR_INFO, "tap configured");
		libinput_device_config_tap_set_enabled(libinput_dev, dc->tap);
		libinput_device_config_tap_set_button_map(libinput_dev,
			dc->tap_button_map);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| dc->tap_and_drag == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "tap-and-drag not configured");
	} else {
		wlr_log(WLR_INFO, "tap-and-drag configured");
		libinput_device_config_tap_set_drag_enabled(
			libinput_dev, dc->tap_and_drag);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| dc->drag_lock == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "drag lock not configured");
	} else {
		wlr_log(WLR_INFO, "drag lock configured");
		libinput_device_config_tap_set_drag_lock_enabled(
			libinput_dev, dc->drag_lock);
	}

	if (libinput_device_config_scroll_has_natural_scroll(libinput_dev) <= 0
			|| dc->natural_scroll == LAB_LIBINPUT_INVALID_INT) {
		wlr_log(WLR_INFO, "natural scroll not configured");
	} else {
		wlr_log(WLR_INFO, "natural scroll configured");
		libinput_device_config_scroll_set_natural_scroll_enabled(
			libinput_dev, dc->natural_scroll);
	}

	if (libinput_device_config_left_handed_is_available(libinput_dev) <= 0
			|| dc->left_handed == LAB_LIBINPUT_INVALID_INT) {
		wlr_log(WLR_INFO, "left-handed mode not configured");
	} else {
		wlr_log(WLR_INFO, "left-handed mode configured");
		libinput_device_config_left_handed_set(libinput_dev,
			dc->left_handed);
	}

	if (libinput_device_config_accel_is_available(libinput_dev) == 0) {
		wlr_log(WLR_INFO, "pointer acceleration unavailable");
	} else {
		wlr_log(WLR_INFO, "pointer acceleration configured");
		if (dc->pointer_speed != LAB_LIBINPUT_INVALID_FLOAT) {
			libinput_device_config_accel_set_speed(libinput_dev,
				dc->pointer_speed);
		}
		if (dc->accel_profile != LAB_LIBINPUT_INVALID_ENUM) {
			libinput_device_config_accel_set_profile(libinput_dev,
				dc->accel_profile);
		}
	}

	if (libinput_device_config_middle_emulation_is_available(libinput_dev) == 0
			|| dc->middle_emu == LAB_LIBINPUT_INVALID_ENUM)  {
		wlr_log(WLR_INFO, "middle emulation not configured");
	} else {
		wlr_log(WLR_INFO, "middle emulation configured");
		libinput_device_config_middle_emulation_set_enabled(
			libinput_dev, dc->middle_emu);
	}

	if (libinput_device_config_dwt_is_available(libinput_dev) == 0
			|| dc->dwt == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "dwt not configured");
	} else {
		wlr_log(WLR_INFO, "dwt configured");
		libinput_device_config_dwt_set_enabled(libinput_dev, dc->dwt);
	}

	if ((dc->click_method != LIBINPUT_CONFIG_CLICK_METHOD_NONE
			&& (libinput_device_config_click_get_methods(libinput_dev)
				& dc->click_method) == 0)
			|| dc->click_method == LAB_LIBINPUT_INVALID_ENUM) {
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

		libinput_device_config_click_set_method(libinput_dev, dc->click_method);
	}

	if ((dc->send_events_mode != LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
			&& (libinput_device_config_send_events_get_modes(libinput_dev)
				& dc->send_events_mode) == 0)
			|| dc->send_events_mode == LAB_LIBINPUT_INVALID_ENUM) {
		wlr_log(WLR_INFO, "send events mode not configured");
	} else {
		wlr_log(WLR_INFO, "send events mode configured");
		libinput_device_config_send_events_set_mode(
			libinput_dev, dc->send_events_mode);
	}

	/* Non-zero if the device can be calibrated, zero otherwise. */
	if (libinput_device_config_calibration_has_matrix(libinput_dev) == 0
			|| dc->calibration_matrix[0] == LAB_LIBINPUT_INVALID_FLOAT) {
		wlr_log(WLR_INFO, "calibration matrix not configured");
	} else {
		wlr_log(WLR_INFO, "calibration matrix configured");
		libinput_device_config_calibration_set_matrix(
			libinput_dev, dc->calibration_matrix);
	}
}
