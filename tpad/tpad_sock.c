/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "sock.h"
#include "log.h"
#include "mem_file.h"
#include "tcp_queue.h"
#include "worker.h"
#include "tsock_trace.h"
#include "tpad.h"
#include "archive.h"

static void tpad_symlink(const char *target, const char *linkpath) {
    if (symlink(target, linkpath) < 0) {
        LOG_WARN("failed to symlink %s -> %s: %s", target, linkpath, strerror(errno));
    }
}

void sock_termination(void) {
    struct archive_ctx ctx;
    struct mem_file *mem_file;
    uint64_t id;

    mem_file = mem_file_map(tpad.sock_file, NULL, MEM_FILE_READ);
    if (!mem_file) return;

    archive_ctx_init(&ctx, tpad.archive_dir, "socks", 16);
    id = archive_raw(&ctx, mem_file->hdr, mem_file->hdr->size);
    unlink(tpad.sock_file);

    if (id != UINT64_MAX) tpad_symlink(archive_path(&ctx, id), tpad.sock_file);

    LOG("skip active sock termination in tpad FreeBSD port");
}

/* XXX: de-duplicate */
static struct mem_file *map_tsock_trace_file(const char *path) {
    struct mem_file *mem_file;

    mem_file = mem_file_map(path, NULL, MEM_FILE_READ);
    if (!mem_file) return NULL;

    memset(&tsock_trace_ctrl, 0, sizeof(tsock_trace_ctrl));
    tsock_trace_ctrl.file = mem_file_data(mem_file);
    tsock_trace_ctrl.size = mem_file_data_size(mem_file);

    tsock_trace_ctrl.parser = mem_file_parser(mem_file);
    tsock_trace_ctrl.parser_size = mem_file_parser_size(mem_file);

    return mem_file;
}

void sock_archive(void) {
    struct archive_ctx ctx;
    struct mem_file *mem_file;
    struct tsock_trace *trace;
    char link_path[PATH_MAX];
    char name[256];
    uint64_t id;

    mem_file = map_tsock_trace_file(tpad.sock_trace_file);
    if (!mem_file) return;

    archive_ctx_init(&ctx, tpad.archive_dir, "trace", 16);
    id = archive_raw(&ctx, mem_file->hdr, mem_file->hdr->size);
    unlink(tpad.sock_trace_file);

    if (id == UINT64_MAX) return;

    tpa_snprintf(link_path, sizeof(link_path), "%s/socktrace", dirname(strdup(tpad.sock_trace_file)));
    tpad_symlink(archive_path(&ctx, id), link_path);

    TSOCK_TRACE_FOREACH(trace) {
        if (trace->sid < 0) continue;

        tsock_trace_name(trace, "tpad", name, sizeof(name));
        archive_map_add(ctx.map, off, trace->init_time, trace->size, trace->sid, name, archive_path(&ctx, id));
    }
}
