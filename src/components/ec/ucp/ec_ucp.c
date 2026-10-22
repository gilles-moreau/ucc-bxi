#include "ec_ucp.h"
#include "utils/ucc_math.h"

#include <ucp/api/ucp.h>

#include <core/ucc_context.h>
#include <unistd.h>

static ucp_atomic_op_t ucc_op_to_ucp_op[] = {[UCC_OP_SUM]  = UCP_ATOMIC_OP_ADD,
                                             [UCC_OP_LAND] = UCP_ATOMIC_OP_AND,
                                             [UCC_OP_LOR]  = UCP_ATOMIC_OP_OR,
                                             [UCC_OP_LXOR] = UCP_ATOMIC_OP_XOR};

static ucp_atomic_type_t ucc_type_to_ucp_type[UCC_DT_PREDEFINED_LAST] = {
    [UCC_DT_PREDEFINED_ID(UCC_DT_UINT32)]  = UCP_ATOMIC_TYPE_UINT32,
    [UCC_DT_PREDEFINED_ID(UCC_DT_FLOAT32)] = UCP_ATOMIC_TYPE_FLOAT,
    [UCC_DT_PREDEFINED_ID(UCC_DT_UINT64)]  = UCP_ATOMIC_TYPE_UINT64,
    [UCC_DT_PREDEFINED_ID(UCC_DT_FLOAT64)] = UCP_ATOMIC_TYPE_DOUBLE};

static ucc_status_t
ucc_ec_ucp_task_reg_mem(ucc_tl_ucp_context_t              *context,
                        const ucc_ee_executor_task_args_t *task_args,
                        ucc_ec_ucp_executor_task_t        *eee_task);
static ucc_status_t
ucc_ec_ucp_task_reduce(const ucc_eee_task_reduce_t *task_args,
                       ucc_ec_ucp_executor_task_t  *task);

static ucc_config_field_t ucc_ec_ucp_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_ec_ucp_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_ec_config_table)},

    {NULL}

};

static ucc_status_t ucc_ec_ucp_init(const ucc_ec_params_t *ec_params)
{
    ucc_status_t status;

    ucc_strncpy_safe(ucc_ec_ucp.super.config->log_component.name,
                     ucc_ec_ucp.super.super.name,
                     sizeof(ucc_ec_ucp.super.config->log_component.name));
    ucc_ec_ucp.thread_mode = ec_params->thread_mode;

    status =
        ucc_mpool_init(&ucc_ec_ucp.executors, 0, sizeof(ucc_ec_ucp_executor_t),
                       0, UCC_CACHE_LINE_SIZE, 16, UINT_MAX, NULL,
                       ec_params->thread_mode, "ec ucp executors");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_ucp.super, "failed to created ec ucp executors mpool");
        return status;
    }

    status = ucc_mpool_init(&ucc_ec_ucp.executor_tasks, 0,
                            sizeof(ucc_ec_ucp_executor_task_t), 0,
                            UCC_CACHE_LINE_SIZE, 16, UINT_MAX, NULL,
                            ec_params->thread_mode, "ec ucp executor tasks");
    if (status != UCC_OK) {
        ec_error(&ucc_ec_ucp.super,
                 "failed to created ec ucp executor tasks mpool");
        ucc_mpool_cleanup(&ucc_ec_ucp.executors, 1);
        return status;
    }

    return UCC_OK;
}

ucc_status_t ucc_ec_ucp_attach(ucc_context_h ctx)
{
    ucs_status_t         ucs_status;
    ucc_status_t         status;
    ucc_tl_context_t    *tl_ctx;
    ucc_tl_ucp_worker_t *worker;
    ucp_ep_params_t      ep_params;

    status = ucc_tl_context_get(ctx, "ucp", &tl_ctx);
    if (UCC_OK != status) {
        ec_debug(&ucc_ec_ucp.super,
                 "TL ucp context is not available, skipping");
        ucc_ec_ucp.ucp_ctx = NULL;
        goto out;
    }

    ucc_ec_ucp.ucp_ctx = ucc_derived_of(tl_ctx, ucc_tl_ucp_context_t);
    worker             = &ucc_ec_ucp.ucp_ctx->worker;

    /* Get worker address to create endpoint. */
    if (NULL == worker->worker_address) {
        ucs_status = ucp_worker_get_address(
            worker->ucp_worker, &worker->worker_address, &worker->ucp_addrlen);
        if (UCS_OK != ucs_status) {
            ec_error(&ucc_ec_ucp.super, "failed to get ucp worker address");
            return ucs_status_to_ucc_status(ucs_status);
        }
    }

    if (NULL == ucc_ec_ucp.self) {
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
        ep_params.address    = (ucp_address_t *)worker->worker_address;

        ucs_status =
            ucp_ep_create(worker->ucp_worker, &ep_params, &ucc_ec_ucp.self);

        if (ucc_unlikely(UCS_OK != ucs_status)) {
            ec_error(&ucc_ec_ucp.super, "ucp returned connect error: %s",
                     ucs_status_string(ucs_status));
            return ucs_status_to_ucc_status(ucs_status);
        }
    }

out:
    return status;
}

static ucc_status_t ucc_ec_ucp_get_attr(ucc_ec_attr_t *ec_attr)
{
    if (ec_attr->field_mask & UCC_EC_ATTR_FIELD_THREAD_MODE) {
        ec_attr->thread_mode = ucc_ec_ucp.thread_mode;
    }

    return UCC_OK;
}

static ucc_status_t ucc_ec_ucp_finalize()
{
    ucc_mpool_cleanup(&ucc_ec_ucp.executors, 1);
    ucc_mpool_cleanup(&ucc_ec_ucp.executor_tasks, 1);

    return UCC_OK;
}

ucc_status_t ucc_ucp_executor_init(const ucc_ee_executor_params_t *params,
                                   ucc_ee_executor_t             **executor)
{
    ucc_ee_executor_t *eee = ucc_mpool_get(&ucc_ec_ucp.executors);

    ec_trace(&ucc_ec_ucp.super, "executor init, eee: %p", eee);
    if (ucc_unlikely(!eee)) {
        ec_error(&ucc_ec_ucp.super, "failed to allocate executor");
        return UCC_ERR_NO_MEMORY;
    }

    eee->ee_type = params->ee_type;
    *executor    = eee;

    return UCC_OK;
}

ucc_status_t ucc_ucp_executor_start(ucc_ee_executor_t *executor,
                                    void              *ee_context)
{
    ucc_ec_ucp_executor_t *eee =
        ucc_derived_of(executor, ucc_ec_ucp_executor_t);

    //TODO: if specified, verify that context is a valid schedule

    eee->super.ee_context = ee_context;
    if (eee->super.ee_context != NULL) {
        eee->flags |= UCC_EC_UCP_EXECUTOR_SCHEDULE;
    }

    return UCC_OK;
}

ucc_status_t
ucc_ucp_executor_status(const ucc_ee_executor_t *UCS_F_MAYBE_UNUSED executor)
{
    return UCC_OK;
}

ucc_status_t
ucc_ucp_executor_stop(ucc_ee_executor_t *UCS_F_MAYBE_UNUSED executor)
{
    return UCC_OK;
}

ucc_status_t
ucc_ucp_executor_task_post(ucc_ee_executor_t                 *executor,
                           const ucc_ee_executor_task_args_t *task_args,
                           ucc_ee_executor_task_t           **task)
{
    ucc_status_t                status = UCC_OK;
    ucc_ec_ucp_executor_task_t *eee_task;

    eee_task = ucc_mpool_get(&ucc_ec_ucp.executor_tasks);
    if (ucc_unlikely(!eee_task)) {
        return UCC_ERR_NO_MEMORY;
    }

    eee_task->super.eee = executor;
    eee_task->self      = ucc_ec_ucp.self;

    /* Register memory. */
    status = ucc_ec_ucp_task_reg_mem(ucc_ec_ucp.ucp_ctx, task_args, eee_task);
    if (status != UCC_OK) {
        goto free_task;
    }

    switch (task_args->task_type) {
    case UCC_EE_EXECUTOR_TASK_REDUCE:
        status = ucc_ec_ucp_task_reduce(&task_args->reduce, eee_task);
        break;
    case UCC_EE_EXECUTOR_TASK_REDUCE_STRIDED:
    {
        ucc_eee_task_reduce_strided_t *trs =
            (ucc_eee_task_reduce_strided_t *)&task_args->reduce_strided;
        size_t                n_srcs = trs->n_src2 + 1;
        void                **srcs;
        ucc_eee_task_reduce_t tr;
        int                   i;

        srcs = &tr.srcs[0];
        for (i = 0; i < n_srcs - 1; i++) {
            srcs[i + 1] = PTR_OFFSET(trs->src2, trs->stride * i);
        }
        tr.count  = trs->count;
        tr.dt     = trs->dt;
        tr.op     = trs->op;
        tr.n_srcs = n_srcs;
        tr.dst    = trs->dst;
        tr.alpha  = trs->alpha;

        status = ucc_ec_ucp_task_reduce(&tr, eee_task);
    } break;
    case UCC_EE_EXECUTOR_TASK_COPY:
    case UCC_EE_EXECUTOR_TASK_COPY_MULTI:
    default:
        status = UCC_ERR_NOT_SUPPORTED;
        goto free_task;
    }
    eee_task->super.status = status;
    *task                  = &eee_task->super;

    return status;

free_task:
    ucc_mpool_put(eee_task);
    return status;
}

static ucc_status_t
ucc_ec_ucp_task_reg_mem(ucc_tl_ucp_context_t              *context,
                        const ucc_ee_executor_task_args_t *task_args,
                        ucc_ec_ucp_executor_task_t        *eee_task)
{
    ucc_status_t         status     = UCC_OK;
    ucs_status_t         ucs_status = UCS_OK;
    void                *rkey_buffer;
    size_t               rkey_buffer_size;
    ucp_mem_map_params_t params;

    params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS
                        | UCP_MEM_MAP_PARAM_FIELD_LENGTH
                        | UCP_MEM_MAP_PARAM_FIELD_FLAGS;
    params.flags = 0;

    switch (task_args->task_type) {
    case UCC_EE_EXECUTOR_TASK_REDUCE:
        params.address = task_args->reduce.dst;
        params.length =
            task_args->reduce.count * ucc_dt_size(task_args->reduce.dt);
        break;
    case UCC_EE_EXECUTOR_TASK_REDUCE_STRIDED:
        params.address = task_args->reduce_strided.dst;
        params.length  = task_args->reduce_strided.count
                        * ucc_dt_size(task_args->reduce_strided.dt);
        break;
    case UCC_EE_EXECUTOR_TASK_COPY:
    case UCC_EE_EXECUTOR_TASK_COPY_MULTI:
    default:
        status = UCC_ERR_NOT_SUPPORTED;
    }

    ucs_status = ucp_mem_map(context->worker.ucp_context, &params,
                             &eee_task->reduce.dst_memh);
    if (UCS_OK != ucs_status) {
        ec_error(&ucc_ec_ucp.super, "failed to map reduce address");
        return ucs_status_to_ucc_status(ucs_status);
    }

    ucs_status =
        ucp_rkey_pack(context->worker.ucp_context, eee_task->reduce.dst_memh,
                      &rkey_buffer, &rkey_buffer_size);
    if (ucs_status != UCS_OK) {
        status = ucs_status_to_ucc_status(ucs_status);
        goto err_unmap_mem;
    }

    ucs_status =
        ucp_ep_rkey_unpack(eee_task->self, rkey_buffer, &eee_task->reduce.rkey);
    if (ucs_status != UCS_OK) {
        status = ucs_status_to_ucc_status(ucs_status);
    }

    ucp_rkey_buffer_release(rkey_buffer);

    return status;

err_unmap_mem:
    ucp_mem_unmap(context->worker.ucp_context, eee_task->reduce.dst_memh);
    return status;
}

static void ucc_ec_ucp_task_unreg_mem(ucc_tl_ucp_context_t       *context,
                                      ucc_ec_ucp_executor_task_t *eee_task)
{
    ucp_mem_unmap(context->worker.ucp_context, eee_task->reduce.dst_memh);
    ucp_rkey_destroy(eee_task->reduce.rkey);
}

void ucc_ec_ucp_atomic_nbx_callback(void *request, ucs_status_t status,
                                    void *user_data)
{
    ucc_ec_ucp_executor_task_t *task = (ucc_ec_ucp_executor_task_t *)user_data;

    task->super.status = ucs_status_to_ucc_status(status);

    ucp_request_free(request);
}

static ucc_status_t ucc_ec_ucp_task_reduce(const ucc_eee_task_reduce_t *task,
                                           ucc_ec_ucp_executor_task_t *eee_task)
{
    ucc_status_t           status = UCC_INPROGRESS;
    ucc_ec_ucp_executor_t *eee =
        ucs_derived_of(eee_task->super.eee, ucc_ec_ucp_executor_t);
    ucp_request_param_t param;

    ucc_assert(task->n_srcs <= UCC_EE_EXECUTOR_NUM_BUFS);

    param.op_attr_mask = UCP_OP_ATTR_FIELD_DATATYPE | UCP_OP_ATTR_FIELD_CALLBACK
                         | UCP_OP_ATTR_FIELD_USER_DATA
                         | UCP_OP_ATTR_FIELD_AMO_TYPE;
    param.op_attr_mask |= (eee->flags & UCC_EC_UCP_EXECUTOR_SCHEDULE)
                              ? UCP_OP_ATTR_FIELD_SCHEDH
                              : 0;
    param.datatype  = ucp_dt_make_contig(ucc_dt_size(task->dt));
    param.cb.send   = ucc_ec_ucp_atomic_nbx_callback;
    param.type      = ucc_type_to_ucp_type[UCC_DT_PREDEFINED_ID(task->dt)];
    param.user_data = (void *)eee_task;
    param.schedh    = (ucp_sched_h)eee->super.ee_context;

    for (int i = 0; task->n_srcs; i++) {
        eee_task->reqs[i] = ucp_atomic_op_nbx(
            eee_task->self, ucc_op_to_ucp_op[task->op], task->srcs[i],
            task->count, (uintptr_t)task->dst, eee_task->reduce.rkey, &param);

        if (UCS_PTR_IS_ERR(eee_task->reqs[i])) {
            status =
                ucs_status_to_ucc_status(UCS_PTR_RAW_STATUS(eee_task->reqs[i]));
        } else if (UCS_PTR_RAW_STATUS(eee_task->reqs[i]) == UCS_OK) {
            status = UCC_OK;
        }
    }

    return status;
}

ucc_status_t ucc_ucp_executor_task_test(const ucc_ee_executor_task_t *task)
{
    return task->status;
}

ucc_status_t ucc_ucp_executor_task_finalize(ucc_ee_executor_task_t *task)
{
    ucc_ec_ucp_executor_task_t *ucp_task =
        ucs_derived_of(task, ucc_ec_ucp_executor_task_t);

    ucc_ec_ucp_task_unreg_mem(ucc_ec_ucp.ucp_ctx, ucp_task);
    ucc_mpool_put(task);
    return UCC_OK;
}

ucc_status_t ucc_ucp_executor_finalize(ucc_ee_executor_t *executor)
{
    ec_trace(&ucc_ec_ucp.super, "executor finalize, eee: %p", executor);
    ucc_mpool_put(executor);

    return UCC_OK;
}

ucc_ec_ucp_t ucc_ec_ucp = {
    .super.super.name = "ucp ec",
    .super.ref_cnt    = 0,
    .super.type       = UCC_EE_UCP,
    .super.init       = ucc_ec_ucp_init,
    .super.attach     = ucc_ec_ucp_attach,
    .super.get_attr   = ucc_ec_ucp_get_attr,
    .super.finalize   = ucc_ec_ucp_finalize,
    .super.config_table =
        {
            .name   = "UCP execution component",
            .prefix = "EC_UCP_",
            .table  = ucc_ec_ucp_config_table,
            .size   = sizeof(ucc_ec_ucp_config_t),
        },
    .super.ops.create_event           = NULL,
    .super.ops.destroy_event          = NULL,
    .super.ops.event_post             = NULL,
    .super.ops.event_test             = NULL,
    .super.executor_ops.init          = ucc_ucp_executor_init,
    .super.executor_ops.start         = ucc_ucp_executor_start,
    .super.executor_ops.status        = ucc_ucp_executor_status,
    .super.executor_ops.stop          = ucc_ucp_executor_stop,
    .super.executor_ops.task_post     = ucc_ucp_executor_task_post,
    .super.executor_ops.task_test     = ucc_ucp_executor_task_test,
    .super.executor_ops.task_finalize = ucc_ucp_executor_task_finalize,
    .super.executor_ops.finalize      = ucc_ucp_executor_finalize,
};

UCC_CONFIG_REGISTER_TABLE_ENTRY(&ucc_ec_ucp.super.config_table,
                                &ucc_config_global_list);
