/**
 * Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See file LICENSE for terms.
 */

#include "mc_ucx.h"
#include "ucc/api/ucc.h"
#include "ucc/api/ucc_status.h"
#include "utils/ucc_assert.h"
#include "utils/ucc_datastruct.h"
#include "utils/ucc_malloc.h"
#include "utils/ucc_math.h"
#include "utils/arch/cpu.h"
#include "components/tl/ucp/tl_ucp.h"
#include "components/tl/ucp/tl_ucp_sendrecv.h"
#include "components/tl/ucp/tl_ucp_coll.h"
#include <sys/types.h>

static ucc_config_field_t ucc_mc_ucx_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_mc_ucx_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_mc_config_table)},

    {"MPOOL_ELEM_SIZE", "0Mb", "The size of each element in mc ucx mpool",
     ucc_offsetof(ucc_mc_ucx_config_t, mpool_elem_size),
     UCC_CONFIG_TYPE_MEMUNITS},

    {"MPOOL_MAX_ELEMS", "0", "The max amount of elements in mc ucx mpool",
     ucc_offsetof(ucc_mc_ucx_config_t, mpool_max_elems), UCC_CONFIG_TYPE_UINT},

    {NULL}

};

static ucc_status_t ucc_mc_ucx_init(const ucc_mc_params_t *mc_params) 
{
    ucc_strncpy_safe(ucc_mc_ucx.super.config->log_component.name,
                     ucc_mc_ucx.super.super.name,
                     sizeof(ucc_mc_ucx.super.config->log_component.name));
    ucc_mc_ucx.thread_mode = mc_params->thread_mode;
    /* There is nothing to allocate nor mpool to manipulate since the real
     * memory is managed by the other memory type. So no spinlock is needed.
     */
    return UCC_OK;
}

static ucc_status_t ucc_mc_ucx_get_attr(ucc_mc_attr_t *mc_attr)
{
    if (mc_attr->field_mask & UCC_MC_ATTR_FIELD_THREAD_MODE) {
        mc_attr->thread_mode = ucc_mc_ucx.thread_mode;
    }
    
    return UCC_OK;
}

static ucc_status_t ucc_mc_ucx_mem_pool_free(ucc_mc_buffer_header_t *h_ptr) //NOLINT
{
    /* not supposed to be used */
    mc_error(&ucc_mc_ucx.super, "ucx memory component shouldn't be used for"
                                "mem pool free");
    return UCC_ERR_NOT_SUPPORTED;
}

static ucc_status_t
ucc_mc_ucx_mem_pool_alloc_with_init(ucc_mc_buffer_header_t **h_ptr, //NOLINT
                                    size_t                   size,  //NOLINT
                                    ucc_memory_type_t        mt)    //NOLINT
{
    /* not supposed to be used */
    mc_error(&ucc_mc_ucx.super, "ucx memory component shouldn't be used for"
                                "mem pool alloc");
    return UCC_ERR_NOT_SUPPORTED;
}

static ucc_status_t ucc_mc_ucx_memcpy(void *dst, const void *src, size_t len,
                                      ucc_memory_type_t dst_mem, //NOLINT
                                      ucc_memory_type_t src_mem) //NOLINT
{
    ucc_tl_ucp_task_t *task;
    ucc_tl_ucp_team_t *team;
    ucc_rank_t rank;
    ucc_status_t status;

    ucc_assert((dst_mem == UCC_MEMORY_TYPE_UCX));

    ucc_ucx_mem_t *dst_param = (ucc_ucx_mem_t *) dst;
    ucc_assert((dst_param->op_attr_mask & UCC_OP_ATTR_FIELD_TASK) &&
                (dst_param->op_attr_mask & UCC_OP_ATTR_FIELD_TEAM) &&
                (dst_param->op_attr_mask & UCC_OP_ATTR_FIELD_RANK));
    task = dst_param->task;
    team = dst_param->team;
    rank = dst_param->rank;

    status = ucc_tl_ucp_send_nb((void *)src, len, src_mem, rank, team, task);
    if (status != UCC_OK){
        return status;
    } 
    status = ucc_tl_ucp_recv_nb(dst, len, dst_mem, rank, team, task);

    return status;
}

static ucc_status_t ucc_mc_ucx_memset(void *ptr, int value, size_t len) //NOLINT
{
    /* not supposed to be used */
    mc_error(&ucc_mc_ucx.super, "ucx memory component shouldn't be used for"
                                "memset");
    return UCC_ERR_NOT_SUPPORTED;
}

static ucc_status_t ucc_mc_ucx_mem_query(const void *ptr, //NOLINT
                                         ucc_mem_attr_t *mem_attr) //NOLINT
{
    /* not supposed to be used */
    mc_error(&ucc_mc_ucx.super, "ucx memory component shouldn't be used for"
                                "mem type detection");
    return UCC_ERR_NOT_SUPPORTED;
}

static ucc_status_t ucc_mc_ucx_finalize()
{
    return UCC_OK;
}

ucc_mc_ucx_t ucc_mc_ucx = {
    .super.super.name             = "ucx mc",
    .super.ref_cnt                = 0,
    .super.type                   = UCC_MEMORY_TYPE_UCX,
    .super.ee_type                = UCC_EE_CPU_THREAD,
    .super.init                   = ucc_mc_ucx_init,
    .super.get_attr               = ucc_mc_ucx_get_attr,
    .super.finalize               = ucc_mc_ucx_finalize,
    .super.ops.mem_query          = ucc_mc_ucx_mem_query,
    .super.ops.mem_alloc          = ucc_mc_ucx_mem_pool_alloc_with_init,
    .super.ops.mem_free           = ucc_mc_ucx_mem_pool_free,
    .super.ops.memcpy             = ucc_mc_ucx_memcpy,
    .super.ops.memset             = ucc_mc_ucx_memset,
    .super.ops.flush              = NULL,
    .super.config_table =
        {
            .name   = "ucx memory component",
            .prefix = "MC_UCX_",
            .table  = ucc_mc_ucx_config_table,
            .size   = sizeof(ucc_mc_ucx_config_t),
        },
    .mpool_init_flag               = 0,
};

UCC_CONFIG_REGISTER_TABLE_ENTRY(&ucc_mc_ucx.super.config_table,
                                &ucc_config_global_list);
