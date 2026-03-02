/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 * Author: Wenlong Luo <luowenlong.linl@bytedance.com>
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#include <sys/event.h>
#include <sys/time.h>
#include <pthread_np.h>

#include "ctrl.h"
#include "log.h"

static int ctrl_thread_create(void *(*func)(void *), void *arg, const char *name) {
    pthread_t tid;

    if (pthread_create(&tid, NULL, func, arg) < 0) {
        LOG_ERR("failed to create ctrl thread: %s", name);
        return -1;
    }

    pthread_set_name_np(tid, name);

    return 0;
}


static int kq = -1;

#define MAX_KEVENT 16

static void *poll_ctrl_event(void *ignored) {
    struct kevent events[MAX_KEVENT];
    struct ctrl_event *ctrl_event;
    int i;
    int ret;

    while (1) {
        ret = kevent(kq, NULL, 0, events, MAX_KEVENT, NULL);
        if (ret < 0) continue;

        for (i = 0; i < ret; i++) {
            ctrl_event = (struct ctrl_event *)events[i].udata;
            if (ctrl_event && ctrl_event->cb) ctrl_event->cb(ctrl_event);
        }
    }

    LOG_ERR("tpa ctrl thread quit");
    return NULL;
}

static int ctrl_event_register(struct ctrl_event *ctrl_event, const char *name) {
    struct kevent kev;
    int filter;
    uintptr_t ident;

    if (ctrl_event->timeout_event) {
        filter = EVFILT_TIMER;
        ident = (uintptr_t)ctrl_event;
        EV_SET(&kev, ident, filter, EV_ADD | EV_ENABLE, NOTE_SECONDS, ctrl_event->fd, ctrl_event);
    } else {
        filter = EVFILT_READ;
        ident = (uintptr_t)ctrl_event->fd;
        EV_SET(&kev, ident, filter, EV_ADD | EV_ENABLE, 0, 0, ctrl_event);
    }

    if (kevent(kq, &kev, 1, NULL, 0, NULL) != 0) {
        LOG_ERR("ctrl thread kevent add for %s error: %s", name, strerror(errno));
        return -1;
    }

    return 0;
}


static struct ctrl_event *do_ctrl_event_create(ctrl_event_cb_t cb, void *arg, int timeout_event, const char *name) {
    struct ctrl_event *ctrl_event;

    ctrl_event = malloc(sizeof(struct ctrl_event));
    if (!ctrl_event) {
        LOG_ERR("failed to malloc ctrl event for %s:%s", name, strerror(errno));
        return NULL;
    }

    ctrl_event->cb = cb;
    ctrl_event->arg = arg;
    ctrl_event->timeout_event = timeout_event;

    return ctrl_event;
}

struct ctrl_event *ctrl_timeout_event_create(long seconds, ctrl_event_cb_t cb, void *arg, const char *name) {
    struct ctrl_event *ctrl_event;

    ctrl_event = do_ctrl_event_create(cb, arg, 1, name);
    if (!ctrl_event) return NULL;

    ctrl_event->fd = seconds;
    if (ctrl_event->fd < 0 || ctrl_event_register(ctrl_event, name) == -1) {
        free(ctrl_event);
        return NULL;
    }

    return ctrl_event;
}

struct ctrl_event *ctrl_event_create(int fd, ctrl_event_cb_t cb, void *arg, const char *name) {
    struct ctrl_event *ctrl_event;

    ctrl_event = do_ctrl_event_create(cb, arg, 0, name);
    if (!ctrl_event) return NULL;

    ctrl_event->fd = fd;
    if (ctrl_event_register(ctrl_event, name) == -1) {
        free(ctrl_event);
        return NULL;
    }

    return ctrl_event;
}

void ctrl_event_destroy(struct ctrl_event *ctrl_event) {
    struct kevent kev;
    if (ctrl_event->timeout_event)
        EV_SET(&kev, (uintptr_t)ctrl_event, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    else
        EV_SET(&kev, (uintptr_t)ctrl_event->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(kq, &kev, 1, NULL, 0, NULL);

    free(ctrl_event);
}

int ctrl_init(void) {
    kq = kqueue();
    if (kq < 0) {
        LOG_ERR("failed to create kqueue fd: %s", strerror(errno));
        return -1;
    }

    if (ctrl_thread_create(poll_ctrl_event, NULL, "tpa-ctrl") != 0) return -1;

    return 0;
}
