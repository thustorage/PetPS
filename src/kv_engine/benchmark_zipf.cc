#include "base/zipf.h"
#include "base/factory.h"
#include "kv_engine/base_kv.h"

#include <atomic>
#include <folly/GLog.h>
#include <folly/init/Init.h>
#include <memory>
#include <stdlib.h>
#include <thread>
#include <time.h>
#include <unistd.h>

DEFINE_string(db, "", "");
DEFINE_int32(value_size, 32 * 4, "");
DEFINE_int32(read_ratio, 50, "");
DEFINE_int32(thread_count, 1, "");
DEFINE_double(zipf_theta, 0.99, "");
DEFINE_int32(running_seconds, 100, "");

const uint64_t MB = 1024 * 1024LL;

uint64_t kKeySpace = 100 * MB;
double warmup_ratio = 0.1;

const int kMaxThread = 32;
std::thread th[kMaxThread];
uint64_t tp[kMaxThread][8];

BaseKV *kv;

inline void bindCore(uint16_t core) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    assert(false);
  }
}

static __inline__ unsigned long long rdtsc(void) {
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

std::atomic<bool> stop_flag;

void thread_run(int id) {
  bindCore(id);
  unsigned int seed = rdtsc();
  struct zipf_gen_state state;
  LOG(INFO) << "before mehcached_zipf_init";
  mehcached_zipf_init(&state, kKeySpace, FLAGS_zipf_theta,
                      (rdtsc() & 0x0000ffffffffffffull) ^ id);
  LOG(INFO) << "after mehcached_zipf_init";

  char *pool = (char *)malloc(32 * 1024);
  memset(pool, 'e', 32 * 1024);

  LOG(INFO) << "before warmup";
  for (int i = 0; i < kKeySpace * warmup_ratio / FLAGS_thread_count; ++i) {
    uint64_t key = mehcached_zipf_next(&state);
    kv->Put(key, std::string_view(pool, FLAGS_value_size), id);
  }
  LOG(INFO) << "after warmup";

  std::string value;
  while (!stop_flag) {
    uint64_t key = mehcached_zipf_next(&state);
    if (rand_r(&seed) % 100 < FLAGS_read_ratio) {  // GET
      kv->Get(key, value, id);
    } else {
      kv->Put(key, std::string_view(pool, FLAGS_value_size), id);
    }
    tp[id][0]++;
  }
}

int main(int argc, char *argv[]) {
  folly::init(&argc, &argv);
  BaseKVConfig config;
  config.capacity = kKeySpace;
  config.value_size = FLAGS_value_size;
  kv = base::Factory<BaseKV, const BaseKVConfig &>::NewInstance(FLAGS_db, config);
  for (int i = 0; i < FLAGS_thread_count; i++) {
    th[i] = std::thread(thread_run, i);
  }

  timespec s, e;
  uint64_t pre_tp = 0;

  uint64_t all_tp = 0;
  while (all_tp == 0) {
    all_tp = 0;
    for (int i = 0; i < FLAGS_thread_count; ++i) {
      all_tp += tp[i][0];
    }
    sleep(1);
    LOG(INFO) << "waiting warm up";
  }

  int running_seconds = 0;
  while (true) {
    clock_gettime(CLOCK_REALTIME, &s);
    sleep(1);
    clock_gettime(CLOCK_REALTIME, &e);
    int microseconds =
        (e.tv_sec - s.tv_sec) * 1000000 + (double)(e.tv_nsec - s.tv_nsec) / 1000;

    all_tp = 0;
    for (int i = 0; i < FLAGS_thread_count; ++i) {
      all_tp += tp[i][0];
    }
    uint64_t cap = all_tp - pre_tp;
    pre_tp = all_tp;

    printf("throughput %.4f\n", cap * 1.0 / microseconds);
    running_seconds++;
    if (running_seconds == FLAGS_running_seconds) {
      stop_flag = true;
      for (int i = 0; i < FLAGS_thread_count; ++i) {
        th[i].join();
      }
      break;
    }
  }

  return 0;
}