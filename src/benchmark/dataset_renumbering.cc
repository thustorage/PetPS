#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/base.h"
#include "base/glob.h"
#include "parse_dataset.h"

#include "folly/GLog.h"
#include "folly/system/MemoryMapping.h"
#include "folly/concurrency/ConcurrentHashMap.h"
#include <oneapi/tbb/parallel_sort.h>


// input & output
DEFINE_string(dataset_file_str, "/data/project/kuai/dump.2022.08.17/*", "");
// input
DEFINE_string(count_bin_file, "/data/project/kuai/dump.2022.08.17.id_count.bin", "");
// output
DEFINE_string(output_dataset_meta_file, "/data/project/kuai/dump.2022.08.17.meta.txt",
              "");
DEFINE_int32(thread_count, 32, "");

struct FileMeta {
  std::string dataset_file;
  int nr_request;
  int nr_keys;
};

// ID to counter
FileMeta RenumberID(const std::string &dataset_file,
                    folly::ConcurrentHashMap<uint64_t, uint64_t> *renumber_map) {
  FileMeta file_meta;
  file_meta.dataset_file = dataset_file;

  std::vector<char> file_content;
  CHECK(folly::readFile(dataset_file.c_str(), file_content));

  PetCursor cursor(file_content.data(), file_content.data() + file_content.size());

  auto nr_request = cursor.ReadInt();
  file_meta.nr_request = nr_request;
  file_meta.nr_keys = 0;
  for (int64_t i = 0; i < nr_request; i++) {
    auto nr_keys_in_one_request = cursor.ReadInt();
    file_meta.nr_keys += nr_keys_in_one_request;

    for (int j = 0; j < nr_keys_in_one_request; j++) {
      uint64 &key = cursor.ReadUint64();
      int dim = cursor.ReadInt();
      CHECK(4 <= dim && dim <= 64) << dim;
      CHECK(dim == 4 || dim == 8 || dim == 16 || dim == 32 || dim == 64) << dim;
      CHECK(renumber_map->find(key) != renumber_map->end());
      key = (*renumber_map)[key];
    }
  }
  CHECK(folly::writeFile(file_content, dataset_file.c_str()));
  return file_meta;
}

// ID to counter
void Check(const std::string &dataset_file, uint64_t max_key) {
  std::vector<char> file_content;
  CHECK(folly::readFile(dataset_file.c_str(), file_content));

  PetCursor cursor(file_content.data(), file_content.data() + file_content.size());
  auto nr_request = cursor.ReadInt();
  for (int64_t i = 0; i < nr_request; i++) {
    auto nr_keys_in_one_request = cursor.ReadInt();
    for (int j = 0; j < nr_keys_in_one_request; j++) {
      uint64 key = cursor.ReadUint64();
      int dim = cursor.ReadInt();
      CHECK(4 <= dim && dim <= 64) << dim;
      CHECK(dim == 4 || dim == 8 || dim == 16 || dim == 32 || dim == 64) << dim;
      CHECK(0 <= key && key < max_key);
    }
  }
}

int main(int argc, char **argv) {
  folly::Init(&argc, &argv);

  std::vector<std::string> dataset_files;
  for (auto &p : glob::glob(FLAGS_dataset_file_str)) {
    dataset_files.push_back(p);
  }

  int nr_dataset_files = dataset_files.size();
  CHECK_NE(nr_dataset_files, 0);

  // Read Id->Count Map
  std::ifstream if_count_bin_file(FLAGS_count_bin_file, std::ios::binary | std::ios::ate);
  std::streamsize size = if_count_bin_file.tellg();
  if_count_bin_file.seekg(0, std::ios::beg);
  uint64_t *data = (uint64_t *)new char[size];
  if_count_bin_file.read((char *)data, size);
  CHECK(size % (2 * sizeof(uint64_t)) == 0);
  uint64 key_num = size / sizeof(uint64) / 2;
  folly::ConcurrentHashMap<uint64_t, uint64_t> *id_count_map =
      new folly::ConcurrentHashMap<uint64_t, uint64_t>();
  std::vector<uint64_t> ids;
  id_count_map->reserve(key_num);
  ids.reserve(key_num);

  LOG(INFO) << "start parse id 2 count map";

  const int id_count_map_thread = 32;
  uint64_t id_count_map_per_thread_count =
      (key_num + id_count_map_thread - 1) / id_count_map_thread;

  std::vector<std::thread> id_count_map_threads;

  for (int tid = 0; tid < id_count_map_thread; tid++) {
    uint64_t thread_start = tid * id_count_map_per_thread_count;
    uint64_t thread_end = std::min((tid + 1) * id_count_map_per_thread_count, key_num);
    id_count_map_threads.emplace_back([tid, thread_start, thread_end,
                                       id_count_map_per_thread_count, id_count_map,
                                       data]() {
      for (auto i = thread_start; i < thread_end; i++) {
        // key: data[i];
        // count: data[i + 1];
        if (tid == 0) {
          FB_LOG_EVERY_MS(INFO, 30000)
              << 100 * (i - thread_start) / id_count_map_per_thread_count << " %";
        }
        id_count_map->insert(data[2 * i], data[2 * i + 1]);
      }
    });
  }
  for (auto &t : id_count_map_threads) t.join();

  LOG(INFO) << "parse id 2 count map done";
  for (uint64_t i = 0; i < key_num; i++) {
    ids.push_back(data[2 * i]);
  }
  delete[]((char *)data);

  LOG(INFO) << "push_back ids done";

  // Sort by count
  LOG(INFO) << "start sort";
  oneapi::tbb::parallel_sort(ids, [&id_count_map](uint64_t a, uint64_t b) {
    return (*id_count_map)[a] > (*id_count_map)[a];
  });
  delete id_count_map;
  LOG(INFO) << "sort done";

  folly::ConcurrentHashMap<uint64_t, uint64_t> *renumber_map =
      new folly::ConcurrentHashMap<uint64_t, uint64_t>();
  // std::unordered_map<uint64_t, uint64_t> renumber_map;
  renumber_map->reserve(key_num);

  LOG(INFO) << "renumber map start";
#pragma omp parallel for num_threads(64)
  for (auto i = 0; i < ids.size(); i++) renumber_map->insert(ids[i], i);
  LOG(INFO) << "renumber map done";

  CHECK_EQ(ids.size(), renumber_map->size());
  CHECK_EQ(ids.size(), key_num);
  LOG(INFO) << folly::sformat("dataset has {} keys", key_num);

  std::vector<std::thread> th(FLAGS_thread_count);

  int file_num_per_thread =
      (nr_dataset_files + FLAGS_thread_count - 1) / FLAGS_thread_count;

  std::mutex mutex;
  std::ofstream of(FLAGS_output_dataset_meta_file);
  LOG(INFO) << "before renumbering";
  for (int i = 0; i < FLAGS_thread_count; i++) {
    th[i] = std::thread([i, file_num_per_thread, &dataset_files, nr_dataset_files, &mutex,
                         &of, renumber_map]() {
      for (int j = i * file_num_per_thread;
           j < std::min((i + 1) * file_num_per_thread, nr_dataset_files); j++) {
        if (i == 0)
          FB_LOG_EVERY_MS(INFO, 10000)
              << (j - i * file_num_per_thread) * 100 / file_num_per_thread << " %";
        auto file_meta = RenumberID(dataset_files[j], renumber_map);
        {
          std::lock_guard<std::mutex> _(mutex);
          of << file_meta.dataset_file << " " << file_meta.nr_request << " "
             << file_meta.nr_keys << std::endl;
        }
      }
    });
  }
  for (int i = 0; i < FLAGS_thread_count; ++i) {
    th[i].join();
  }
  LOG(INFO) << "renumbering done";
  for (int i = 0; i < FLAGS_thread_count; i++) {
    th[i] = std::thread(
        [i, file_num_per_thread, &dataset_files, nr_dataset_files, key_num]() {
          for (int j = i * file_num_per_thread;
               j < std::min((i + 1) * file_num_per_thread, nr_dataset_files); j++) {
            if (i == 0)
              FB_LOG_EVERY_MS(INFO, 10000)
                  << (j - i * file_num_per_thread) * 100 / file_num_per_thread << " %";
            Check(dataset_files[j], key_num);
          }
        });
  }
  for (int i = 0; i < FLAGS_thread_count; ++i) {
    th[i].join();
  }
  LOG(INFO) << "check done";
  return 0;
}