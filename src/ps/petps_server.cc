#include <folly/init/Init.h>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "base/factory.h"
#include "base/timer.h"
#include "kv_engine/base_kv.h"
#include "memory/epoch_manager.h"
#include "memory/shm_file.h"

#include "load_db.h"
#include "mayfly_config.h"
#include "petps_magic.h"
#include "third_party/Mayfly-main/include/DSM.h"

DEFINE_string(db, "KVEnginePersistDoubleShmKV", "");
DEFINE_int64(key_space_m, 100, "key space in million");
DEFINE_double(warmup_ratio, 0.8,
              "bulk load (warmup_ratio * key_space) kvs in DB");
DEFINE_int32(warmup_thread_num, 36, "");
DEFINE_int32(thread_num, 1, "");
DEFINE_bool(use_sglist, true, "");
DEFINE_bool(preload, false, "");
DEFINE_bool(check_all_inserted, false, "check DB, whether all kv are inserted");
DEFINE_bool(use_dram, false, "");
DEFINE_bool(exit, false, "");
DEFINE_int32(numa_id, 0, "");

DECLARE_int32(value_size);
DECLARE_int32(max_kv_num_per_request);

class RDMARpcParameterServiceImpl {
 public:
  RDMARpcParameterServiceImpl(BaseKV *base_kv, int thread_count)
      : base_kv_(base_kv),
        thread_count_(thread_count),
        get_parameter_timer_("GetParameter", 1),
        index_timer_("Index Part", 1),
        value_timer_("Value Part", 1),
        epoch_manager_(base::epoch::EpochManager::GetInstance()) {
    CHECK_LE(thread_count, kMaxThread);

    ClusterInfo cluster;
    cluster.serverNR = XPostoffice::GetInstance()->NumServers();
    cluster.clientNR = XPostoffice::GetInstance()->NumClients();

    DSMConfig config(CacheConfig(), cluster, 0, false);
    if (FLAGS_use_sglist) {
      pm_address_for_check_ = base_kv_->RegisterPMAddr();
      config.baseAddr = pm_address_for_check_.first;
      config.dsmSize = pm_address_for_check_.second;
      LOG(INFO) << "register PM space to RNIC";
    } else {
      config.dsmSize = 100 * define::MB;
      config.baseAddr = (uint64_t)hugePageAlloc(config.dsmSize);
      LOG(INFO) << "WE DONT register PM space to RNIC";
    }
    LOG(INFO) << "register MR start =" << (void *)config.baseAddr
              << ", end = " << (void *)(config.baseAddr + config.dsmSize)
              << ", size = " << config.dsmSize;

    config.NIC_name = '0' + FLAGS_numa_id;
    {
      dsm_ = DSM::getInstance(config, XPostoffice::GetInstance()->GlobalID());
      CHECK_EQ(dsm_->getMyNodeID(), XPostoffice::GetInstance()->GlobalID())
          << "inconsistent postoffice and wq dsm";
      LOG(INFO) << "xmh: finish construct DSM";
    }
    sourcelists_.resize(thread_count);
    for (int i = 0; i < thread_count; i++) {
      sourcelists_[i].resize(FLAGS_max_kv_num_per_request);
    }
  }

  void Start() {
    for (int i = 0; i < thread_count_; i++) {
      LOG(INFO) << "Starts PS polling thread " << i;
      threads_.emplace_back(&RDMARpcParameterServiceImpl::PollingThread, this,
                            i);
      tp[i][0] = 0;
    }
  }

  uint64_t GetThroughputCounterSum() const {
    uint64_t sum = 0;
    for (int i = 0; i < thread_count_; i++) sum += tp[i][0];
    return sum;
  }

 private:
  void RpcGetServerServingThreadIDs(RawMessage *recv) {
    CHECK_EQ(recv->type, GET_SERVER_THREADIDS);
    static std::atomic_int serving_thread_id{0};
    auto m = RawMessage::get_new_msg();
    m->type = RESP_GET_SERVER_THREADIDS;
    std::vector<int> thread_ids;
    int r = serving_thread_id.fetch_add(1);
    thread_ids.push_back(r % thread_count_);
    r = serving_thread_id.fetch_add(1);
    thread_ids.push_back(r % thread_count_);
    dsm_->rpc_call(
        m, recv->node_id, recv->t_id,
        Slice((char *)thread_ids.data(), thread_ids.size() * sizeof(int)));
  }

  void RpcPsPut(RawMessage *recv, int thread_id) {
    thread_local base::PseudoRandom random_engine;
    Cursor cursor;
    Slice extra_data = recv->get_string(cursor);
    int put_kv_count = extra_data.len / sizeof(uint64_t);
    base::ConstArray<uint64_t> keys((uint64_t *)extra_data.s, put_kv_count);
    for (int i = 0; i < keys.Size(); i++) {
      base_kv_->Put(keys[i], random_engine.GetString(FLAGS_value_size),
                    thread_id);
    }
    auto buf = dsm_->get_rdma_buffer();
    memcpy(buf, "123", 4);
    GlobalAddress gaddr = recv->receive_gaddr;
    dsm_->write(buf, gaddr, 4, true, petps::WR_ID_PUT);
  }

  void RpcPsGet(RawMessage *recv, int thread_id) {
    thread_local std::vector<base::ConstArray<float>> values;
    const bool perf_condition = (thread_id == 0);
    auto &sourcelist = sourcelists_[thread_id];

    epoch_manager_->Protect();

    if (perf_condition) get_parameter_timer_.start();
    Cursor cursor;
    Slice extra_data = recv->get_string(cursor);

    int batch_get_kv_count = extra_data.len / sizeof(uint64_t);
    tp[thread_id][0] += batch_get_kv_count;
    base::ConstArray<uint64_t> keys((uint64_t *)extra_data.s,
                                    batch_get_kv_count);
#ifdef RPC_DEBUG
    for (auto each : keys) {
      CHECK_EQ(XPostoffice::GetInstance()->ServerID(),
               ShardManager::KeyPartition(each))
          << each << " not belong to this PS; "
          << "sended from client node_id = " << (int)recv->node_id;
      ;
    }
    LOG(INFO) << "recv->msg_size=" << extra_data.len;
    LOG(INFO) << "server batch gets: " << keys.Debug();
#endif
    CHECK_LE(batch_get_kv_count, FLAGS_max_kv_num_per_request);
    values.clear();
    if (perf_condition) index_timer_.start();
    base_kv_->BatchGet(keys, &values, thread_id);
    if (perf_condition) index_timer_.end();
    CHECK_EQ(values.size(), batch_get_kv_count);
#ifdef RPC_DEBUG
    int emb_dim = FLAGS_value_size / sizeof(float);
    for (int i = 0; i < batch_get_kv_count; i++) {
      XDebug::AssertTensorEq(
          values[i].Data(), emb_dim, keys[i],
          folly::sformat("server embedding check error, key is {}", keys[i]));
    }
#endif
    if (perf_condition) value_timer_.start();
    if (FLAGS_use_sglist) {
      for (int i = 0; i < batch_get_kv_count; i++) {
        sourcelist[i].addr = values[i].binary_data();
        sourcelist[i].size = values[i].binary_size();
#ifdef RPC_DEBUG
        CHECK_GE((uint64_t)sourcelist[i].addr, pm_address_for_check_.first);
        CHECK_LT((uint64_t)sourcelist[i].addr + sourcelist[i].size,
                 pm_address_for_check_.first + pm_address_for_check_.second);
        CHECK_EQ(sourcelist[i].size, FLAGS_value_size);
#endif
      }

      GlobalAddress gaddr = recv->receive_gaddr;
      CHECK(dsm_->write_from_pm_vec(sourcelist.data(), batch_get_kv_count,
                                    gaddr, true, 30, petps::WR_ID_SG_GET));
    } else {
      auto buf = dsm_->get_rdma_buffer();
      int acc = 0;
      for (int i = 0; i < batch_get_kv_count; i++) {
        memcpy(buf + acc, values[i].binary_data(), values[i].binary_size());
        acc += values[i].binary_size();
      }
      epoch_manager_->UnProtect();
      GlobalAddress gaddr = recv->receive_gaddr;
      dsm_->write(buf, gaddr, acc, true, petps::WR_ID_GET);
    }
    if (perf_condition) value_timer_.end();

#ifdef RPC_DEBUG
    LOG(INFO) << "RPC done";
#endif
    if (perf_condition) get_parameter_timer_.end();
  }

  void PollingThread(int thread_id) {
    auto_bind_core(0);
    dsm_->registerThread();
    auto msg = RawMessage::get_new_msg();

    while (1) {
      msg->clear();
      uint64_t wr_id;
      RawMessage *recv;
      do {
        recv = dsm_->rpc_fast_wait(&wr_id);
        if (recv == nullptr && wr_id == petps::WR_ID_SG_GET) {
          // FB_LOG_EVERY_MS(ERROR, 1000)
          //     << "MaxPendingEpochNumPerThread = "
          //     << epoch_manager_->MaxPendingEpochNumPerThread();
          epoch_manager_->UnProtect();
        }
      } while (nullptr == recv);

      if (recv->type == GET_SERVER_THREADIDS) {
        LOG(INFO) << "RPC: GET_SERVER_THREADIDS received";
        RpcGetServerServingThreadIDs(recv);
      } else if (recv->type == PUT) {
        FB_LOG_EVERY_MS(WARNING, 5000) << "here is write";
        RpcPsPut(recv, thread_id);
      } else if (recv->type == GET) {
        RpcPsGet(recv, thread_id);
      } else {
        LOG(FATAL) << "unknown message type";
      }
    }
  }

 private:
  std::vector<std::vector<SourceList>> sourcelists_;
  BaseKV *base_kv_;
  std::vector<std::thread> threads_;
  int thread_count_;
  DSM *dsm_;
  xmh::Timer get_parameter_timer_;
  xmh::Timer index_timer_;
  xmh::Timer value_timer_;

  std::pair<uint64_t, uint64_t> pm_address_for_check_;

  base::epoch::EpochManager *epoch_manager_;

  constexpr static int kMaxThread = 128;
  uint64_t tp[kMaxThread][8];
};

int main(int argc, char *argv[]) {
  folly::init(&argc, &argv);
  xmh::Reporter::StartReportThread();

  BaseKVConfig config;
  std::string path = folly::sformat("/media/aep{}/", FLAGS_numa_id);

  if (FLAGS_use_dram)
    config.path = path + "dram-placeholder";
  else {
    if (FLAGS_db == "KVEnginePersistDoubleShmKV") {
      config.path = path + "double-placeholder";
    } else if (FLAGS_db == "KVEnginePersistShmKV")
      config.path = path + "kuai-placeholder";
    else if (FLAGS_db == "KVEnginePetKV") {
      config.path = path + "petkv-placeholder";
    } else if (FLAGS_db == "KVEngineDash")
      config.path = path + "dash-placeholder";
    else if (FLAGS_db == "KVEngineCLHT") {
      FLAGS_db = "HashAPI";
      config.path = path + "CLHT-placeholder";
      config.hash_name = "clht";
      config.hash_size = 40960;
      config.pool_size = 32UL * 1024 * 1024 * 1024 * 2;
    } else if (FLAGS_db == "KVEngineLevel") {
      FLAGS_db = "HashAPI";
      config.path = path + "Level-placeholder";
      config.hash_name = "level";
      config.hash_size = 40960;
      config.pool_size = 32UL * 1024 * 1024 * 1024 * 2;
    } else if (FLAGS_db == "KVEngineClevel") {
      FLAGS_db = "HashAPI";
      config.path = path + "Clevel-placeholder";
      config.hash_name = "clevel";
      config.hash_size = 40960;
      config.pool_size = 32UL * 1024 * 1024 * 1024 * 2;
      LOG(WARNING) << "for KVEngineClevel, setting warmup_thread_num = 1";
      FLAGS_warmup_thread_num = 1;
    } else if (FLAGS_db == "KVEngineCCEH") {
      FLAGS_db = "HashAPI";
      config.path = path + "CCEH-placeholder";
      config.hash_name = "cceh";
      config.hash_size = 4096;
      config.pool_size = 32UL * 1024 * 1024 * 1024 * 2;
    } else if (FLAGS_db == "KVEngineCCEHVM") {
      FLAGS_db = "HashAPI";
      config.path = path + "CCEH-placeholder";
      config.hash_name = "ccehvm";
      config.hash_size = 4096;
      config.pool_size = 32UL * 1024 * 1024 * 1024;
      LOG(WARNING) << "for KVEngineCCEHVM, setting warmup_thread_num = 1";
      FLAGS_warmup_thread_num = 1;
    } else if (FLAGS_db == "KVEngineMap") {
      config.path = path + "unorderedMap-placeholder";
      LOG(WARNING) << "for KVEngineMap, setting warmup_thread_num = 1";
      FLAGS_warmup_thread_num = 1;
    } else if (FLAGS_db == "KVEngineMapPM") {
      config.path = path + "unorderedMapPM-placeholder";
      config.pool_size = 32UL * 1024 * 1024 * 1024;
      LOG(WARNING) << "for KVEngineMapPM, setting warmup_thread_num = 1";
      FLAGS_warmup_thread_num = 1;
    } else if (FLAGS_db == "KVEngineMultiMapPM") {
      config.path = path + "KVEngineMultiMapPM-placeholder";
      config.pool_size = 100UL * 1024 * 1024 * 1024;
    } else if (FLAGS_db == "KVEngineF14") {
      config.path = path + "f14-placeholder";
      config.pool_size = 32UL * 1024 * 1024 * 1024;
    } else if (FLAGS_db == "KVEngineFakeKV") {
      config.path = path + "fake-placeholder";
    } else
      CHECK(0);
  }
  base::PMMmapRegisterCenter::GetConfig().use_dram = FLAGS_use_dram;
  base::PMMmapRegisterCenter::GetConfig().numa_id = FLAGS_numa_id;

  extern int global_socket_id;
  global_socket_id = FLAGS_numa_id;
  LOG(INFO) << "set NUMA ID = " << FLAGS_numa_id;

  config.capacity = FLAGS_key_space_m * 1024 * 1024LL /
                    XPostoffice::GetInstance()->NumServers();
  config.value_size = FLAGS_value_size;

  config.num_threads = std::max(FLAGS_thread_num, FLAGS_warmup_thread_num);

  auto kv = base::Factory<BaseKV, const BaseKVConfig &>::NewInstance(FLAGS_db,
                                                                     config);

  LoadDBHelper load_db_helper(
      kv, XPostoffice::GetInstance()->ServerID(), FLAGS_warmup_thread_num,
      FLAGS_key_space_m * 1024 * 1024LL * FLAGS_warmup_ratio);
  if (FLAGS_preload) {
    load_db_helper.PreLoadDB();
    kv->Util();
    load_db_helper.CheckDBLoad();
    kv->DebugInfo();
    if (FLAGS_exit) {
      delete kv;
      return 0;
    }
  }
  if (FLAGS_check_all_inserted) {
    load_db_helper.CheckDBLoad();
    return 0;
  }

  RDMARpcParameterServiceImpl parameterServiceImpl(kv, FLAGS_thread_num);
  parameterServiceImpl.Start();

  while (1) {
    auto micro_second1 = base::GetTimestamp();
    uint64_t tp_sum1 = parameterServiceImpl.GetThroughputCounterSum();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto micro_second2 = base::GetTimestamp();
    uint64_t tp_sum2 = parameterServiceImpl.GetThroughputCounterSum();
    double tps = (tp_sum2 - tp_sum1) * 1.0 / (micro_second2 - micro_second1);
    printf("throughput %.4f Mkv/s\n", tps);
  }
  return 0;
}
