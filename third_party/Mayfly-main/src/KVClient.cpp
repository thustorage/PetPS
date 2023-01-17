#include "KVClient.h"
#include "murmur_hash2.h"

namespace kv {

KVClient::KVClient(DSM *dsm, uint16_t server_thread_nr)
    : server_thread_nr(server_thread_nr) {
  buf_for_get = (char *)malloc(MESSAGE_SIZE);
  msg = RawMessage::get_new_msg();
}

KVClient::~KVClient() {
  free(buf_for_get);
  RawMessage::put_new_msg(msg);
}

std::atomic_bool is_hotspot_shift{false}; // for mirgation test
std::vector<uint64_t> key_list_of_shard_0;

std::pair<uint32_t, uint32_t> KVClient::get_target(const Slice &k) {

  thread_local uint64_t next_id = 0;

  HashType hash = MurmurHash64A(k.s, k.len);
  uint32_t tagert_node = 0;

  uint32_t t_id = 0;

  return std::make_pair(tagert_node, t_id);
}

constexpr int kPipelineWdn = 1;
bool KVClient::pipeline_get_for_test(const Slice &key, uint64_t &counter) {
  msg->clear();
  msg->set_type(RpcType::GET);

  auto target = get_target(key);

  msg->add<KeySizeType>(key.len);
  msg->add_string_wo_size(key.s, key.len);

  const int pipeline_wdn = kPipelineWdn;

  for (int k = 0; k < pipeline_wdn; ++k) {
    dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
  }

  while (true) {
    dsm->rpc_wait();

    dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
    counter++;
  }

  return true;
}

bool KVClient::pipeline_put_for_test(const Slice &key, const Slice &value,
                                     uint64_t &counter) {
  msg->clear();
  msg->set_type(RpcType::PUT);

  auto target = get_target(key);

  msg->add<KeySizeType>(key.len);
  msg->add<ValueSizeType>(value.len);
  msg->add_string_wo_size(key.s, key.len);
  msg->add_string_wo_size(value.s, value.len);

  const int pipeline_wdn = kPipelineWdn;

  for (int k = 0; k < pipeline_wdn; ++k) {
    dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
  }

  while (true) {
    dsm->rpc_wait();

    dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
    counter++;
  }

  return true;
}

void KVClient::get_async(const Slice &key, Slice &value, uint8_t rpc_tag) {
  msg->clear();
  msg->set_type(RpcType::GET);
  msg->rpc_tag = rpc_tag;
  // msg->receive_gaddr = 0;

  auto target = get_target(key);

  msg->add<KeySizeType>(key.len);
  msg->add_string_wo_size(key.s, key.len);

  dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
}

bool KVClient::get(const Slice &key, Slice &value) {

  get_async(key, value);
  auto resp = dsm->rpc_wait();
  if (resp->type == RpcType::RESP_GET_NOT_FOUND) {
    return false;
  }
  if (resp->type != RpcType::RESP_GET_OK) {
    fprintf(stderr, "get error\n");
    exit(-1);
  }

  Cursor cur;
  Slice v = resp->get_string(cur);
  memcpy(buf_for_get, v.s, v.len);
  value = Slice(buf_for_get, v.len);

  return true;
}

void KVClient::put_async(const Slice &key, const Slice &value,
                         uint8_t rpc_tag) {
  msg->clear();
  msg->set_type(RpcType::PUT);
  msg->rpc_tag = rpc_tag;
  // msg->receive_gaddr = 0;

  auto target = get_target(key);

  msg->add<KeySizeType>(key.len);
  msg->add<ValueSizeType>(value.len);
  msg->add_string_wo_size(key.s, key.len);
  msg->add_string_wo_size(value.s, value.len);

  dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
}

bool KVClient::put(const Slice &key, const Slice &value) {

  put_async(key, value);
  auto resp = dsm->rpc_wait();
  if (resp->type != RpcType::RESP_PUT) {
    // fprintf(stderr, "put error %d\n", resp->type);
    // exit(-1);
  }

  return true;
}

void KVClient::del_async(const Slice &key, uint8_t rpc_tag) {
  msg->clear();
  msg->set_type(RpcType::DEL);
  msg->rpc_tag = rpc_tag;

  auto target = get_target(key);
  msg->add<KeySizeType>(key.len);
  msg->add<ValueSizeType>(0);
  msg->add_string_wo_size(key.s, key.len);

  dsm->rpc_call(msg, target.first, target.second, Slice::Void(), false);
}

bool KVClient::del(const Slice &key) {

  del_async(key);

  auto resp = dsm->rpc_wait();

  if (resp->type == RpcType::RESP_DEL_OK) {
    return true;
  }

  if (resp->type == RpcType::RESP_DEL_NOT_FOUND) {
    return false;
  }

  fprintf(stderr, "del error\n");
  exit(-1);
}

void KVClient::stats() {}
} // namespace kv
