#pragma once
#include "base/array.h"
#include <memory>
#include <string>

class BaseParameterClient {
public:
  explicit BaseParameterClient(const std::string &host, int port, int shard)
      : host_(host), port_(port), shard_(shard) {}
  virtual ~BaseParameterClient() {}

  virtual int GetParameter(base::ConstArray<uint64_t> keys,
                           std::vector<std::vector<float>> *values,
                           bool perf = true) = 0;
  virtual void InitThread() = 0;

  virtual void Barrier(const std::string &ss, int k) {
    LOG(FATAL) << "not implementation";
  }

  // message buffer for received embeddings
  virtual void *GetReceiveBuffer(size_t size) = 0;

  virtual int GetParameter(base::ConstArray<uint64_t> keys, float *values,
                           bool isAsync, bool perf = true,
                           int async_req_id = 0) = 0;

  virtual bool QueryRPCFinished(int rpc_id) = 0;

  virtual void WaitRPCFinish(int rpc_id) = 0;

  virtual void RevokeRPCResource(int rpc_id) = 0;

  virtual int PutParameter(const std::vector<uint64_t> &keys,
                           const std::vector<std::vector<float>> &values) = 0;

  virtual int FakePutParameter(base::ConstArray<uint64_t>keys,
                               float *values) {
    LOG(FATAL) << "not Implement";
    return 0;
  }

protected:
  std::string host_;
  int port_;
  int shard_;
};
