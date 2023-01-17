#include "HugePageAlloc.h"
#include "Rdma.h"
#include <arpa/inet.h>

#include <cmath>

char rnic_name = '0';
int gid_index = 3;

#define likely(x) __builtin_expect(!!(x), 1)

bool createContext(RdmaContext *context, uint8_t port, int gidIndex,
                   uint8_t devIndex) {

  gidIndex = gid_index;

  ibv_device *dev = NULL;
  ibv_context *ctx = NULL;
  ibv_pd *pd = NULL;
  ibv_port_attr portAttr;

  // get device names in the system
  int devicesNum;
  struct ibv_device **deviceList = ibv_get_device_list(&devicesNum);
  if (!deviceList) {
    Debug::notifyError("failed to get IB devices list");
    goto CreateResourcesExit;
  }

  // if there isn't any IB device in host
  if (!devicesNum) {
    Debug::notifyInfo("found %d device(s)", devicesNum);
    goto CreateResourcesExit;
  }
  // Debug::notifyInfo("Open IB Device");

  for (int i = 0; i < devicesNum; ++i) {
    // printf("Device %d: %s\n", i, ibv_get_device_name(deviceList[i]));
    if (ibv_get_device_name(deviceList[i])[5] == rnic_name) {
      devIndex = i;
      break;
    }
  }

  if (devIndex >= devicesNum) {
    Debug::notifyError("ib device wasn't found");
    goto CreateResourcesExit;
  }

  dev = deviceList[devIndex];
  printf("I open %s :)\n", ibv_get_device_name(dev));

  // get device handle
  ctx = ibv_open_device(dev);
  if (!ctx) {
    Debug::notifyError("failed to open device");
    goto CreateResourcesExit;
  }
  /* We are now done with device list, free it */
  ibv_free_device_list(deviceList);
  deviceList = NULL;

  // query port properties
  if (ibv_query_port(ctx, port, &portAttr)) {
    Debug::notifyError("ibv_query_port failed");
    goto CreateResourcesExit;
  }

  // allocate Protection Domain
  // Debug::notifyInfo("Allocate Protection Domain");
  pd = ibv_alloc_pd(ctx);
  if (!pd) {
    Debug::notifyError("ibv_alloc_pd failed");
    goto CreateResourcesExit;
  }

  if (ibv_query_gid(ctx, port, gidIndex, &context->gid)) {
    Debug::notifyError("could not get gid for port: %d, gidIndex: %d", port,
                       gidIndex);
    goto CreateResourcesExit;
  }

  // Success :)
  context->devIndex = devIndex;
  context->gidIndex = gidIndex;
  context->port = port;
  context->ctx = ctx;
  context->pd = pd;
  context->lid = portAttr.lid;

  return true;

/* Error encountered, cleanup */
CreateResourcesExit:
  Debug::notifyError("Error Encountered, Cleanup ...");

  if (pd) {
    ibv_dealloc_pd(pd);
    pd = NULL;
  }
  if (ctx) {
    ibv_close_device(ctx);
    ctx = NULL;
  }
  if (deviceList) {
    ibv_free_device_list(deviceList);
    deviceList = NULL;
  }

  return false;
}

bool destoryContext(RdmaContext *context) {
  bool rc = true;
  if (context->pd) {
    if (ibv_dealloc_pd(context->pd)) {
      Debug::notifyError("Failed to deallocate PD");
      rc = false;
    }
  }
  if (context->ctx) {
    if (ibv_close_device(context->ctx)) {
      Debug::notifyError("failed to close device context");
      rc = false;
    }
  }

  return rc;
}

ibv_mr *createMemoryRegion(uint64_t mm, uint64_t mmSize, RdmaContext *ctx,
                           bool is_physical_mapping) {

  ibv_mr *mr = NULL;

  // auto flag = (mmSize < 100ull * 1024 * 1024) ? 0 : IBV_ACCESS_ON_DEMAND;
  auto flag = 0;
  mr = ibv_reg_mr(ctx->pd, (void *)mm, mmSize,
                  IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | flag
                  /* | IBV_ACCESS_ON_DEMAND */);

  // mr = ibv_reg_mr(ctx->pd, (void *)mm, mmSize,
  //                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
  //                     IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
  if (!mr) {
    Debug::notifyError(
        "Memory registration failed, mm=%p, mm_end=%p, mmSize=%lld", mm,
        mm + mmSize, mmSize);
  }

  return mr;
}

bool createQueuePair(ibv_qp **qp, ibv_qp_type mode, ibv_cq *send_cq,
                     ibv_cq *recv_cq, RdmaContext *context,
                     uint32_t qpsMaxDepth, uint32_t maxInlineData) {
//  static int i = 0;
//  printf("%d, %p %p %p\n", i++, send_cq, recv_cq, context);
#ifdef OFED_VERSION_5
  struct ibv_qp_init_attr_ex attr;
  memset(&attr, 0, sizeof(attr));

  attr.qp_type = mode;
  attr.sq_sig_all = 0;
  attr.send_cq = send_cq;
  attr.recv_cq = recv_cq;
  attr.pd = context->pd;

  if (mode == IBV_QPT_RC) {
    attr.comp_mask = IBV_QP_INIT_ATTR_CREATE_FLAGS | IBV_QP_INIT_ATTR_PD;
    // attr.max_atomic_arg = 32;
  } else {
    attr.comp_mask = IBV_QP_INIT_ATTR_PD;
  }

  attr.cap.max_send_wr = qpsMaxDepth;
  attr.cap.max_recv_wr = qpsMaxDepth;
  attr.cap.max_send_sge = 1;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;

  *qp = ibv_create_qp_ex(context->ctx, &attr);
  if (!(*qp)) {
    Debug::notifyError("Failed to create QP");
    perror("XXX");
    return false;
  }
#else
  struct ibv_exp_qp_init_attr attr;
  memset(&attr, 0, sizeof(attr));

  attr.qp_type = mode;
  attr.sq_sig_all = 0;
  attr.send_cq = send_cq;
  attr.recv_cq = recv_cq;
  attr.pd = context->pd;

  if (mode == IBV_QPT_RC) {
    attr.comp_mask = IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS |
                     IBV_EXP_QP_INIT_ATTR_PD | IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
    attr.max_atomic_arg = maxInlineData;
  } else {
    attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;
  }

  attr.cap.max_send_wr = qpsMaxDepth;
  attr.cap.max_recv_wr = qpsMaxDepth;
  // NOTE(xieminhui):
  attr.cap.max_send_sge = 30;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = maxInlineData;

  *qp = ibv_exp_create_qp(context->ctx, &attr);
  if (!(*qp)) {
    Debug::notifyError("Failed to create QP");
    return false;
  }

#endif

  return true;
}

bool createQueuePair(ibv_qp **qp, ibv_qp_type mode, ibv_cq *cq,
                     RdmaContext *context, uint32_t qpsMaxDepth,
                     uint32_t maxInlineData) {
  return createQueuePair(qp, mode, cq, cq, context, qpsMaxDepth, maxInlineData);
}

void fillAhAttr(ibv_ah_attr *attr, uint32_t remoteLid, uint8_t *remoteGid,
                RdmaContext *context) {

  (void)remoteGid;

  memset(attr, 0, sizeof(ibv_ah_attr));
  attr->dlid = remoteLid;
  attr->sl = 0;
  attr->src_path_bits = 0;
  attr->port_num = context->port;

  // attr->is_global = 0;

  // fill ah_attr with GRH
  attr->is_global = 1;
  memcpy(&attr->grh.dgid, remoteGid, 16);
  attr->grh.flow_label = 0;
  attr->grh.hop_limit = 1;
  attr->grh.sgid_index = context->gidIndex;
  attr->grh.traffic_class = 0;
}
