// SPDX-License-Identifier: GPL-2.0-only
#include "edges.h"
#include <assert.h>
#include <limits.h>
#include <pixman.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "common/border.h"
#include "common/box.h"
#include "common/macros.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "ssd.h"
#include "view.h"

struct lab_icon {

};