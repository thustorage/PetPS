#ifndef _RDMA_H__
#define _RDMA_H__

#define forceinline inline __attribute__((always_inline))

#include <assert.h>
#include <cstring>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <list>
#include <string>

#include "Debug.h"

// #define OFED_VERSION_5

#define MAX_POST_LIST 32
#define DCT_ACCESS_KEY 3185
#define UD_PKEY 0x11111111
#define PSN 3185

extern int kMaxDeviceMemorySize;

constexpr size_t kLogNumStrides = 16;
constexpr size_t kLogStrideBytes = 6;
constexpr size_t kStrideSegSize =
    (1ull << kLogNumStrides) * (1ull << kLogStrideBytes);

// set is to 8 can cap the IOPS to 28Mops, since cache line contention between
// CPU cores and the CPU’s on-die PCIe controller
constexpr size_t kRecvCQDepth = 128;

constexpr size_t kStridesPerWQE = (1ull << kLogNumStrides);
// constexpr size_t kCQESnapshotCycle = 65536 * kStridesPerWQE;

constexpr size_t kMpSrqSize = 2048;

constexpr size_t kPersistReadSize = 1;
struct ReadDescriptor {
  uint64_t source;
  uint32_t lkey;
  uint64_t dest;
  uint32_t rkey;

  mutable uint8_t cur;

  uint64_t next_source() const {
    cur = (cur + 1) % 64;
    return source + 64 * cur;
  }

  uint64_t next_dest() const { return dest + 64 * cur; }
};

struct RdmaContext {
  uint8_t devIndex;
  uint8_t port;
  int gidIndex;

  ibv_context *ctx;
  ibv_pd *pd;

  uint16_t lid;
  union ibv_gid gid;

  RdmaContext() : ctx(NULL), pd(NULL) {}
};

struct Region {
  uint64_t source;
  uint32_t size;

  uint64_t dest;
};

//// Resource.cpp
bool createContext(RdmaContext *context, uint8_t port = 1, int gidIndex = 1,
                   uint8_t devIndex = 0);
bool destoryContext(RdmaContext *context);

ibv_mr *createMemoryRegion(uint64_t mm, uint64_t mmSize, RdmaContext *ctx,
                           bool is_physical_mapping = false);
ibv_mr *createMemoryRegionOnChip(uint64_t mm, uint64_t mmSize,
                                 RdmaContext *ctx);

#define kMaxInlineData 64

bool createQueuePair(ibv_qp **qp, ibv_qp_type mode, ibv_cq *cq,
                     RdmaContext *context, uint32_t qpsMaxDepth = 1024,
                     uint32_t maxInlineData = kMaxInlineData);

bool createQueuePair(ibv_qp **qp, ibv_qp_type mode, ibv_cq *send_cq,
                     ibv_cq *recv_cq, RdmaContext *context,
                     uint32_t qpsMaxDepth = 1024,
                     uint32_t maxInlineData = kMaxInlineData);

ibv_srq *createSRQ(RdmaContext *context, ibv_cq **cq);

// bool createDCTarget(ibv_exp_dct **dct, ibv_cq *cq, RdmaContext *context,
//                     uint32_t qpsMaxDepth = 128, uint32_t maxInlineData = 0);
void fillAhAttr(ibv_ah_attr *attr, uint32_t remoteLid, uint8_t *remoteGid,
                RdmaContext *context);

//// StateTrans.cpp
bool modifyQPtoInit(struct ibv_qp *qp, RdmaContext *context);
bool modifyQPtoRTR(struct ibv_qp *qp, uint32_t remoteQPN, uint16_t remoteLid,
                   uint8_t *gid, RdmaContext *context);
bool modifyQPtoRTS(struct ibv_qp *qp);

bool modifyUDtoRTS(struct ibv_qp *qp, RdmaContext *context);
bool modifyDCtoRTS(struct ibv_qp *qp, uint16_t remoteLid, uint8_t *remoteGid,
                   RdmaContext *context);

//// Operation.cpp
int pollWithCQ(ibv_cq *cq, int pollNumber, struct ibv_wc *wc);
int pollOnce(ibv_cq *cq, int pollNumber, struct ibv_wc *wc);

bool rdmaPersistSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
                     const ReadDescriptor &read_desc, uint64_t wr_id);

bool rdmaSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
              ibv_ah *ah, uint32_t remoteQPN, bool isSignaled = false);
bool rdmaSend2Sge(ibv_qp *qp, uint64_t source1, uint64_t size1,
                  uint64_t source2, uint64_t size2, uint32_t lkey1,
                  uint32_t lkey2, ibv_ah *ah, uint32_t remoteQPN,
                  bool isSignaled);

bool rdmaSend(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
              int32_t imm = -1);

bool rdmaReceive(ibv_qp *qp, uint64_t source, uint64_t size, uint32_t lkey,
                 uint64_t wr_id = 0);
bool rdmaReceive(ibv_srq *srq, uint64_t source, uint64_t size, uint32_t lkey);
// bool rdmaReceive(ibv_exp_dct *dct, uint64_t source, uint64_t size,
//                  uint32_t lkey);

bool rdmaRead(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
              uint32_t lkey, uint32_t remoteRKey, bool signal = true,
              uint64_t wrID = 0);
bool rdmaRead(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
              uint32_t lkey, uint32_t remoteRKey, ibv_ah *ah,
              uint32_t remoteDctNumber);

bool rdmaWritePersist(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
                      uint32_t lkey, uint32_t remoteRKey,
                      const ReadDescriptor &read_desc, bool isSignaled = true,
                      uint64_t wrID = 0);

bool rdmaWrite(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
               uint32_t lkey, uint32_t remoteRKey, int32_t imm = -1,
               bool isSignaled = true, uint64_t wrID = 0);

struct SourceList {
  const void *addr;
  uint16_t size;
};

bool rdmaWriteVector(ibv_qp *qp, const SourceList *source_list,
                     const int list_size, uint64_t dest, uint32_t lkey,
                     uint32_t remoteRKey, bool isSignaled = true,
                     uint64_t wrID = 0, int sgePerWr = 30);

bool rdmaWrite2(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t size,
                uint32_t lkey, uint32_t remoteRKey, int32_t imm,
                bool isSignaled = true, uint64_t wrID = 0);

bool rdmaFetchAndAdd(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t add,
                     uint32_t lkey, uint32_t remoteRKey);
bool rdmaFetchAndAdd(ibv_qp *qp, uint64_t source, uint64_t dest, uint64_t add,
                     uint32_t lkey, uint32_t remoteRKey, ibv_ah *ah,
                     uint32_t remoteDctNumber);

bool rdmaCompareAndSwap(ibv_qp *qp, uint64_t source, uint64_t dest,
                        uint64_t compare, uint64_t swap, uint32_t lkey,
                        uint32_t remoteRKey, bool signal = true);
bool rdmaCompareAndSwap(ibv_qp *qp, uint64_t source, uint64_t dest,
                        uint64_t compare, uint64_t swap, uint32_t lkey,
                        uint32_t remoteRKey, ibv_ah *ah,
                        uint32_t remoteDctNumber);
//// Utility.cpp
void rdmaQueryQueuePair(ibv_qp *qp);
void checkDctSupported(struct ibv_context *ctx);

#endif
