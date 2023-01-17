#include "Rdma.h"
bool modifyQPtoInit(struct ibv_qp *qp, RdmaContext *context) {

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = context->port;
    attr.pkey_index = 0;

    switch (qp->qp_type) {
        case IBV_QPT_RC:
            attr.qp_access_flags = IBV_ACCESS_REMOTE_READ |
                                   IBV_ACCESS_REMOTE_WRITE |
                                   IBV_ACCESS_REMOTE_ATOMIC;
            break;

        case IBV_QPT_UC:
            attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
            break;

        default:
            Debug::notifyError("implement me:)");
    }

    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                     IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        Debug::notifyError("Failed to modify QP state to INIT");
        return false;
    }
    return true;
}

bool modifyQPtoRTR(struct ibv_qp *qp, uint32_t remoteQPN, uint16_t remoteLid,
                   uint8_t *remoteGid, RdmaContext *context) {

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;

    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remoteQPN;
    attr.rq_psn = PSN;

    fillAhAttr(&attr.ah_attr, remoteLid, remoteGid, context);

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 12;
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }

    if (ibv_modify_qp(qp, &attr, flags)) {
        Debug::notifyError("failed to modify QP state to RTR");
        return false;
    }
    return true;
}

bool modifyQPtoRTS(struct ibv_qp *qp) {
    struct ibv_qp_attr attr;
    int flags;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7; // 7 is infinte
        attr.max_rd_atomic = 16;
        flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                 IBV_QP_MAX_QP_RD_ATOMIC;
    }

    if (ibv_modify_qp(qp, &attr, flags)) {
        Debug::notifyError("failed to modify QP state to RTS");
        return false;
    }
    return true;
}

bool modifyUDtoRTS(struct ibv_qp *qp, RdmaContext *context) {
    // assert(qp->qp_type == IBV_QPT_UD);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = context->port;
    attr.qkey = UD_PKEY;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                         IBV_QP_PORT | IBV_QP_QKEY)) {
            Debug::notifyError("Failed to modify QP state to INIT");
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PORT)) {
            Debug::notifyError("Failed to modify QP state to INIT");
            return false;
        }
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
        Debug::notifyError("failed to modify QP state to RTR");
        return false;
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            Debug::notifyError("failed to modify QP state to RTS");
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            Debug::notifyError("failed to modify QP state to RTS");
            return false;
        }
    }
    return true;
}

