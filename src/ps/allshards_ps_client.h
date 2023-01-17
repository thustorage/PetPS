#pragma once
#include "base/array.h"
#include "base/factory.h"
#include "base/log.h"
#include "base_client.h"
#include "config.h"
#include "shard_manager.h"

// This should be called only by one THREAD
class AllShardsParameterClientWrapper : public BaseParameterClient {
 public:
  explicit AllShardsParameterClientWrapper(
      const std::vector<BaseParameterClient *> &clients, int num_shards)
      : BaseParameterClient("", 0, 0),
        clients_(clients),
        num_shards_(num_shards) {
    partitioned_key_buffer_.resize(num_shards_);
    partitioned_recv_buffer_.resize(num_shards_);
  }

  ~AllShardsParameterClientWrapper() {}

  virtual int GetParameter(base::ConstArray<uint64_t> keys,
                           std::vector<std::vector<float>> *values,
                           bool perf = true) override {
    CHECK(0);
    return 0;
  }
  virtual void InitThread() override {
    for (int i = 0; i < num_shards_; i++) {
      clients_[i]->InitThread();
    }
  }
  virtual void Barrier(const std::string &ss, int k) override {
    clients_[0]->Barrier(ss, k);
  }

  // message buffer for received embeddings
  virtual void *GetReceiveBuffer(size_t size) override {
    for (int i = 0; i < num_shards_; i++) {
      partitioned_recv_buffer_[i].push_back(clients_[i]->GetReceiveBuffer(
          FLAGS_max_kv_num_per_request * FLAGS_value_size));
    }
    return new char[size];
  }

  virtual int GetParameter(base::ConstArray<uint64_t> keys, float *values,
                           bool isAsync, bool perf = true,
                           int async_req_id = 0) override {
    for (auto &each : partitioned_key_buffer_) each.clear();

    for (auto k : keys) {
      int shard = ShardManager::KeyPartition(k);
      partitioned_key_buffer_[shard].push_back(k);
    }

    for (int i = 0; i < num_shards_; i++) {
      if (partitioned_key_buffer_[i].size() > 500)
        partitioned_key_buffer_[i].resize(500);
    }

    std::vector<int> shard_rpc_ids;
    for (int i = 0; i < num_shards_; i++) {
      CHECK_LE(partitioned_key_buffer_[i].size(), 500);
      auto shard_rpc_id = clients_[i]->GetParameter(
          base::ConstArray<uint64_t>(partitioned_key_buffer_[i]),
          (float *)partitioned_recv_buffer_[i][async_req_id], isAsync, perf,
          async_req_id);
      shard_rpc_ids.push_back(shard_rpc_id);
    }

    auto batchRpcIDAccOld = batchRpcIDAcc_;

    batchRpcId2CbMap_[batchRpcIDAccOld] = [=]() {
#ifdef RPC_DEBUG
      std::vector<int> offset_accs;
      offset_accs.resize(num_shards_, 0);
      int acc = 0;
      for (auto k : keys) {
        int shard = ShardManager::KeyPartition(k);
        memcpy(values + acc / sizeof(float),
               (char *)partitioned_recv_buffer_[shard][async_req_id] +
                   offset_accs[shard],
               FLAGS_value_size);

        offset_accs[shard] += FLAGS_value_size;
        acc += FLAGS_value_size;
      }
      CHECK_EQ(acc, keys.Size() * FLAGS_value_size);
#endif
    };

    batchRpcIDAcc_++;
    batchRpcId2RpcListMap_[batchRpcIDAccOld] = shard_rpc_ids;
    return batchRpcIDAccOld;
  }

  virtual bool QueryRPCFinished(int rpc_id) override {
    for (int i = 0; i < num_shards_; i++) {
      auto shard_rpc_id = batchRpcId2RpcListMap_[rpc_id][i];
      if (shard_rpc_id == MAGIC_RPC_ID_FOR_EMPTY_REQUEST) continue;
      if (!clients_[i]->QueryRPCFinished(shard_rpc_id)) {
        return false;
      } else {
        batchRpcId2CbMap_[rpc_id]();
      }
    }
    return true;
  }

  virtual void WaitRPCFinish(int rpc_id) override {
    for (int i = 0; i < num_shards_; i++) {
      auto shard_rpc_id = batchRpcId2RpcListMap_[rpc_id][i];
      if (shard_rpc_id == MAGIC_RPC_ID_FOR_EMPTY_REQUEST) continue;
      clients_[i]->WaitRPCFinish(shard_rpc_id);
    }
  }

  virtual void RevokeRPCResource(int rpc_id) override {
    for (int i = 0; i < num_shards_; i++) {
      auto shard_rpc_id = batchRpcId2RpcListMap_[rpc_id][i];
      if (shard_rpc_id == MAGIC_RPC_ID_FOR_EMPTY_REQUEST) continue;
      clients_[i]->RevokeRPCResource(shard_rpc_id);
    }
    batchRpcId2RpcListMap_.erase(rpc_id);
    batchRpcId2CbMap_.erase(rpc_id);
  }

  virtual int PutParameter(
      const std::vector<uint64_t> &keys,
      const std::vector<std::vector<float>> &values) override {
    LOG(FATAL) << "TODO";
    return 0;
  }

 private:
  std::vector<BaseParameterClient *> clients_;
  std::unordered_map<uint64_t, std::vector<int>> batchRpcId2RpcListMap_;
  std::unordered_map<uint64_t, std::function<void()>> batchRpcId2CbMap_;
  int num_shards_;
  uint64_t batchRpcIDAcc_ = 0;
  std::vector<std::vector<uint64_t>> partitioned_key_buffer_;
  std::vector<std::vector<void *>> partitioned_recv_buffer_;
  static constexpr uint64_t MAGIC_RPC_ID_FOR_EMPTY_REQUEST = -1;
};