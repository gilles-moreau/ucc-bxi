/**
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See file LICENSE for terms.
 */

#ifndef UCC_EC_UCP_H_
#define UCC_EC_UCP_H_

#include "components/ec/base/ucc_ec_base.h"
#include "components/ec/ucc_ec_log.h"

#include "components/tl/ucp/tl_ucp.h"

enum {
    UCC_EC_UCP_EXECUTOR_SCHEDULE = UCC_BIT(0)
};

typedef enum {
    UCC_EC_UCP_REDUCE_STAGE_ACC,
    UCC_EC_UCP_REDUCE_STAGE_WRITE_DST,
    UCC_EC_UCP_REDUCE_STAGE_WAIT,
    UCC_EC_UCP_REDUCE_STAGE_COMPLETED
} ucc_ec_ucp_reduce_stage_t;

typedef struct ucc_ec_ucp_executor_task {
    ucc_ee_executor_task_t super;
    size_t                 n_reqs;
    ucp_ep_h               self;
    unsigned               failure;
    ucc_memory_type_t      mem_type;
    size_t                 size;
    union {
        struct {
            size_t                    count;
            ucc_datatype_t            dt;
            ucp_rkey_h                dst_rkey;
            ucp_mem_h                 dst_memh;
            void                     *dst;
            ucp_rkey_h                acc_rkey;
            ucp_mem_h                 acc_memh;
            void                     *acc_buf;
            ucc_ec_ucp_reduce_stage_t stage;
            ucs_status_ptr_t          flush_req;
        } reduce;
    };
} ucc_ec_ucp_executor_task_t;

typedef struct ucc_ec_ucp_executor {
    ucc_ee_executor_t     super;
    unsigned              flags;
    ucc_tl_ucp_context_t *ucp_ctx;
    ucp_ep_h              self;
    ucc_memory_type_t     mem_type;
} ucc_ec_ucp_executor_t;

typedef struct ucc_ec_ucp_config {
    ucc_ec_config_t super;
} ucc_ec_ucp_config_t;

typedef struct ucc_ec_ucp {
    ucc_ec_base_t     super;
    ucc_thread_mode_t thread_mode;
    ucc_mpool_t       executors;
    ucc_mpool_t       executor_tasks;
    ucc_spinlock_t    init_spinlock;
} ucc_ec_ucp_t;

extern ucc_ec_ucp_t ucc_ec_ucp;

#endif
