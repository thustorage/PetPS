#include <folly/portability/GTest.h>
#include <string>
#include <utility>
#include <vector>

#include "base/base.h"
#include "pet_kv/pet_hash.h"
namespace base {

TEST(PetHash, Normal) {
  base::PseudoRandom random;
  const int kCapacity = 14 * (1 << 18);
  const int kTestNum = kCapacity * 0.9;
  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(kCapacity);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(kCapacity);
  pet_hash->Debug();
  CHECK(pet_hash->Valid(memory_size));

  auto begin = GetTimestamp();
  for (int i = 0; i < kTestNum; ++i) {
    uint64_t key = random.GetInt(0, 1 << 26);
    test_data.push_back(std::make_pair(key, key));
    bool succ =
        pet_hash->Set(test_data[i].first, test_data[i].second) != nullptr;
    CHECK(succ);
  }

  auto f14_hash2 = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  std::cout << "kTestNum: " << kTestNum << "\n";
  CHECK(f14_hash2->Valid(memory_size));
  for (int i = 0; i < kTestNum; ++i) {
    auto [result, exists] = f14_hash2->Get(test_data[i].first);
    CHECK(exists);
    CHECK_EQ(result, test_data[i].second);
  }

  for (int i = 0; i < kTestNum; ++i) {
    pet_hash->Delete(test_data[i].first);
    auto [result, exists] = pet_hash->Get(test_data[i].first);
    CHECK(!exists);
  }

  auto use_time = (GetTimestamp() - begin);
  pet_hash->Debug();
  std::cout << "Total Time: " << use_time << "\n";

  delete[] data_;
}

TEST(PetHash, ForceInsert) {
  base::PseudoRandom random;
  const int kCapacity = 14 * (1 << 16);
  const int kTestNum = kCapacity * 4;
  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(kCapacity);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(kCapacity);
  pet_hash->Debug();

  for (int i = 0; i < kTestNum; ++i) {
    CHECK(pet_hash->Set(i, i, nullptr, true) != nullptr);
    auto [val, exists] = pet_hash->Get(i);
    CHECK(exists && val == i);
  }
  CHECK(pet_hash->Valid(memory_size));

  delete[] data_;
}

TEST(PetHash, Recovery) {
  base::PseudoRandom random;
  const int kCapacity = 14 * (1 << 16);
  const int kTestNum = kCapacity * 4;
  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(kCapacity);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(kCapacity);
  pet_hash->Debug();

  for (int i = 0; i < kTestNum; ++i) {
    CHECK(pet_hash->Set(i, i, nullptr, true) != nullptr);
    auto [val, exists] = pet_hash->Get(i);
    CHECK(exists && val == i);
  }
  CHECK(pet_hash->Valid(memory_size));
  std::cout << "before recovery, size = " << pet_hash->Size() << std::endl;

  char *old_data_ = new char[memory_size];
  memcpy(old_data_, data_, memory_size);
  // NOTE(xieminhui): begin reload
  pet_hash->Reload(kCapacity, false, [&](uint64_t, uint64_t) {});
  std::cout << "after recovery, size = " << pet_hash->Size() << std::endl;
  int res = memcmp(old_data_, data_, memory_size);
  CHECK_EQ(res, 0);

  delete[] data_;
  delete[] old_data_;
}

TEST(PetHash, Multithread) {
  base::PseudoRandom random;
  const int NR_READ_THREAD = 10;
  const int NR_WRITE_THREAD = 10;
  const int NR_THREAD = NR_READ_THREAD + NR_WRITE_THREAD;
  const int NR_OP = 100000;
  const float load_factor = 0.8;

  const int64 dict_capacity = NR_OP / load_factor;
  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(dict_capacity, true);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(dict_capacity, true);
  CHECK(pet_hash->Valid(memory_size));

  base::ScopedTempDir dir;
  EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath(SHM_PATH));
  std::cout << dir.path().value() << std::endl;

  std::vector<std::thread> threadVec(NR_THREAD);
  for (int i = 0; i < NR_THREAD; i++) {
    if (i < NR_READ_THREAD) {
      threadVec[i] = std::thread([pet_hash] {
        for (int j = 0; j < NR_OP; ++j) {
          uint64_t key = j;
          auto [kv_data, exists] = pet_hash->Get(key);
          if (exists) {
            CHECK_EQ(kv_data, key);
          }
        }
      });
    } else if (i < NR_READ_THREAD + NR_WRITE_THREAD) {
      threadVec[i] = std::thread([pet_hash] {
        for (int j = 0; j < NR_OP; ++j) {
          uint64_t key = j;
          CHECK(pet_hash->Set(key, key)) << "key " << key << "\n";
        }
      });
    }
  }
  for (int i = 0; i < NR_THREAD; ++i) {
    threadVec[i].join();
  }
  for (int i = 0; i < NR_OP; ++i) {
    uint64_t key = i;
    auto [kv_data, exists] = pet_hash->Get(key);
    CHECK(exists) << "final check; missing key " << key;
    CHECK_EQ(kv_data, key);
  }

  CHECK(pet_hash->Valid(memory_size));

  delete[] data_;
}

void CheckHash(PetHash<uint64_t, uint64_t, true> *pet_hash, size_t capacity,
               const std::vector<std::pair<uint64_t, uint64_t>> &test_data) {
  CHECK_EQ(pet_hash->Size(), capacity);
  for (size_t i = 0; i < test_data.size(); ++i) {
    auto [result, exists] = pet_hash->Get(test_data[i].first);
    CHECK(exists) << fmt::format("key is {}", test_data[i].first);
    CHECK(result == test_data[i].second);
  }
  CHECK_EQ(pet_hash->Size(), capacity);
}

TEST(PetHash, HotnessMigration) {
  int seed = Rdtsc::ReadTSC();
  LOG(INFO) << "seed is " << seed;
  base::PseudoRandom random(seed);
  const int kCapacity = 1 * (1e6);
  const int kTestNum = kCapacity * 0.8;
  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(kCapacity);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(kCapacity);
  pet_hash->Debug();
  CHECK(pet_hash->Valid(memory_size));

  std::unordered_map<uint64_t, uint64_t> test_data_map;
  for (int i = 0; i < kTestNum; ++i) {
    uint64_t key = random.GetInt(0, 1 << 26);
    test_data.push_back(std::make_pair(key, key + 1));
    test_data_map[key] = key + 1;
    bool succ = pet_hash->Set(test_data[i].first, test_data[i].second, nullptr,
                              true) != nullptr;
    CHECK(succ);
  }

  for (int _ = 0; _ < 100; _++) {
    LOG(INFO) << "HotnessMigration [i] = " << _;
    CHECK_EQ(pet_hash->Size(), test_data_map.size());
    std::vector<uint64_t> hotset;
    {
      const int hotset_capacity = 1000;
      hotset.reserve(hotset_capacity);
      for (int i = 0; i < hotset_capacity; i++) {
        auto hot_key = test_data[random.GetInt(0, test_data.size())].first;
        hotset.push_back(hot_key);
      }
    }
    pet_hash->HotnessAwareMigration(hotset);
    CheckHash(pet_hash, test_data_map.size(), test_data);
  }
  CheckHash(pet_hash, test_data_map.size(), test_data);

  for (int i = 0; i < kTestNum; ++i) {
    pet_hash->Delete(test_data[i].first);
    auto [result, exists] = pet_hash->Get(test_data[i].first);
    CHECK(!exists);
  }
  CHECK_EQ(pet_hash->Size(), 0) << "seed = " << seed;

  pet_hash->Debug();
  delete[] data_;
}

TEST(PetHash, HotnessMigrationMultiThread) {
  // GTEST_SKIP();
  int seed = Rdtsc::ReadTSC();
  LOG(INFO) << "seed is " << seed;
  base::PseudoRandom random(seed);
  const int kCapacity = 1 * (1e6);
  const int kTestNum = kCapacity * 0.8;
  const int kReaderThreadNumber = 32;

  uint64_t memory_size =
      PetHash<uint64_t, uint64_t, true>::MemorySize(kCapacity);
  PetHash<uint64_t, uint64_t, true> d_;
  std::vector<std::pair<uint64_t, uint64_t>> test_data;
  char *data_ = new char[memory_size];
  auto pet_hash = reinterpret_cast<PetHash<uint64_t, uint64_t, true> *>(data_);
  pet_hash->Initialize(kCapacity);
  pet_hash->Debug();
  CHECK(pet_hash->Valid(memory_size));

  std::unordered_map<uint64_t, uint64_t> test_data_map;
  for (int i = 0; i < kTestNum; ++i) {
    uint64_t key = random.GetInt(0, 1 << 26);
    test_data.push_back(std::make_pair(key, key + 1));
    test_data_map[key] = key + 1;
    bool succ = pet_hash->Set(test_data[i].first, test_data[i].second, nullptr,
                              true) != nullptr;
    CHECK(succ);
  }

  // read v.s. migration

  std::atomic<bool> migration_thread_stop_flag{false};
  auto migrate_thread = std::thread([pet_hash, &random, &test_data_map,
                                     &test_data,
                                     &migration_thread_stop_flag]() {
    while (!migration_thread_stop_flag.load()) {
      FB_LOG_EVERY_MS(INFO, 4000) << "HotnessMigration";
      CHECK_EQ(pet_hash->Size(), test_data_map.size());
      std::vector<uint64_t> hotset;
      {
        const int hotset_capacity = 1000;
        hotset.reserve(hotset_capacity);
        for (int i = 0; i < hotset_capacity; i++) {
          auto hot_key = test_data[random.GetInt(0, test_data.size())].first;
          hotset.push_back(hot_key);
        }
      }
      pet_hash->HotnessAwareMigration(hotset);
      CheckHash(pet_hash, test_data_map.size(), test_data);
    }
  });

  std::atomic<bool> reader_thread_stop_flag{false};
  std::vector<std::thread> reader_threads;
  for (int reader_no = 0; reader_no < kReaderThreadNumber; reader_no++) {
    reader_threads.emplace_back([reader_no, pet_hash, &test_data_map,
                                 &test_data, &reader_thread_stop_flag]() {
      while (!reader_thread_stop_flag) {
        FB_LOG_EVERY_MS(INFO, 4000) << "Reader thread " << reader_no;
        CHECK_EQ(pet_hash->Size(), test_data_map.size());
        for (size_t i = 0; i < test_data.size(); ++i) {
          auto [result, exists] = pet_hash->Get(test_data[i].first);
          CHECK(exists) << fmt::format("key is {}", test_data[i].first);
          CHECK(result == test_data[i].second);
        }
        CHECK_EQ(pet_hash->Size(), test_data_map.size());
      }
    });
    // std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::this_thread::sleep_for(std::chrono::seconds(10));

  reader_thread_stop_flag = true;
  for (auto &t : reader_threads) {
    t.join();
  }
  migration_thread_stop_flag = true;
  migrate_thread.join();
  CheckHash(pet_hash, test_data_map.size(), test_data);

  for (int i = 0; i < kTestNum; ++i) {
    pet_hash->Delete(test_data[i].first);
    auto [result, exists] = pet_hash->Get(test_data[i].first);
    CHECK(!exists);
  }
  CHECK_EQ(pet_hash->Size(), 0) << "seed = " << seed;

  pet_hash->Debug();
  delete[] data_;
}

}  // namespace base
