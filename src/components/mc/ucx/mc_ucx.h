/**
 * Copyright (c) 2020-2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See file LICENSE for terms.
 */

#ifndef UCC_MC_UCX_H_
#define UCC_MC_UCX_H_

#include "components/mc/base/ucc_mc_base.h"
#include "components/mc/ucc_mc_log.h"


typedef struct ucc_mc_ucx_config {
    ucc_mc_config_t super;
    size_t          mpool_elem_size;
    int             mpool_max_elems;
} ucc_mc_ucx_config_t;

typedef struct ucc_mc_ucx {
    ucc_mc_base_t     super;
    ucc_mpool_t       mpool;
    int               mpool_init_flag;
    ucc_spinlock_t    mpool_init_spinlock;
    ucc_thread_mode_t thread_mode;
} ucc_mc_ucx_t;

extern ucc_mc_ucx_t ucc_mc_ucx;
#define MC_UCX_CONFIG                                                          \
    (ucc_derived_of(ucc_mc_ucx.super.config, ucc_mc_ucx_config_t))
#endif
