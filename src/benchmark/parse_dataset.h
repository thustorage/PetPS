#pragma once
#include "base/base.h"
#include "base/glob.h"
#include "base/timer.h"

#include "folly/Conv.h"
#include "folly/FBString.h"
#include "folly/FileUtil.h"
#include "folly/GLog.h"
#include "folly/Range.h"
#include "folly/String.h"
#include "folly/init/Init.h"
#include "folly/system/MemoryMapping.h"
#include "ps/Postoffice.h"

#include <folly/MPMCQueue.h>
#include <string>

class PetCursor {
public:
  PetCursor(const char *start, const char *end) : start_(start), end_(end) {
    data_ = start_;
  }

  int &ReadInt() {
    auto &r = *(int *)data_;
    data_ += sizeof(int);
    CHECK_LE(data_, end_);
    return r;
  }
  uint64 &ReadUint64() {
    auto &r = *(uint64 *)data_;
    data_ += sizeof(uint64);
    CHECK_LE(data_, end_);
    return r;
  }
  int64 &ReadInt64() {
    auto &r = *(int64 *)data_;
    data_ += sizeof(int64);
    CHECK_LE(data_, end_);
    return r;
  }

  bool isEnd() const { return data_ == end_; }

private:
  const char *data_;
  const char *start_;
  const char *end_;
};

class PetDatasetReader {
public:
  struct PetSample {
    int key_num_;
    std::shared_ptr<std::vector<uint64_t>> keys_;
    // std::shared_ptr<std::vector<int>> dims_;
  };
  PetDatasetReader(int reader_thread_count, uint64_t hashed_key_space_M,
                   const std::string &dataset_file_wildcard_str, bool repeat)
      : reader_thread_count_(reader_thread_count),
        hashed_key_space_(hashed_key_space_M * 1024 * 1024LL), repeat_(repeat) {
    int count = 0;
    for (auto &p : glob::glob(dataset_file_wildcard_str)) {
      // first split to different client processes
      if (count % XPostoffice::GetInstance()->NumClients() ==
          XPostoffice::GetInstance()->ClientID())
        dataset_files_.push_back(p);
      count++;
    }

    CHECK_NE(dataset_files_.size(), 0) << "not find dataset files";

    std::vector<std::vector<std::string>> thread_dataset_files(
        reader_thread_count);

    thread_samples.resize(reader_thread_count);

    for (int file_no = 0; file_no < dataset_files_.size(); file_no++) {
      thread_dataset_files[file_no % reader_thread_count].push_back(
          dataset_files_[file_no]);
    }

#pragma omp parallel for num_threads(32)
    for (int tid = 0; tid < reader_thread_count; tid++) {
      auto &thread_files = thread_dataset_files[tid];
      for (int fid = 0; fid < thread_files.size(); fid++) {
        // if (fid >= 10) {
        //   LOG(WARNING) << "only read 10 datasets";
        //   break;
        // }
        auto filename = thread_files[fid];
        std::vector<char> file_contents;
        folly::readFile(filename.c_str(), file_contents);
        FB_LOG_EVERY_MS(INFO, 1000) << "Thread " << tid << " loading datasets "
                                    << fid * 100 / thread_files.size() << " %";

        PetCursor cursor(file_contents.data(),
                         file_contents.data() + file_contents.size());

        auto nr_request = cursor.ReadInt();
        for (int64_t i = 0; i < nr_request; i++) {
          auto nr_keys_in_one_request = cursor.ReadInt();
          auto keys =
              std::make_shared<std::vector<uint64_t>>(nr_keys_in_one_request);
          // auto dims =
          // std::make_shared<std::vector<int>>(nr_keys_in_one_request);
          for (int j = 0; j < nr_keys_in_one_request; j++) {
            uint64 key = (cursor.ReadUint64() % hashed_key_space_) + 1;
            int dim = cursor.ReadInt();
            CHECK(4 <= dim && dim <= 64) << dim;
            CHECK(dim == 4 || dim == 8 || dim == 16 || dim == 32 || dim == 64)
                << dim;
            (*keys)[j] = key;
            // (*dims)[j] = dim;
          }
          PetSample sample;
          sample.key_num_ = nr_keys_in_one_request;
          sample.keys_ = keys;
          // sample.dims_ = dims;
          thread_samples[tid].push_back(sample);
        }
      }
    }
  }

  PetSample ReadNext(int tid) {
    thread_local int sample_index = 0;
    auto ret = thread_samples.at(tid).at(sample_index);
    sample_index += 1;
    if (sample_index == thread_samples.at(tid).size()) {
      if (repeat_)
        sample_index = 0;
      else {
        LOG(FATAL) << "samples read out";
      }
    }
    return ret;
  }

private:
  int reader_thread_count_;
  constexpr static int initial_q_capacity_ = 100;
  const uint64_t hashed_key_space_;
  std::vector<std::string> dataset_files_;
  std::vector<std::vector<PetSample>> thread_samples;
  bool repeat_;
};
