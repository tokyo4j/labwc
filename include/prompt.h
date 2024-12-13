/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PROMPT_H
#define LABWC_PROMPT_H

#include "view.h"

struct prompt_view *prompt_create(struct server *server, const char *text,
	const char **answers, int nr_answers);

#endif /* LABWC_PROMPT_H */
