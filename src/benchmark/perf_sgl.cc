#include "base/array.h"
#include "base/async_time.h"
#include "base/base.h"
#include "base/factory.h"
#include "base/hash.h"
#include "base/timer.h"

#include "memory/shm_file.h"
#include <cstdint>
#include <folly/init/Init.h>
#include <future>
#include <string>
#include <vector>

#include "ps/base_client.h"

#include "benchmark/benchmark_client_common.h"
#include "third_party/Mayfly-main/include/DSM.h"

DEFINE_string(actor, "", "server/client");
DECLARE_int32(value_size);
DECLARE_int32(max_kv_num_per_request);
DEFINE_int64(key_space_m, 100, "key space in million");
DEFINE_int32(batch_read_count, 300, "");
DEFINE_bool(use_sglist, true, "");
DEFINE_bool(use_dram, true, "");
DEFINE_bool(preload, false, "");
DEFINE_double(zipf_theta, 0.9, "");
DEFINE_int32(thread_num, 1, "client thread num");
DEFINE_int32(async_req_num, 1, "");
DEFINE_int32(sge_per_wr, 30, "# of scatter-gather-element per wr");
DEFINE_int32(fake_pm_read_times, 0, "# of fake pm read, for motivation exp");

// void FOLLY_NOINLINE ReadUint64(uint64_t *ptr) { asm volatile("" : "=m"(*ptr)
// : "r"(*ptr)); }
void FOLLY_ALWAYS_INLINE ReadUint64(uint64_t *ptr) {
  asm volatile("" : "=m"(*ptr) : "r"(*ptr));
}

class Client {
public:
  Client(int thread_count) {
    BenchmarkClientCommon::BenchmarkClientCommonArgs args;
    args.thread_count_ = thread_count;
    args.async_req_num_ = FLAGS_async_req_num;
    args.batch_read_count_ = FLAGS_batch_read_count;
    args.key_space_M_ = FLAGS_key_space_m;
    args.zipf_theta_ = FLAGS_zipf_theta;
    args.value_size_ = FLAGS_value_size;
    benchmark_client_.reset(new BenchmarkClientCommon(args));
  }
  void Main() { benchmark_client_->Run(); }

private:
  std::unique_ptr<BenchmarkClientCommon> benchmark_client_;
};

class Server {
public:
  Server(int thread_count)
      : thread_count_(thread_count), get_parameter_timer_("GetParameter", 1),
        value_timer_("Value Part", 1) {
    ClusterInfo cluster;
    cluster.serverNR = 1;
    cluster.clientNR = 1;
    DSMConfig config(CacheConfig(), cluster, 0, false);

    mmap_base_addr_ =
        (char *)base::PMMmapRegisterCenter::GetInstance()->Register(
            "/media/aep0/perf_sgl-placeholder/value",
            FLAGS_key_space_m * 1024 * 1024LL * FLAGS_value_size);

    pm_address_for_check_ =
        base::PMMmapRegisterCenter::GetInstance()->ForRDMAMemoryRegion();

    config.baseAddr = pm_address_for_check_.first;
    config.dsmSize = pm_address_for_check_.second;

    LOG(INFO) << "register MR start =" << (void *)config.baseAddr
              << ", end = " << (void *)(config.baseAddr + config.dsmSize)
              << ", size = " << config.dsmSize;

    if (FLAGS_preload) {
      LOG(INFO) << "preload whole DB";
      InitValue();
      LOG(INFO) << "preload whole DB done";
      exit(0);
    }

    dsm_ = DSM::getInstance(config);
    LOG(INFO) << "xmh: finish construct DSM";
    sourcelists_.resize(thread_count);
    for (int i = 0; i < thread_count; i++) {
      sourcelists_[i].resize(FLAGS_max_kv_num_per_request);
    }
  }

  void Main() {
    for (int i = 0; i < thread_count_; i++) {
      LOG(INFO) << "Starts PS polling thread " << i;
      threads_.emplace_back(&Server::PollingThread, this, i);
    }
    while (1)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }

private:
  void InitValue() {
    int64_t key_space = FLAGS_key_space_m * 1024 * 1024LL;
#pragma omp parallel for num_threads(36)
    for (int64_t i = 0; i < key_space; i++) {
      for (int j = 0; j < FLAGS_value_size / sizeof(float); j++) {
        *(float *)&mmap_base_addr_[i * FLAGS_value_size + j * sizeof(float)] =
            i;
      }
    }
  }
  void RpcGetServerServingThreadIDs(RawMessage *recv) {
    CHECK_EQ(recv->type, GET_SERVER_THREADIDS);
    static int serving_thread_id = 0;
    auto m = RawMessage::get_new_msg();
    m->type = RESP_GET_SERVER_THREADIDS;
    std::vector<int> thread_ids;
    thread_ids.push_back(serving_thread_id);
    serving_thread_id = (serving_thread_id + 1) % thread_count_;
    thread_ids.push_back(serving_thread_id);
    serving_thread_id = (serving_thread_id + 1) % thread_count_;
    dsm_->rpc_call(
        m, recv->node_id, recv->t_id,
        Slice((char *)thread_ids.data(), thread_ids.size() * sizeof(int)));
  }

  void PollingThread(int thread_id) {
    auto_bind_core();
    dsm_->registerThread();
    auto msg = RawMessage::get_new_msg();
    auto &sourcelist = sourcelists_[thread_id];
    while (1) {
      msg->clear();
      uint64_t wr_id;
      auto recv = dsm_->rpc_fast_wait(&wr_id);
      if (!recv) {
        continue;
      }

      if (recv->type == GET_SERVER_THREADIDS) {
        RpcGetServerServingThreadIDs(recv);
        continue;
      }

      if (thread_id == 0)
        get_parameter_timer_.start();
      {
        // CHECK(0);
        Cursor cursor;
        Slice extra_data = recv->get_string(cursor);

        int batch_get_kv_count = extra_data.len / sizeof(uint64_t);
        base::ConstArray<uint64_t> keys((uint64_t *)extra_data.s,
                                        batch_get_kv_count);
#ifdef RPC_DEBUG
        LOG(INFO) << "recv->msg_size=" << extra_data.len;
        LOG(INFO) << "server batch gets: " << keys.Debug();
#endif
        CHECK_LE(batch_get_kv_count, FLAGS_max_kv_num_per_request);
        std::vector<base::ConstArray<float>> values;
        for (int i = 0; i < keys.Size(); i++) {
          // generate fake PM read
          for (int _ = 0; _ < FLAGS_fake_pm_read_times; _++) {
            uint64_t mmap_range_size = pm_address_for_check_.second;
            ReadUint64(
                (uint64_t *)(GetHash(base::AsyncTimeHelper::GetTimestamp()) %
                                 (mmap_range_size - 8) +
                             mmap_base_addr_));
          }
          // generate fake PM read done
          values.emplace_back(
              (float *)(keys[i] * FLAGS_value_size + mmap_base_addr_),
              FLAGS_value_size / sizeof(float));
        }
        CHECK_EQ(values.size(), batch_get_kv_count);
#ifdef RPC_DEBUG
        int emb_dim = FLAGS_value_size / sizeof(float);
        for (int i = 0; i < batch_get_kv_count; i++) {
          XDebug::AssertTensorEq(values[i].Data(), emb_dim, keys[i],
                                 "server embedding check error");
        }
#endif
        if (thread_id == 0)
          value_timer_.start();
        if (FLAGS_use_sglist) {
          for (int i = 0; i < batch_get_kv_count; i++) {
            sourcelist[i].addr = values[i].binary_data();
            sourcelist[i].size = values[i].binary_size();
#ifdef RPC_DEBUG
            CHECK_GE((uint64_t)sourcelist[i].addr, pm_address_for_check_.first);
            CHECK_LT((uint64_t)sourcelist[i].addr + sourcelist[i].size,
                     pm_address_for_check_.first +
                         pm_address_for_check_.second);
            CHECK_EQ(sourcelist[i].size, FLAGS_value_size);
#endif
          }

          GlobalAddress gaddr = recv->receive_gaddr;
          CHECK_EQ(gaddr.nodeID, 1) << "now we have only 1 client";
          CHECK(dsm_->write_from_pm_vec(sourcelist.data(), batch_get_kv_count,
                                        gaddr, 1, FLAGS_sge_per_wr));
        } else {
          auto buf = dsm_->get_rdma_buffer();
          int acc = 0;
          for (int i = 0; i < batch_get_kv_count; i++) {
            memcpy(buf + acc, values[i].binary_data(), values[i].binary_size());
            acc += values[i].binary_size();
          }
          GlobalAddress gaddr = recv->receive_gaddr;
          CHECK_EQ(gaddr.nodeID, 1) << "now we have only 1 client";
          dsm_->write(buf, gaddr, acc, true);
        }

        if (thread_id == 0)
          value_timer_.end();
#ifdef RPC_DEBUG
        LOG(ERROR) << "write_from_pm_vec done";
#endif
      }
      if (thread_id == 0)
        get_parameter_timer_.end();
    }
  }

  DSM *dsm_;
  std::vector<std::vector<SourceList>> sourcelists_;
  std::vector<std::thread> threads_;
  int thread_count_;
  xmh::Timer get_parameter_timer_;
  xmh::Timer value_timer_;
  std::pair<uint64_t, uint64_t> pm_address_for_check_;
  char *mmap_base_addr_;
};

int main(int argc, char *argv[]) {
  folly::init(&argc, &argv);
  xmh::Reporter::StartReportThread();

  base::PMMmapRegisterCenter::GetConfig().use_dram = FLAGS_use_dram;
  base::PMMmapRegisterCenter::GetConfig().numa_id = 0;

  if (FLAGS_actor == "server") {
    system(
        "bash "
        "/home/xieminhui/petps/third_party/Mayfly-main/script/restartMemc.sh");
    Server server(FLAGS_thread_num);
    server.Main();
  } else if (FLAGS_actor == "client") {
    Client client(FLAGS_thread_num);
    client.Main();
  } else {
    CHECK(0) << FLAGS_actor;
  }
}