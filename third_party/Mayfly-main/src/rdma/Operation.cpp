#include "Rdma.h"

int pollWithCQ(ibv_cq *cq, int pollNumber, struct ibv_wc *wc) {
  int count = 0;

  do {

    int new_count = ibv_poll_cq(cq, 1, wc);
    count += new_count;

  } while (count < pollNumber);

  if (count < 0) {
    Debug::notifyError("Poll Completion failed.");
    return -1;
  }

  if (wc->status != IBV_WC_SUCCESS) {
    Debug::notifyError("Failed status %s (%d) for wr_id %d",
                       ibv_wc_status_str(wc->status), wc->status,
                       (int)wc->wr_id);
    sleep(1);
    return -1;
  }

  return count;
}

int pollOnce(ibv_cq *cq, int pollNumber, struct ibv_wc *wc) {
  int count = ibv_poll_cq(cq, pollNumber, wc);
  if (count <= 0) {
    return 0;
  }
  if (wc->status != IBV_WC_SUCCESS) {
    Debug::notifyError("Failed status %s (%d) for wr_id %d",
                       ibv_wc_status_str(wc->status), wc->status,
                       (int)wc->wr_id);
    sleep(1);
    return -1;
  } else {
    return count;
  }
}

static inline void fillSgeWr(ibv_sge &sg, ibv_send_wr &wr, uint64_t source,
                             uint64_t size, uint32_t lkey) {
  memset(&sg, 0, sizeof(sg));
  sg.addr = (uintptr_t)source;
  sg.length = size;
  sg.lkey = lkey;

  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 0;
  wr.sg_list = &sg;
  wr.num_sge = 1;
}

static inline void fillSgeWr(ibv_sge &sg, ibv_recv_wr &wr, uint64_t source,
                             uint64_t size, uint32_t lkey) {
  memset(&sg, 0, sizeof(sg));
  sg.addr = (uintptr_t)source;
  sg.length = size;
  sg.lkey = lkey;

  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 0;
  wr.sg_list = &sg;
  wr.num_sge = 1;
}

// for UD and DC
bool rdmaSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
              ibv_ah *ah, uint32_t remoteQPN /* remote dct_number */,
              bool isSignaled) {

  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  wr.opcode = IBV_WR_SEND;

  wr.wr.ud.ah = ah;
  wr.wr.ud.remote_qpn = remoteQPN;
  wr.wr.ud.remote_qkey = UD_PKEY;

  if (size <= kMaxInlineData) {
    wr.send_flags |= IBV_SEND_INLINE;
  }

  if (isSignaled)
    wr.send_flags = IBV_SEND_SIGNALED;
  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with RDMA_SEND failed.");
    return false;
  }
  return true;
}

// for UD and DC
bool rdmaSend2Sge(ibv_qp *qp, uint64_t source1, uint64_t size1,
                  uint64_t source2, uint64_t size2, uint32_t lkey1,
                  uint32_t lkey2, ibv_ah *ah,
                  uint32_t remoteQPN /* remote dct_number */, bool isSignaled) {

  struct ibv_sge sg[2];
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  sg[0].addr = (uintptr_t)source1;
  sg[0].length = size1;
  sg[0].lkey = lkey1;

  sg[1].addr = (uintptr_t)source2;
  sg[1].length = size2;
  sg[1].lkey = lkey2;

  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 0;
  wr.sg_list = sg;
  wr.num_sge = 2;

  wr.opcode = IBV_WR_SEND;

  wr.wr.ud.ah = ah;
  wr.wr.ud.remote_qpn = remoteQPN;
  wr.wr.ud.remote_qkey = UD_PKEY;

  // if (size1 <= kMaxInlineData) {
  //   wr.send_flags |= IBV_SEND_INLINE;
  // }

  if (isSignaled)
    wr.send_flags = IBV_SEND_SIGNALED;
  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with RDMA_SEND failed %d %d.\n", size1, size2);
    perror("CCCC");
    return false;
  }
  return true;
}

// for RC & UC
bool rdmaSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
              int32_t imm) {

  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  if (imm != -1) {
    wr.imm_data = imm;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
  } else {
    wr.opcode = IBV_WR_SEND;
  }

  wr.send_flags = IBV_SEND_SIGNALED;
  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with RDMA_SEND failed.");
    sleep(1);
    return false;
  }
  return true;
}

bool rdmaPersistSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
                     const ReadDescriptor &read_desc, uint64_t wr_id) {
  struct ibv_sge sg[2];
  struct ibv_send_wr wr[2];
  struct ibv_send_wr *wrBad;

  size = (size + 64 - 1) & (~(64 - 1));

  memset(sg, 0, sizeof(sg));
  memset(wr, 0, sizeof(wr));

  sg[0].addr = (uintptr_t)source;
  sg[0].length = size;
  sg[0].lkey = lkey;
  wr[0].sg_list = &sg[0];
  wr[0].num_sge = 1;
  wr[0].opcode = IBV_WR_SEND;

  // if (size <= kMaxInlineData) {
  //   wr[0].send_flags |= IBV_SEND_INLINE;
  // }

  // #define WITHOUT_READ

#ifdef WITHOUT_READ
  wr[0].send_flags = IBV_SEND_SIGNALED;
#else
  wr[0].next = &wr[1];
#endif

  sg[1].addr = (uintptr_t)read_desc.next_source();
  sg[1].length = kPersistReadSize;
  sg[1].lkey = read_desc.lkey;
  wr[1].sg_list = &sg[1];
  wr[1].num_sge = 1;
  wr[1].wr.rdma.remote_addr = read_desc.next_dest();
  wr[1].wr.rdma.rkey = read_desc.rkey;
  wr[1].opcode = IBV_WR_RDMA_READ;
  wr[1].send_flags = IBV_SEND_SIGNALED;
  wr[1].next = NULL;
  wr[1].wr_id = wr_id;
  // wr[1].send_flags |= IBV_SEND_INLINE;

  if (ibv_post_send(qp, &wr[0], &wrBad)) {
    Debug::notifyError("Pesistent Send failed.");
    sleep(1);
    return false;
  }
  return true;
}

bool rdmaReceive(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
                 uint64_t wr_id) {
  struct ibv_sge sg;
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  wr.wr_id = wr_id;

  if (ibv_post_recv(qp, &wr, &wrBad)) {
    Debug::notifyError("Receive with RDMA_RECV failed.");
    return false;
  }
  return true;
}

bool rdmaReceive(ibv_srq *srq, uint64_t source, uint64_t size, uint32_t lkey) {

  struct ibv_sge sg;
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  if (ibv_post_srq_recv(srq, &wr, &wrBad)) {
    Debug::notifyError("Receive with SRQ RDMA_RECV failed.");
    return false;
  }
  return true;
}

// for RC & UC
bool rdmaRead(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
              uint32_t lkey, uint32_t remoteRKey, bool signal, uint64_t wrID) {
  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  wr.opcode = IBV_WR_RDMA_READ;

  if (signal) {
    wr.send_flags = IBV_SEND_SIGNALED;
  }

  wr.wr.rdma.remote_addr = dest;
  wr.wr.rdma.rkey = remoteRKey;
  wr.wr_id = wrID;

  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with RDMA_READ failed.");
    return false;
  }
  return true;
}

// for RC & UC
bool rdmaWrite(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
               uint32_t lkey, uint32_t remoteRKey, int32_t imm, bool isSignaled,
               uint64_t wrID) {

  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, size, lkey);

  if (imm == -1) {
    wr.opcode = IBV_WR_RDMA_WRITE;
  } else {
    wr.imm_data = imm;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  }

  if (isSignaled) {
    wr.send_flags = IBV_SEND_SIGNALED;
  }

  wr.wr.rdma.remote_addr = dest;
  wr.wr.rdma.rkey = remoteRKey;
  wr.wr_id = wrID;

  if (ibv_post_send(qp, &wr, &wrBad) != 0) {
    Debug::notifyError("Send with RDMA_WRITE(WITH_IMM) failed. rdmaWrite");
    assert(0);
    sleep(10);
    return false;
  }
  return true;
}

bool rdmaWriteVector(ibv_qp *qp, const SourceList *source_list,
                     const int list_size, uint64_t dest, uint32_t lkey,
                     uint32_t remoteRKey, bool isSignaled, uint64_t wrID,
                     int sgePerWr) {

  // int kCntSeg = 30;
  int kCntSeg = sgePerWr;
  int kCntWr = (list_size + kCntSeg - 1) / kCntSeg;

  struct ibv_send_wr wr[kCntWr];
  struct ibv_sge sg[kCntWr][kCntSeg];

  memset(wr, 0, sizeof(wr));
  memset(sg, 0, sizeof(sg));

  int acc_size = 0;

  for (int i = 0; i < list_size; i++) {
    int req_id = i / kCntSeg;
    int sge_id = i % kCntSeg;

    auto &sge = sg[req_id][sge_id];

    sge.addr = (uintptr_t)source_list[i].addr;
    sge.length = source_list[i].size;
    sge.lkey = lkey;

    if (sge_id == 0) {
      wr[req_id].sg_list = &sge;
      wr[req_id].num_sge = std::min(kCntSeg, list_size - req_id * kCntSeg);
      wr[req_id].opcode = IBV_WR_RDMA_WRITE;
      wr[req_id].wr.rdma.remote_addr = dest + acc_size;
      wr[req_id].wr.rdma.rkey = remoteRKey;
      wr[req_id].wr_id = wrID;

      wr[req_id].next = &wr[req_id + 1];
    }
    acc_size += source_list[i].size;

    if (i == list_size - 1) {
      wr[req_id].send_flags = IBV_SEND_SIGNALED;
      wr[req_id].next = nullptr;
    }
  }

  struct ibv_send_wr *wrBad;
  if (ibv_post_send(qp, &wr[0], &wrBad) != 0) {
    Debug::notifyError(
        "Send with RDMA_WRITE(WITH_IMM) failed. rdmaWriteVector");
    assert(0);
    exit(-1);
    return false;
  }
  return true;
}

bool rdmaWritePersist(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
                      uint32_t lkey, uint32_t remoteRKey,
                      const ReadDescriptor &read_desc, bool isSignaled,
                      uint64_t wrID) {

  struct ibv_sge sg[2];
  struct ibv_send_wr wr[2];
  struct ibv_send_wr *wrBad;

  size = (size + 64 - 1) & (~(64 - 1));

  memset(sg, 0, sizeof(sg));
  memset(wr, 0, sizeof(wr));

  sg[0].addr = (uintptr_t)source;
  sg[0].length = size;
  sg[0].lkey = lkey;
  wr[0].sg_list = &sg[0];
  wr[0].num_sge = 1;
  wr[0].opcode = IBV_WR_RDMA_WRITE;
  wr[0].wr.rdma.remote_addr = dest;
  wr[0].wr.rdma.rkey = remoteRKey;
  wr[0].wr_id = wrID;

  wr[0].send_flags = IBV_SEND_SIGNALED;
  wr[0].next = nullptr;

  sg[1].addr = (uintptr_t)read_desc.next_source();
  sg[1].length = kPersistReadSize;
  sg[1].lkey = read_desc.lkey;
  wr[1].sg_list = &sg[1];
  wr[1].num_sge = 1;
  wr[1].wr.rdma.remote_addr = read_desc.next_dest();
  wr[1].wr.rdma.rkey = read_desc.rkey;
  wr[1].opcode = IBV_WR_RDMA_READ;
  wr[1].send_flags = IBV_SEND_SIGNALED;
  wr[1].next = NULL;
  wr[1].wr_id = wrID;

  if (ibv_post_send(qp, &wr[0], &wrBad) != 0) {
    Debug::notifyError(
        "Send with RDMA_WRITE(WITH_IMM) failed. rdmaWritePersist");
    assert(0);
    sleep(10);
    return false;
  }
  return true;
}

// RC & UC
bool rdmaFetchAndAdd(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t add,
                     uint32_t lkey, uint32_t remoteRKey) {
  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, 8, lkey);

  wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
  wr.send_flags = IBV_SEND_SIGNALED;

  wr.wr.atomic.remote_addr = dest;
  wr.wr.atomic.rkey = remoteRKey;
  wr.wr.atomic.compare_add = add;

  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with ATOMIC_FETCH_AND_ADD failed.");
    return false;
  }
  return true;
}

// for RC & UC
bool rdmaCompareAndSwap(ibv_qp *qp, uint64_t source, uint64_t dest,
                        uint64_t compare, uint64_t swap, uint32_t lkey,
                        uint32_t remoteRKey, bool signal) {
  struct ibv_sge sg;
  struct ibv_send_wr wr;
  struct ibv_send_wr *wrBad;

  fillSgeWr(sg, wr, source, 8, lkey);

  wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;

  if (signal) {
    wr.send_flags = IBV_SEND_SIGNALED;
  }

  wr.wr.atomic.remote_addr = dest;
  wr.wr.atomic.rkey = remoteRKey;
  wr.wr.atomic.compare_add = compare;
  wr.wr.atomic.swap = swap;

  if (ibv_post_send(qp, &wr, &wrBad)) {
    Debug::notifyError("Send with ATOMIC_CMP_AND_SWP failed.");
    return false;
  }
  return true;
}
