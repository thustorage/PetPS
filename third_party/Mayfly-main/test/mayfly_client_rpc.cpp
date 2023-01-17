#include "DSM.h"
#include "KVClient.h"
#include "Rdma.h"
#include "Timer.h"
#include "murmur_hash2.h"

#include "zipf.h"

#define ENABLE_LATENCY_TEST
// #define MS_2_TP_STATS

const int kLoadThread = 35;

struct ZippyDB {

  ZippyDB() { seed = asm_rdtsc(); }

  static const int kKeySize = 8;
  static const int kAvgObjSize = 91;
  static const int kMaxObjSize = 512;
  uint64_t next_value_size() {

    // return 512 - 27;
    int r = rand_r(&seed);
    if (r % 2 == 1) { // 50%
      return kAvgObjSize - kKeySize;
    }
    if (r % 100 < 84) {
      return (r >> 7) % (kAvgObjSize - kKeySize);
    } else {
      return (kAvgObjSize - kKeySize) + (r >> 7) % (kMaxObjSize - kAvgObjSize);
    }
  }

  uint32_t seed;
};

struct UP2X {

  UP2X() { seed = asm_rdtsc(); }

  static const int kKeySize = 8;
  static const int kAvgObjSize = 57;
  static const int kMaxObjSize = 512;
  uint64_t next_value_size() {
    int r = rand_r(&seed);
    if (r % 2 == 1) { // 50%
      return kAvgObjSize - kKeySize;
    }
    if (r % 100 < 90) {
      return (r >> 7) % (kAvgObjSize - kKeySize);
    } else {
      return (kAvgObjSize - kKeySize) + (r >> 7) % (kMaxObjSize - kAvgObjSize);
    }
  }

  uint32_t seed;
};

struct UDB {

  UDB() { seed = asm_rdtsc(); }

  static const int kKeySize = 8;
  static const int kAvgObjSize = 153;
  static const int kMaxObjSize = 512;
  uint64_t next_value_size() {
    int r = rand_r(&seed);
    if (r % 2 == 1) { // 50%
      return kAvgObjSize - kKeySize;
    }
    if (r % 100 < 71) {
      return (r >> 7) % (kAvgObjSize - kKeySize);
    } else {
      return (kAvgObjSize - kKeySize) + (r >> 7) % (kMaxObjSize - kAvgObjSize);
    }
  }

  uint32_t seed;
};

uint16_t server_thread_nr;
uint32_t client_thread_nr;
uint32_t server_nr;
uint32_t client_nr;
DSM *dsm;

const int kLatencyInterval = 10000;
uint64_t tp_counter[64][8];
uint64_t *global_latency_buckets_read[48];
uint64_t *global_latency_buckets_write[48];
uint64_t final_latency_bucket_read[kLatencyInterval];
uint64_t final_latency_bucket_write[kLatencyInterval];

// workloads parameter
const int write_ratio = 5;
const int kWindowSize = 4;
const uint64_t kSpace = 200 * define::MB;
const double zipfan_arg = 0;

struct LatencyRecord {
  uint64_t start;
  uint64_t end;
  bool is_write;

  uint64_t key;
};

void print_latency(uint64_t *latency_buckets) {

  uint64_t all_lat = 0;

  for (size_t i = 0; i < kLatencyInterval; ++i) {
    all_lat += latency_buckets[i];
  }

  uint64_t th50 = all_lat / 2;
  uint64_t th90 = all_lat * 9 / 10;
  uint64_t th95 = all_lat * 95 / 100;
  uint64_t th99 = all_lat * 99 / 100;
  uint64_t th999 = all_lat * 999 / 1000;

  uint64_t cum = 0;
  for (int i = 0; i < kLatencyInterval; ++i) {
    cum += latency_buckets[i];

    if (cum >= th50) {
      printf("p50 %f\t", i / 10.0);
      th50 = -1;
    }
    if (cum >= th90) {
      printf("p90 %f\t", i / 10.0);
      th90 = -1;
    }
    if (cum >= th95) {
      printf("p95 %f\t", i / 10.0);
      th95 = -1;
    }
    if (cum >= th99) {
      printf("p99 %f\t", i / 10.0);
      th99 = -1;
    }
    if (cum >= th999) {
      printf("p999 %f\n", i / 10.0);
      th999 = -1;
      return;
    }
  }
}

std::atomic<int> load_counter{0};
void test(int thread_id) {
  dsm->registerThread();

  // bind_core(core_table[global_socket_id][thread_id]);

  // UP2X workload;
  ZippyDB workload;

  auto latency_buckets_read = global_latency_buckets_read[thread_id];
  auto latency_buckets_write = global_latency_buckets_write[thread_id];

  auto client = new kv::KVClient(dsm, server_thread_nr);

  char *buf = dsm->get_rdma_buffer();

  *(buf) = 1;
  RawMessage *m = RawMessage::get_new_msg();
  (void)m;

  Slice get_value;
  char *put_buf = (char *)malloc(4096);

  // load
  uint64_t id = (dsm->getMyNodeID() - server_nr) * kLoadThread + thread_id;
  uint64_t all_id = kLoadThread * client_nr;
  uint64_t per_thread_load = kSpace / all_id;

  const int kLoadWindow = 4;
  for (int i = 0; i < kLoadWindow; ++i) {
    uint64_t key = i + id * 1024;
    auto slice_key = Slice((char *)&key, sizeof(uint64_t));
    client->put_async(slice_key, Slice(put_buf, workload.next_value_size()), i);
  }

  // printf("from %ld, %ld\n", per_thread_load * id, per_thread_load);
  for (uint64_t i = 0; i < per_thread_load; ++i) {
    uint64_t key = per_thread_load * id + i;
    key = MurmurHash64A(&key, sizeof(key));
    auto slice_key = Slice((char *)&key, sizeof(uint64_t));
    client->sync_once();
    client->put_async(slice_key, Slice(put_buf, workload.next_value_size()));

    // if (i % 1000000 == 0) {
    //   printf("%lu\n", i);
    // }
  }
  for (int i = 0; i < kLoadWindow; ++i) {
    client->sync_once();
  }

  if (thread_id == 0) {
    sleep(10);
#ifdef MIGRATION_TEST
    generate_shard_0();
#endif
    dsm->barrier("client-load", client_nr);
  }

  load_counter.fetch_add(1);

  while (load_counter.load() != kLoadThread) {
    /* code */
  }

  if (thread_id == 0) {
    printf("finish load\n");
    fflush(stdout);
  }
  if ((uint32_t)thread_id >= client_thread_nr) {
    return;
  }

  uint64_t cur = 0;

  LatencyRecord lat[kWindowSize];

  uint32_t seed = asm_rdtsc();
  uint64_t key;
  for (int i = 0; i < kWindowSize; ++i) {
    key = rand_r(&seed) % kSpace;
    auto slice_key = Slice((char *)&key, sizeof(uint64_t));

#ifdef ENABLE_LATENCY_TEST
    lat[i].start = Timer::get_time_ns();
    lat[i].is_write = write_ratio != 0;
#endif
    client->put_async(slice_key, Slice(put_buf, workload.next_value_size()), i);
  }

  zipf_gen_state state;
  mehcached_zipf_init(&state, kSpace, zipfan_arg, seed);

  while (true) {
    bool old_key = false;

    auto resp = client->sync_once();
    auto tag = resp->rpc_tag;

    tp_counter[thread_id][0]++;

#ifdef ENABLE_LATENCY_TEST
    lat[tag].end = Timer::get_time_ns();
    auto gap = (lat[tag].end - lat[tag].start) / 100;

    if (gap >= kLatencyInterval) {
      if (lat[tag].is_write) {
        latency_buckets_write[kLatencyInterval - 1]++;
      } else {
        latency_buckets_read[kLatencyInterval - 1]++;
      }

    } else {
      if (lat[tag].is_write) {
        latency_buckets_write[gap]++;
      } else {
        latency_buckets_read[gap]++;
      }
    }
#endif

    cur++;

    if (old_key) {
      key = lat[tag].key;
    } else {
      key = mehcached_zipf_next(&state) % kSpace;
      key = MurmurHash64A(&key, sizeof(key));
    }

    auto slice_key = Slice((char *)&key, sizeof(uint64_t));
    // dsm->write_sync(buf, gaddr, 64);
    // client->get(Slice(key.c_str(), key.size()), get_value);

    Slice get_buf;

    bool is_write = rand_r(&seed) % 100 < write_ratio;

#ifdef ENABLE_LATENCY_TEST
    lat[tag].start = Timer::get_time_ns();
    lat[tag].is_write = is_write;
    lat[tag].key = key;
#endif

    if (!is_write) {
      client->get_async(slice_key, get_buf, tag);
    } else {
      client->put_async(slice_key, Slice(put_buf, workload.next_value_size()),
                        tag);
    }
  }
}

// ./client server_nr client_nr server_thread_nr client_thread_nr
int main(int argc, char **argv) {

  if (argc != 5 && argc != 6) {
    fprintf(stderr, "Usage: ./client server_nr client_nr server_thread_nr "
                    "client_thread_nr\n");
    exit(-1);
  }

  server_nr = std::atoi(argv[1]);
  client_nr = std::atoi(argv[2]);
  server_thread_nr = std::atoi(argv[3]);
  client_thread_nr = std::atoi(argv[4]);

  if (argc == 6) {
    global_socket_id = std::atoi(argv[5]);
  }

  ClusterInfo cluster;
  cluster.serverNR = server_nr;
  cluster.clientNR = client_nr;

  for (size_t i = 0; i < client_thread_nr; ++i) {
    global_latency_buckets_read[i] = new uint64_t[kLatencyInterval];
    memset(global_latency_buckets_read[i], 0,
           sizeof(uint64_t) * kLatencyInterval);

    global_latency_buckets_write[i] = new uint64_t[kLatencyInterval];
    memset(global_latency_buckets_write[i], 0,
           sizeof(uint64_t) * kLatencyInterval);
  }

  DSMConfig config(CacheConfig(), cluster, 1, true);
  dsm = DSM::getInstance(config);

  for (size_t i = 0; i < kLoadThread; ++i) {
    new std::thread(test, i);
  }

  while (load_counter.load() != kLoadThread) {
    /* code */
  }

  Timer timer;

  uint64_t pre_tp = 0;

  timer.begin();

  int second = 0;
  while (true) {

#ifdef MS_2_TP_STATS

    uint64_t bench_begin = Timer::get_time_ns();

    uint64_t pre_time = Timer::get_time_ns();

    std::vector<float> result_vec;

    while (true) {
      Timer::sleep(10 * define::ns2ms);

      uint64_t cur_time = Timer::get_time_ns();

      uint64_t ns = cur_time - pre_time;

      uint64_t tp = 0;
      for (size_t i = 0; i < 40; ++i) {
        tp += tp_counter[i][0];
      }
      pre_time = cur_time;

      double t_tp = (tp - pre_tp) * 1.0 / (1.0 * ns / 1000 / 1000 / 1000);

      if (tp <= pre_tp) {
        t_tp = 0;
      }
      pre_tp = tp;

      result_vec.push_back(t_tp * 8 / 1000 / 1000);

      if (cur_time - bench_begin >= 30 * define::ns2s) {
        if (dsm->getMyNodeID() == dsm->get_conf()->machineNR - 1) {
          freopen("./wq.res", "w", stdout);
          for (auto v : result_vec) {
            printf("%lf\n", v);
          }
        }
        exit(0);
      }
#ifdef MIGRATION_TEST
      else if (cur_time - bench_begin >= 20 * define::ns2s &&
               kv::is_hotspot_shift == false) {
        kv::is_hotspot_shift.store(true);
      }
#endif
    }

#endif

    // sleep(1024);

    sleep(1);
    second++;

#ifdef MIGRATION_TEST
    if (second == 20) {
      kv::is_hotspot_shift.store(true);
    }
#endif

#ifdef ENABLE_LATENCY_TEST
    if (second > 40) {

      for (size_t i = 0; i < kLatencyInterval; ++i) {
        for (size_t k = 0; k < client_thread_nr; ++k) {
          final_latency_bucket_read[i] += global_latency_buckets_read[k][i];
          final_latency_bucket_write[i] += global_latency_buckets_write[k][i];
        }
      }

      print_latency(final_latency_bucket_read);
      print_latency(final_latency_bucket_write);

      exit(0);
    }
#endif

    auto ns = timer.end();

    uint64_t tp = 0;

    for (size_t i = 0; i < 64; ++i) {
      tp += tp_counter[i][0];
    }

    double t_tp = (tp - pre_tp) * 1.0 / (1.0 * ns / 1000 / 1000 / 1000);
    pre_tp = tp;

    timer.begin();
    printf("IOPS %lf\n", t_tp);
    for (int i = 0; i < 35; ++i) {
      printf("%ld\t", tp_counter[i][0]);
    }
    printf("\n");
  }

  while (true) {

    /* code */
  }
}