#pragma once
#include "base/array.h"
#include "base/zipf.h"
#include "parse_dataset.h"

class SampleReader {
 public:
  virtual base::MutableArray<uint64_t> fillArray(uint64_t *buffer) = 0;
  virtual ~SampleReader(){};
};

class ZipfianSampleReader : public SampleReader {
 public:
  ZipfianSampleReader(int tid, int key_space_M, double zipf_theta, int nr_keys_per_req)
      : nr_keys_per_req_(nr_keys_per_req) {
    LOG(INFO) << "before mehcached_zipf_init";
    mehcached_zipf_init(&state, key_space_M * 1024 * 1024LL, zipf_theta,
                        (rdtsc() & 0x0000ffffffffffffull) ^ tid);
    LOG(INFO) << "after mehcached_zipf_init";
  }

  base::MutableArray<uint64_t> fillArray(uint64_t *buffer) override {
    base::MutableArray<uint64_t> ret(buffer, nr_keys_per_req_);
    for (int i = 0; i < ret.Size(); i++) {
      ret[i] = mehcached_zipf_next(&state) + 1;
    }
    return ret;
  }

 private:
  static __inline__ unsigned long long rdtsc(void) {
    unsigned hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
  }
  struct zipf_gen_state state;
  int nr_keys_per_req_;
};

// Multiple samples may be read to replenish to nr_keys_per_req
class PetDatasetSampleReader : public SampleReader {
 public:
  PetDatasetSampleReader(PetDatasetReader *dataset_reader, int tid, int key_space_M,
                         int nr_keys_per_req)
      : tid_(tid)
      , nr_keys_per_req_(nr_keys_per_req)
      , cursor_(0)
      , dataset_reader_(dataset_reader) {
    cursor_sample_ = dataset_reader_->ReadNext(tid_);
    cursor_ = 0;
  }

  base::MutableArray<uint64_t> fillArray(uint64_t *buffer) override {
    base::MutableArray<uint64_t> ret(buffer, nr_keys_per_req_);
    xmh::Timer get_sample("sample", 1);
    int filled_key_index = 0;

    while (filled_key_index != nr_keys_per_req_) {
      if (cursor_ < cursor_sample_.key_num_) {
        CHECK_EQ(cursor_sample_.key_num_, cursor_sample_.keys_->size());
        int remained_keys_in_this_sample = cursor_sample_.key_num_ - cursor_;

        int need_copy_key_num =
            std::min(remained_keys_in_this_sample, nr_keys_per_req_ - filled_key_index);
        // copy [cursor_, cursor + need_copy_key_num) to [filled_key_index,
        // filled_key_index+need_copy_key_num]
        std::copy_n(&cursor_sample_.keys_->at(cursor_), need_copy_key_num,
                    buffer + filled_key_index);
        cursor_ += need_copy_key_num;
        filled_key_index += need_copy_key_num;
      } else {
        cursor_sample_ = dataset_reader_->ReadNext(tid_);
        cursor_ = 0;
      }
    }
    get_sample.end();
    return ret;
  }

 private:
  int tid_;
  int nr_keys_per_req_;
  int cursor_;  // point to the first unread key of cusor_sample_
  PetDatasetReader::PetSample cursor_sample_;
  PetDatasetReader *dataset_reader_;
};

// directly replay the dataset
class PetDatasetInferenceReader : public SampleReader {
 public:
  PetDatasetInferenceReader(PetDatasetReader *dataset_reader, int tid, int key_space_M)
      : tid_(tid), dataset_reader_(dataset_reader) {}

  base::MutableArray<uint64_t> fillArray(uint64_t *buffer) override {
    PetDatasetReader::PetSample cursor_sample = dataset_reader_->ReadNext(tid_);
    std::copy_n(&cursor_sample.keys_->at(0), cursor_sample.key_num_, buffer);
    base::MutableArray<uint64_t> ret(buffer, cursor_sample.key_num_);
    CHECK_LE(cursor_sample.key_num_, 500);
    return ret;
  }

 private:
  int tid_;
  PetDatasetReader *dataset_reader_;
};