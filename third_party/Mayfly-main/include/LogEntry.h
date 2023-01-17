#if !defined(_LOG_ENTRY_H_)
#define _LOG_ENTRY_H_

#include "Common.h"
#include "NVM.h"
#include <cassert>
#include <cstdint>
#include <iostream>

namespace kv {

enum LogType : uint8_t {
  LOG_DEL = 0x11,
  LOG_PUT,
  LOG_END, // for padding at the end of segment
};

class LogEntry {
public:
  CRCType crc;
  VersionType ver;
  KeySizeType key_size;
  ValueSizeType value_size;
  char kv[0];

  uint32_t size() { return sizeof(LogEntry) + key_size + value_size; }

  Slice get_key() { return Slice(kv, key_size); }

  Slice get_value() { return Slice(kv + key_size, value_size); }

  void persist() { clwb_range(this, this->size()); }

  void print() {
    std::cout << "size: " << size() << ", key: " << get_key().to_string()
              << ", value: " << get_value().to_string() << std::endl;
  }

} __attribute__((packed));

} // namespace kv

#endif // _LOG_ENTRY_H_
