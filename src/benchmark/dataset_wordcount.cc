#include "base/base.h"
#include "base/glob.h"
#include "folly/GLog.h"
#include "folly/system/MemoryMapping.h"
#include "parse_dataset.h"

#include <string>
#include <unordered_map>
#include <vector>

DEFINE_string(dataset_file_str, "/data/project/kuai/dump.2022.08.17.origin/*",
              "");
DEFINE_string(output_file, "", "");
DEFINE_int32(thread_count, 8, "");

// ID to counter
std::unordered_map<uint64_t, uint64_t>
CountIDOccurrence(const std::string &dataset_file) {
  std::unordered_map<uint64_t, uint64_t> counter;
  auto file_mmap = folly::MemoryMapping(dataset_file.c_str());
  PetCursor cursor((char *)file_mmap.range().begin(),
                   (char *)file_mmap.range().end());

  auto nr_request = cursor.ReadInt();
  for (int64_t i = 0; i < nr_request; i++) {
    auto nr_keys_in_one_request = cursor.ReadInt();
    for (int j = 0; j < nr_keys_in_one_request; j++) {
      uint64 key = cursor.ReadUint64();
      int dim = cursor.ReadInt();
      CHECK(4 <= dim && dim <= 64) << dim;
      CHECK(dim == 4 || dim == 8 || dim == 16 || dim == 32 || dim == 64) << dim;
      if (counter.find(key) == counter.end()) {
        counter[key] = 1;
      } else {
        counter[key] += 1;
      }
    }
  }
  return counter;
}

void MergeCounter(std::unordered_map<uint64_t, uint64_t> &a,
                  const std::unordered_map<uint64_t, uint64_t> &b) {
  for (auto p : b) {
    if (a.find(p.first) == a.end()) {
      a[p.first] = p.second;
    } else {
      a[p.first] += p.second;
    }
  }
}

int main(int argc, char **argv) {
  folly::Init(&argc, &argv);
  CHECK_NE(FLAGS_output_file, "");

  std::vector<std::string> dataset_files;
  for (auto &p : glob::glob(FLAGS_dataset_file_str)) {
    dataset_files.push_back(p);
  }

  int nr_dataset_files = dataset_files.size();
  CHECK_NE(nr_dataset_files, 0);

  std::vector<std::thread> th(FLAGS_thread_count);

  int file_num_per_thread =
      (nr_dataset_files + FLAGS_thread_count - 1) / FLAGS_thread_count;

  std::vector<std::unordered_map<uint64_t, uint64_t> *> per_thread_counter;
  for (int i = 0; i < FLAGS_thread_count; i++) {
    per_thread_counter.push_back(new std::unordered_map<uint64_t, uint64_t>());
  }

  for (int i = 0; i < FLAGS_thread_count; i++) {
    th[i] = std::thread([i, file_num_per_thread, &dataset_files,
                         nr_dataset_files, &per_thread_counter]() {
      for (int j = i * file_num_per_thread;
           j < std::min((i + 1) * file_num_per_thread, nr_dataset_files); j++) {
        if (i == 0)
          FB_LOG_EVERY_MS(INFO, 10000)
              << (j - i * file_num_per_thread) * 100 / file_num_per_thread
              << " %";

        auto file_counter = CountIDOccurrence(dataset_files[j]);
        MergeCounter(*per_thread_counter[i], file_counter);
      }
    });
  }
  for (int i = 0; i < FLAGS_thread_count; ++i) {
    th[i].join();
  }

  for (int i = 1; i < FLAGS_thread_count; i++) {
    MergeCounter(*per_thread_counter[0], *per_thread_counter[i]);
  }
  auto *final_counter = per_thread_counter[0];

  LOG(INFO) << "finished counter";

  folly::MemoryMapping::Options options;
  options.setWritable(true).setGrow(true);
  system(("touch " + FLAGS_output_file).c_str());
  auto file_mmap = folly::MemoryMapping(FLAGS_output_file.c_str(), 0,
                                        final_counter->size() * 16, options);
  uint64_t *data = (uint64_t *)file_mmap.range().begin();
  uint64_t offset = 0;
  for (auto p : *final_counter) {
    data[offset] = p.first;
    data[offset + 1] = p.second;
    offset += 2;
  }

  return 0;
}