#if !defined(_KV_CLIENT_H_)
#define _KV_CLIENT_H_

#include "Common.h"
#include "DSM.h"

namespace kv {

class KVClient {

public:
  bool get(const Slice &key, Slice &value);
  bool put(const Slice &key, const Slice &value);
  bool del(const Slice &key);

  void get_async(const Slice &key, Slice &value, uint8_t rpc_tag = 0);
  void put_async(const Slice &key, const Slice &value, uint8_t rpc_tag = 0);
  void del_async(const Slice &key, uint8_t rpc_tag = 0);

  RawMessage *sync_once() { return dsm->rpc_wait(); }

  bool pipeline_get_for_test(const Slice &key, uint64_t &counter);
  bool pipeline_put_for_test(const Slice &key, const Slice &value,
                             uint64_t &counter);

  void fetch_mapping();

  void stats();

  KVClient(DSM *dsm,
           uint16_t server_thread_nr);
  ~KVClient();

private:
  DSM *dsm;
  char *buf_for_get;
  uint16_t server_thread_nr;

  RawMessage *msg;

  std::pair<uint32_t, uint32_t> get_target(const Slice &k);

  uint32_t seed;
};

} // namespace kv

#endif // _KV_CLIENT_H_
