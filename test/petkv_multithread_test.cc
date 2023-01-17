#include <folly/portability/GTest.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "pet_kv/pet_kv.h"

namespace base {

static base::PseudoRandom random(100);

TEST(PetKV, CheckMultiThread) {
  base::PMMmapRegisterCenter::GetConfig().numa_id = 0;

  const int NR_READ_THREAD = 10;
  const int NR_WRITE_THREAD = 10;
  const int NR_THREAD = NR_READ_THREAD + NR_WRITE_THREAD;
  const int NR_OP = 100000;
  const int64 memory_size = 32 * sizeof(float) * NR_OP * 2;
  const int64 dict_size = NR_OP;

  base::ScopedTempDir dir;
  EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath(SHM_PATH));
  std::cout << dir.path().value() << std::endl;
  PetKV *shm_kv =
      new PetKV(dir.path().value() + "/shm", memory_size, dict_size);

  std::vector<std::thread> threadVec(NR_THREAD);
  for (int i = 0; i < NR_THREAD; i++) {
    if (i < NR_READ_THREAD) {
      threadVec[i] = std::thread([shm_kv] {
        for (int j = 0; j < NR_OP; ++j) {
          uint64_t key = j;
          auto kv_data = shm_kv->Get(key);
          if (kv_data.data) {
            std::string value(32, '-');
            std::memcpy(&value[0], &key, sizeof(uint64_t));
            CHECK_EQ(std::string_view(kv_data.data, kv_data.size), value);
          }
        }
      });
    } else if (i < NR_READ_THREAD + NR_WRITE_THREAD) {
      threadVec[i] = std::thread([shm_kv] {
        for (int j = 0; j < NR_OP; ++j) {
          uint64_t key = j;
          std::string value(32, '-');
          std::memcpy(&value[0], &key, sizeof(uint64_t));
          CHECK(shm_kv->Update(key, value.data(), value.size()));
        }
      });
    }
  }
  for (int i = 0; i < NR_THREAD; ++i) {
    threadVec[i].join();
  }
  for (int i = 0; i < NR_OP; ++i) {
    uint64_t key = i;
    auto kv_data = shm_kv->Get(key);
    CHECK(kv_data.data);
    CHECK_EQ(kv_data.size, 32);
    std::string value(32, '-');
    std::memcpy(&value[0], &key, sizeof(uint64_t));
    CHECK_EQ(std::string_view(kv_data.data, kv_data.size), value);
  }
  delete shm_kv;
}
}  // namespace base