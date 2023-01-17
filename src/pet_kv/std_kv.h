#pragma once

#include "base/base.h"

namespace base {
class DBApi {
public:
  virtual bool Write(uint64 key, const char *data, int size,
                     uint32 expire_timet) = 0;
  bool Write(uint64 key, const char *data, int size) {
    return Write(key, data, size, 0);
  }

  virtual bool Read(uint64 key, std::string *data, uint32 *expire_timet) = 0;
  bool Read(uint64 key, std::string *data) {
    uint32 expire_timet = 0;
    if (!Read(key, data, &expire_timet))
      return false;
    if (expire_timet == 0)
      return true;
    if (expire_timet < base::GetTimestamp() / 1000000)
      return false;
    return true;
  }
  virtual std::string GetInfo() const { return ""; }
  virtual ~DBApi() {}

protected:
  DBApi() {}

private:
  DISALLOW_COPY_AND_ASSIGN(DBApi);
};

class TestDB : public DBApi {
public:
  TestDB() {}
  ~TestDB() {}
  virtual bool Write(uint64 key, const char *data, int size,
                     uint32 expire_timet) override {
    key_values_[key] = std::make_pair(expire_timet, std::string(data, size));
    return true;
  }
  virtual bool Read(uint64 key, std::string *data,
                    uint32 *expire_timet) override {
    data->clear();
    auto it = key_values_.find(key);
    if (it == key_values_.end())
      return false;
    *expire_timet = it->second.first;
    *data = it->second.second;
    return true;
  }

  std::string GetInfo() const override {
    return folly::sformat("[TestDB] size={}", key_values_.size());
  }

private:
  std::unordered_map<uint64, std::pair<uint32, std::string>> key_values_;
  DISALLOW_COPY_AND_ASSIGN(TestDB);
};
} // namespace base