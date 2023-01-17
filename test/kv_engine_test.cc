#include <folly/portability/GTest.h>
#include <string>
#include <vector>

#include "base/base.h"
#include "base/factory.h"
#include "base/timer.h"
#include "kv_engine/base_kv.h"


TEST(KVEngine, Normal) {
  GTEST_SKIP();
  int64_t capacity = 1000000;
  int value_size = 32;
  int emb_dim = value_size / sizeof(float);

  BaseKVConfig config;
  base::ScopedTempDir dir;
  EXPECT_TRUE(dir.CreateUniqueTempDirUnderPath("/media/aep0/"));

  config.path = dir.path().value();
  config.capacity = capacity;
  config.value_size = value_size;
  config.hash_size = 4096;
  config.pool_size = 32UL * 1024 * 1024 * 1024 / 8;
  // config.pool_size        = 64UL * 1024 * 1024 * 1024; // why in clevel
  config.num_threads = 1;
  // config.hash_name        = "clht";
  // config.hash_name        = "level";
  // config.hash_name        = "clevel";
  config.hash_name        = "ccehvm";

  auto kv = base::Factory<BaseKV, const BaseKVConfig &>::NewInstance("HashAPI",
                                                                     config);
  
  int64_t insert_capacity = capacity;
  for (int i = 1; i < insert_capacity; i++) {
    std::vector<float> value(emb_dim, i);
    kv->Put(i, std::string_view((char *)value.data(), value_size), 0);
  }

  kv->Util();
  for (int i = 0; i < insert_capacity; i++) {
    std::string value;
    kv->Get(i, value, 0);
    XDebug::AssertTensorEq((float *)value.c_str(), emb_dim, i, "123");
  }
}