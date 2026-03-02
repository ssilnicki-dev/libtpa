/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
 */
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <rte_ether.h>

#include "cfg.h"
#include "lib/utils.h"
#include "log.h"

enum { CFG_EXTERN_OPT_MAX = CFG_SPECS_SIZE };

struct cfg_pending_opt {
    char name[NAME_SIZE];
    char val[VAL_SIZE];
};

struct tpa_cfg tpa_cfg;

static struct cfg_pending_opt pending_opts[CFG_EXTERN_OPT_MAX];
static size_t nr_pending_opt;

static struct cfg_spec *cfg_spec_find(const char *name) {
    size_t i;

    for (i = 0; i < tpa_cfg.nr_spec; i++) {
        if (strcmp(tpa_cfg.cfg_specs[i]->name, name) == 0) return tpa_cfg.cfg_specs[i];
    }

    return NULL;
}

static struct cfg_pending_opt *cfg_pending_find(const char *name) {
    size_t i;

    for (i = 0; i < nr_pending_opt; i++) {
        if (strcmp(pending_opts[i].name, name) == 0) return &pending_opts[i];
    }

    return NULL;
}

int cfg_spec_set_num(struct cfg_spec *spec, const char *val) {
    uint64_t num;

    if (spec->type == CFG_TYPE_SIZE)
        num = tpa_parse_num(val, NUM_TYPE_SIZE);
    else if (spec->type == CFG_TYPE_TIME)
        num = tpa_parse_num(val, NUM_TYPE_TIME_US);
    else
        num = tpa_parse_num(val, NUM_TYPE_NONE);

    if (num == UINT64_MAX) return -1;

    if ((spec->flags & CFG_FLAG_HAS_MIN) && num < (uint64_t)spec->min) return -1;

    if ((spec->flags & CFG_FLAG_HAS_MAX) && num > (uint64_t)spec->max) return -1;

    if ((spec->flags & CFG_FLAG_POWEROF2) && ((num & (num - 1)) != 0)) return -1;

    if (spec->data_len == 1)
        *(uint8_t *)spec->data = num;
    else if (spec->data_len == 2)
        *(uint16_t *)spec->data = num;
    else if (spec->data_len == 4)
        *(uint32_t *)spec->data = num;
    else if (spec->data_len == 8)
        *(uint64_t *)spec->data = num;
    else
        return -1;

    return 0;
}

static int cfg_spec_set_str(struct cfg_spec *spec, const char *val) {
    int len = strlen(val);

    if (len >= spec->data_len) return -1;

    strcpy(spec->data, val);

    return 0;
}

static int cfg_spec_set_ipv4(struct cfg_spec *spec, const char *val) {
    return inet_pton(AF_INET, val, spec->data) == 1 ? 0 : -1;
}

static int cfg_spec_set_ipv6(struct cfg_spec *spec, const char *val) {
    return inet_pton(AF_INET6, val, spec->data) == 1 ? 0 : -1;
}

static int cfg_spec_set_mac(struct cfg_spec *spec, const char *val) {
    return rte_ether_unformat_addr(val, spec->data);
}

static int cfg_spec_set_mask(struct cfg_spec *spec, const char *val) {
    char *end;
    uint64_t n;

    errno = 0;
    n = strtoull(val, &end, 0);
    if (errno || *end != '\0') return -1;

    if (spec->data_len == 4)
        *(uint32_t *)spec->data = n;
    else if (spec->data_len == 8)
        *(uint64_t *)spec->data = n;
    else
        return -1;

    return 0;
}

static int cfg_spec_set(struct cfg_spec *spec, const char *val, int init) {
    if (!init && (spec->flags & CFG_FLAG_RDONLY)) return -1;

    if (spec->set) return spec->set(spec, val);

    switch (spec->type) {
    case CFG_TYPE_SIZE:
    case CFG_TYPE_TIME:
    case CFG_TYPE_UINT:
        return cfg_spec_set_num(spec, val);
    case CFG_TYPE_STR:
        return cfg_spec_set_str(spec, val);
    case CFG_TYPE_IPV4:
        return cfg_spec_set_ipv4(spec, val);
    case CFG_TYPE_IPV6:
        return cfg_spec_set_ipv6(spec, val);
    case CFG_TYPE_MAC:
        return cfg_spec_set_mac(spec, val);
    case CFG_TYPE_MASK:
        return cfg_spec_set_mask(spec, val);
    default:
        return -1;
    }
}

static int cfg_apply_opt(const char *name, const char *val, int init) {
    struct cfg_spec *spec;

    spec = cfg_spec_find(name);
    if (!spec) return -1;

    return cfg_spec_set(spec, val, init);
}

int tpa_cfg_set(const char *name, const char *value) {
    struct cfg_pending_opt *opt;

    if (!name || !value || strlen(name) >= NAME_SIZE || strlen(value) >= VAL_SIZE) {
        errno = EINVAL;
        return -1;
    }

    opt = cfg_pending_find(name);
    if (!opt) {
        if (nr_pending_opt >= CFG_EXTERN_OPT_MAX) {
            errno = ENOSPC;
            return -1;
        }

        opt = &pending_opts[nr_pending_opt++];
        tpa_snprintf(opt->name, sizeof(opt->name), "%s", name);
    }

    tpa_snprintf(opt->val, sizeof(opt->val), "%s", value);

    if (cfg_apply_opt(name, value, 1) < 0 && cfg_spec_find(name) != NULL) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int cfg_init(void) {
    memset(&tpa_cfg, 0, sizeof(tpa_cfg));

    return 0;
}

int cfg_file_parse(FILE *file) {
    (void)file;
    errno = ENOTSUP;
    return -1;
}

const char *cfg_file_opt_get(const char *name) {
    struct cfg_pending_opt *opt = cfg_pending_find(name);
    if (!opt) return NULL;

    return opt->val;
}

void cfg_reset(void) {
    memset(&tpa_cfg, 0, sizeof(tpa_cfg));
    nr_pending_opt = 0;
}

int cfg_section_parse(const char *section) {
    (void)section;
    return 0;
}

int cfg_spec_register(struct cfg_spec *specs, int nr_spec) {
    int i;

    for (i = 0; i < nr_spec; i++) {
        struct cfg_pending_opt *opt;

        if (tpa_cfg.nr_spec >= CFG_SPECS_SIZE) return -1;

        if (cfg_spec_find(specs[i].name)) continue;

        tpa_cfg.cfg_specs[tpa_cfg.nr_spec++] = &specs[i];

        opt = cfg_pending_find(specs[i].name);
        if (opt && cfg_spec_set(&specs[i], opt->val, 1) < 0) {
            LOG_WARN("failed to set cfg %s=%s", specs[i].name, opt->val);
            return -1;
        }
    }

    return 0;
}

void cfg_dump_unknown_opts(void) {
}
