// SPDX-License-Identifier: GPL-2.0-only
#include "common/list.h"

/* Bubble sort */
void wl_list_sort(struct wl_list *list,
                  wl_list_sort_compare compare) {
    int len = wl_list_length(list);
    for (int i = 0; i < len; i++) {
        struct wl_list *elm = list->next;
        for (int j = 0; j < len - i - 1; j++) {
            if (compare(elm, elm->next) > 0) {
                struct wl_list *tmp = elm->next;
                wl_list_remove(elm);
                wl_list_insert(tmp, elm);
                elm = tmp;
            }
            elm = elm->next;
        }
    }
}
