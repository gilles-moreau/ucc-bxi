#include "ec_ucp.h"

#include <ucp/api/ucp.h>

static ucc_config_field_t ucc_ec_ucp_config_table[] = {
    {"", "", NULL, ucc_offsetof(ucc_ec_ucp_config_t, super),
     UCC_CONFIG_TYPE_TABLE(ucc_ec_config_table)},

    {NULL}

};

ucc_ec_ucp_t ucc_ec_ucp = {
    .super.super.name                 = "ucp ec",
    .super.ref_cnt                    = 0,
    .super.type                       = UCC_EE_UCP,
    .super.init                       = NULL,
    .super.get_attr                   = NULL,
    .super.finalize                   = NULL,
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
    .super.executor_ops.init          = NULL,
    .super.executor_ops.start         = NULL,
    .super.executor_ops.status        = NULL,
    .super.executor_ops.stop          = NULL,
    .super.executor_ops.task_post     = NULL,
    .super.executor_ops.task_test     = NULL,
    .super.executor_ops.task_finalize = NULL,
    .super.executor_ops.finalize      = NULL,
};

UCC_CONFIG_REGISTER_TABLE_ENTRY(&ucc_ec_ucp.super.config_table,
                                &ucc_config_global_list);
