#pragma once

#include <folly/init/Init.h>

#include "base/factory.h"
#include "base/timer.h"
#include "ps/Postoffice.h"
#include "ps/allshards_ps_client.h"
#include "ps/base_client.h"
#include "sample_reader.h"
#include "third_party/Mayfly-main/include/Common.h"

DEFINE_bool(thread_cut_off, false, "thread cut off");
DEFINE_int32(benchmark_seconds, 120, "benchmark seconds");

class BenchmarkClientCommon {
 public:
  struct BenchmarkClientCommonArgs {
    int thread_count_ = 0;
    int async_req_num_ = 0;
    int batch_read_count_ = 0;
    int64_t key_space_M_ = 0;
    double zipf_theta_ = -1;
    int value_size_ = 0;
    int read_ratio_ = 100;
    std::string dataset_;

    void CheckArgs() {
      CHECK_NE(thread_count_, 0);
      CHECK_NE(async_req_num_, 0);
      CHECK_NE(batch_read_count_, 0);
      CHECK_NE(key_space_M_, 0);
      CHECK(!(std::fabs(zipf_theta_ + 1) < 1e-6));
      CHECK_NE(value_size_, 0);
      CHECK_GE(read_ratio_, 0);
      CHECK_LE(read_ratio_, 100);
    }
  };

  BenchmarkClientCommon(BenchmarkClientCommonArgs args)
      : args_(args), stop_flags_(args.thread_count_) {
    args_.CheckArgs();
    for (auto &each : stop_flags_) {
      each.store(false);
    }
    pause_flags_ = false;
    for (int i = 0; i < kMaxThread; i++) {
      tp[i][0] = -1;
    }
    start_flag_ = false;
  }

  void Run() {
    std::vector<std::unique_ptr<BaseParameterClient>> clients;
    CHECK_LE(args_.thread_count_, kMaxThread);
    for (int _ = 0; _ < args_.thread_count_; _++) {
      BaseParameterClient *client;
      if (XPostoffice::GetInstance()->NumServers() == 1) {
        // client = base::Factory<BaseParameterClient, const std::string &,
        // int,
        //                             int>::NewInstance("LJRPCParameterClient",
        //                                               "127.0.0.1", 1234, 0);

        client = base::Factory<BaseParameterClient, const std::string &, int,
                               int>::NewInstance("WqRPCParameterClient",
                                                 "127.0.0.1", 1234, 0);

        // client = base::Factory<BaseParameterClient, const std::string &,
        // int,
        //                             int>::NewInstance("PsLiteParameterClient",
        //                                               "127.0.0.1", 1234, 0);
      } else {
        std::vector<BaseParameterClient *> shard_clients;
        for (int shard = 0; shard < XPostoffice::GetInstance()->NumServers();
             shard++) {
          auto shard_client =
              base::Factory<BaseParameterClient, const std::string &, int,
                            int>::NewInstance("WqRPCParameterClient",
                                              "127.0.0.1", 1234, shard);
          shard_clients.push_back(shard_client);
        }
        client = new AllShardsParameterClientWrapper(
            shard_clients, XPostoffice::GetInstance()->NumServers());
      }
      clients.emplace_back(client);
    }
    PetDatasetReader *dataset_reader = nullptr;
    if (args_.dataset_ == "dataset") {
      dataset_reader = new PetDatasetReader(
          args_.thread_count_, args_.key_space_M_,
          "/data/project/kuai/dump.2022.08.17/sign*", true);
    }

    std::vector<std::unique_ptr<SampleReader>> sample_readers;
    for (int _ = 0; _ < args_.thread_count_; _++) {
      SampleReader *each;
      if (args_.dataset_ == "zipfian")
        each = new ZipfianSampleReader(_, args_.key_space_M_, args_.zipf_theta_,
                                       args_.batch_read_count_);
      else if (args_.dataset_ == "dataset") {
        LOG(FATAL) << "We can not make the production dataset public.";
        each = new PetDatasetSampleReader(dataset_reader, _, args_.key_space_M_,
                                          args_.batch_read_count_);
      } else {
        LOG(FATAL) << "dataset_ = " << args_.dataset_;
      }
      sample_readers.emplace_back(each);
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < args_.thread_count_; i++) {
      threads.push_back(std::thread(&BenchmarkClientCommon::clientThreadLoop,
                                    this, i, sample_readers[i].get(),
                                    clients[i].get()));
    }

    timespec s, e;
    uint64_t pre_tp = 0;

    int running_seconds = 0;

    std::vector<int> stop_thread_id_orders;

    for (int i = 0; i < args_.thread_count_; i++)
      stop_thread_id_orders.push_back(i);

    // wait all threads in client ready
    for (int i = 0; i < args_.thread_count_; i++) {
      while (tp[i][0] != 0)
        FB_LOG_EVERY_MS(WARNING, 5000)
            << "main client thread, stalled for waiting thread " << i;
    }
    // wait all clients ready
    clients[0]->Barrier("clients start",
                        XPostoffice::GetInstance()->NumClients());
    start_flag_ = true;

    while (true) {
      clock_gettime(CLOCK_REALTIME, &s);
      sleep(1);
      clock_gettime(CLOCK_REALTIME, &e);
      int microseconds = (e.tv_sec - s.tv_sec) * 1000000 +
                         (double)(e.tv_nsec - s.tv_nsec) / 1000;

      uint64_t all_tp = 0;
      for (int i = 0; i < args_.thread_count_; ++i) {
        all_tp += tp[i][0];
      }
      uint64_t cap = all_tp - pre_tp;
      pre_tp = all_tp;

      printf("throughput %.4f Mreq/s %.4f Mkv/s\n", cap * 1.0 / microseconds,
             cap * (double)args_.batch_read_count_ / microseconds);
      running_seconds++;

      if (FLAGS_thread_cut_off && running_seconds % 30 == 0) {
        const int stride = 2;
        for (int i = 0; i < stride; i++) {
          int stop_tid = stop_thread_id_orders.back();
          stop_thread_id_orders.pop_back();
          stop_flags_[stop_tid] = true;
          if (stop_thread_id_orders.size() == 0) {
            goto label_finish;
          }
        }
        // pause all running threads
        pause_flags_ = true;
        LOG(INFO) << folly::sformat("Stop {} threads; remaining {} threads",
                                    stride, stop_thread_id_orders.size());
        sleep(1);
        // wait for all client processes;
        clients[0]->Barrier(
            folly::sformat("clients pause {}", stop_thread_id_orders.size()),
            XPostoffice::GetInstance()->NumClients());
        // clean Timer
        xmh::Reporter::Init4GoogleTest();
        xmh::Reporter::StartReportThread();
        for (int i = 0; i < args_.thread_count_; ++i) {
          tp[i][0] = 0;
        }
        pre_tp = 0;
        // continue all running threads
        pause_flags_ = false;
      }

      if (!FLAGS_thread_cut_off && running_seconds >= FLAGS_benchmark_seconds) {
        LOG(INFO) << "already run for a while; stop benchmark";
        for (auto &each : stop_flags_) {
          each = true;
        }
        goto label_finish;
      }
    }
  label_finish:
    LOG(INFO) << "All client threads stopped";
    for (auto &t : threads) t.join();
  }

 private:
  void clientThreadLoop(int tid, SampleReader *sample,
                        BaseParameterClient *client) {
    auto_bind_core(1);
    std::vector<uint64_t> client_keys(args_.batch_read_count_ *
                                      args_.async_req_num_);
    std::vector<float *> recv_buffers;
    for (int i = 0; i < args_.async_req_num_; i++) {
      recv_buffers.push_back((float *)client->GetReceiveBuffer(
          args_.batch_read_count_ * args_.value_size_));
    }

    client->InitThread();

    std::vector<int> running_rpc_ids(args_.async_req_num_, -1);
    std::vector<bool> isPullRequest(args_.async_req_num_, true);
    xmh::Timer client_get_timer("client get", 1);
    xmh::Timer client_put_timer("client put", 1);

    // mark this thread ready
    tp[tid][0] = 0;
    LOG(INFO) << "client thread " << tid << "th ready";
    while (!start_flag_)
      ;

    while (!stop_flags_[tid]) {
      while (pause_flags_) {
        client_get_timer.destroy();
      }
      for (int req_i = 0; req_i < args_.async_req_num_; req_i++) {
        if (running_rpc_ids[req_i] == -1) {
          // send a request
          auto keys =
              sample->fillArray(&client_keys[req_i * args_.batch_read_count_]);
          float *values = recv_buffers[req_i];

          if (folly::Random::rand32(100) < args_.read_ratio_) {
            // read request
            isPullRequest[req_i] = true;
            running_rpc_ids[req_i] = client->GetParameter(
                keys.ToConstArray(), values, true, true, req_i);
            if (req_i == 0) client_get_timer.start();
          } else {
            isPullRequest[req_i] = false;
            running_rpc_ids[req_i] =
                client->FakePutParameter(keys.ToConstArray(), values);
            if (req_i == 0) client_put_timer.start();
          }
        } else {
          if (client->QueryRPCFinished(running_rpc_ids[req_i])) {
            tp[tid][0] += 1;
            if (req_i == 0) {
              if (isPullRequest[req_i])
                client_get_timer.end();
              else
                client_put_timer.end();
            }
#ifdef RPC_DEBUG
            base::ConstArray<uint64_t> keys(
                &client_keys[req_i * args_.batch_read_count_],
                args_.batch_read_count_);
            int emb_dim = args_.value_size_ / sizeof(float);
            float *values = recv_buffers[req_i];
            CheckEmbDebug(emb_dim, keys, values);
            FB_LOG_EVERY_MS(ERROR, 2000)
                << "successfully ------------------------------";
#endif
            client->RevokeRPCResource(running_rpc_ids[req_i]);
            running_rpc_ids[req_i] = -1;
          }
        }
      }
    }
    LOG(INFO) << "client thread " << tid << "th exit";
  }

  void CheckEmbDebug(int emb_dim, base::ConstArray<uint64_t> keys,
                     const float *values) {
    for (int i = 0; i < keys.Size(); i++) {
      XDebug::AssertTensorEq(
          &values[i * emb_dim], emb_dim, keys[i],
          folly::sformat("client embedding check error, key={}", keys[i]));
    }
  }

 private:
  BenchmarkClientCommonArgs args_;

  std::vector<std::atomic<bool>> stop_flags_;
  std::atomic<bool> pause_flags_;
  std::atomic<bool> start_flag_;

  constexpr static int kMaxThread = 64;
  uint64_t tp[kMaxThread][8];
};