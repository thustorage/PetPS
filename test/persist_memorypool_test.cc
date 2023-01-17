#include <folly/portability/GTest.h>
#include <algorithm>
#include <string>

#include "base/base.h"
#include "memory/persist_malloc.h"

namespace base {

static base::PseudoRandom random;

struct TestData {
  uint64 key;
  int size;
};

void CheckPersistMemoryPool(
    MallocApi *malloc, std::unordered_map<int64, std::string> *std_memory) {
  std::unordered_map<int64, char *> alloc_memory;
  std::vector<char *> mallocs_data;
  std::vector<int64> mallocs_offset;
  malloc->GetMallocsAppend(&mallocs_data);
  malloc->GetMallocsAppend(&mallocs_offset);
  CHECK_EQ(mallocs_data.size(), malloc->total_malloc());
  CHECK_EQ(mallocs_offset.size(), malloc->total_malloc());
  CHECK_EQ(std_memory->size(), malloc->total_malloc());

  for (int i = 0; i < mallocs_data.size(); ++i) {
    CHECK_EQ(malloc->GetMallocOffset(mallocs_data[i]), mallocs_offset[i]);
    CHECK_EQ(malloc->GetMallocData(mallocs_offset[i]), mallocs_data[i]);
  }
  for (auto block_data : mallocs_data) {
    auto test_data = reinterpret_cast<TestData *>(block_data);
    alloc_memory[test_data->key] = block_data;
    CHECK_EQ(std_memory->count(test_data->key), 1);
    CHECK_EQ(malloc->GetMallocSize(block_data),
             sizeof(TestData) + test_data->size);
    CHECK_EQ((*std_memory)[test_data->key].size(), test_data->size);
    CHECK_EQ((*std_memory)[test_data->key],
             std::string(block_data + sizeof(TestData), test_data->size));
  }

  for (int key = 0; key < 3 * 60 * (1 << 10); ++key) {
    // free
    if (std_memory->count(key) != 0 && random.GetInt(0, 2) == 0) {
      CHECK(alloc_memory.count(key));
      CHECK(malloc->Free(alloc_memory[key]));
      alloc_memory.erase(key);
      std_memory->erase(key);
      CHECK_EQ(std_memory->size(), malloc->total_malloc());
    }

    // malloc
    if (std_memory->count(key) == 0 && random.GetInt(0, 2) == 0) {
      int block_size = 0;
      if (random.GetInt(0, 2) == 0)
        block_size = 16;
      else
        block_size = 40;

      TestData test_data;
      test_data.key = key;
      test_data.size = block_size;

      auto data = malloc->New(sizeof(TestData) + block_size);
      CHECK(data);
      std::string random_string = random.GetString(block_size);
      memcpy(data, reinterpret_cast<const char *>(&test_data),
             sizeof(TestData));
      memcpy(data + sizeof(TestData), random_string.data(),
             random_string.size());
      (*std_memory)[key] = random_string;
      alloc_memory[key] = data;
      CHECK_EQ(std_memory->size(), malloc->total_malloc());
    }
  }

  CHECK_EQ(alloc_memory.size(), std_memory->size());
  for (auto it : alloc_memory) {
    auto test_data = reinterpret_cast<TestData *>(it.second);
    CHECK_EQ(std_memory->count(test_data->key), 1);
    CHECK_EQ((*std_memory)[test_data->key].size(), test_data->size);
    CHECK_EQ((*std_memory)[test_data->key],
             std::string(it.second + sizeof(TestData), test_data->size));
  }

  std::cout << malloc->GetInfo() << std::endl;
}

// TEST(PersistMemoryPool, OOM) {
//   base::ScopedTempDir dir;
//   EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath(SHM_PATH));
//   std::cout << dir.path().value() << std::endl;
//   PersistMemoryPool malloc(dir.path().value() + "/malloc", 1024 * 16 + 1024 *
//   16 / 64); for (int i = 0; i < 16; ++i) CHECK(malloc.New(1024 - 8)) << i;
//   CHECK(!malloc.New(8));
//   std::vector<char *> mallocs_data;
//   malloc.GetMallocsAppend(&mallocs_data);
//   CHECK_EQ(mallocs_data.size(), 16);
//   CHECK_EQ(mallocs_data.size(), malloc.total_malloc());
//   for (auto block : mallocs_data) CHECK(malloc.Free(block));
//   for (int i = 0; i < 16; ++i) CHECK(malloc.New(1024 - 8));
//   CHECK(!malloc.New(8));
// }

TEST(PersistMemoryPool, PerfectFit) {
  auto seed = base::GetTimestamp();
  LOG(INFO) << "seed is " << seed;
  random.SetSeed(seed);
  base::ScopedTempDir dir;
  EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath(SHM_PATH));
  std::cout << dir.path().value() << std::endl;
  std::unordered_map<int64, std::string> memory;
  PersistMemoryPool<true> malloc(
      dir.path().value() + "/malloc", 10 * 1024 * 1024LL,
      {16 + sizeof(TestData), 40 + sizeof(TestData)});
  for (int test_num = 0; test_num < 16; ++test_num) {
    std::cout << "multi malloc test: " << test_num << std::endl;
    CheckPersistMemoryPool(&malloc, &memory);
  }
}

TEST(PersistMemoryPool, BestFit) {
  auto seed = base::GetTimestamp();
  LOG(INFO) << seed;
  random.SetSeed(seed);
  base::ScopedTempDir dir;
  EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath(SHM_PATH));
  std::cout << dir.path().value() << std::endl;
  std::unordered_map<int64, std::string> memory;
  PersistMemoryPool<false> malloc(
      dir.path().value() + "/malloc", 10 * 1024 * 1024LL,
      {8 + 16 + sizeof(TestData), 8 + 40 + sizeof(TestData)});
  for (int test_num = 0; test_num < 16; ++test_num) {
    std::cout << "multi malloc test: " << test_num << std::endl;
    CheckPersistMemoryPool(&malloc, &memory);
  }
}

}  // namespace base
