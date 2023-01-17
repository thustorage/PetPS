#ifndef __RAWMESSAGECONNECTION_H__
#define __RAWMESSAGECONNECTION_H__

#include "AbstractMessageConnection.h"
#include "CRC32.h"
#include "Common.h"
#include "GlobalAddress.h"

#include <thread>

enum RpcType : uint8_t {

  // client -> server
  GET,
  PUT,
  DEL,
  GET_SERVER_THREADIDS,

  // server -> client
  RESP_GET_OK,
  RESP_GET_NOT_FOUND,
  RESP_PUT,
  RESP_DEL_OK,
  RESP_DEL_NOT_FOUND,
  RESP_GET_SERVER_THREADIDS,

};

struct Cursor {
  uint32_t off;

  Cursor() : off(0) {}
};

struct RawMessage {
  RpcType type;
  NodeIDType node_id;
  ThreadIDType t_id;
  uint8_t rpc_tag;
  GlobalAddress receive_gaddr;
  uint16_t msg_size{0};
  char msg[0];

  void print() {
    printf("type %d, [%d %d] [size: %d] ", type, node_id, t_id, msg_size);
    for (size_t i = 0; i < msg_size; ++i) {
      printf("%c-", msg[i]);
    }
    printf("\n");
  }

  size_t size() const { return sizeof(RawMessage) + this->msg_size; }

  void clear() { msg_size = 0; }

  void set_type(RpcType t) { type = t; }

  template <class T> void add(const T &v) {
    *(T *)(msg + msg_size) = v;
    msg_size += sizeof(T);
  }

  void add_string_wo_size(const char *s, uint32_t k) {
    memcpy(msg + msg_size, s, k);
    msg_size += k;
  }

  void add_string_wo_size(Slice s) { add_string_wo_size(s.s, s.len); }

  void add_string(const char *s, uint32_t k) {
    add<uint32_t>(k);
    memcpy(msg + msg_size, s, k);
    msg_size += k;
  }

  void add_string(Slice s) { add_string(s.s, s.len); }

  template <class T> T get(Cursor &cur) {
    T k = *(T *)(msg + cur.off);
    cur.off += sizeof(T);

    return k;
  }

  Slice get_string_wo_size(Cursor &cur, size_t size) {
    Slice res(msg + cur.off, size);
    cur.off += size;

    return res;
  }

  Slice get_string(Cursor &cur) {
    uint32_t k = *(uint32_t *)(msg + cur.off);
    Slice res(msg + cur.off + sizeof(uint32_t), k);

    cur.off += sizeof(uint32_t) + k;

    return res;
  }

  RawMessage *th(size_t k) { // msg array
    return (RawMessage *)((char *)this + MESSAGE_SIZE * k);
  }

  static RawMessage *get_new_msg() {
    RawMessage *m = (RawMessage *)malloc(MESSAGE_SIZE);
    m->msg_size = 0;

    return m;
  }

  static RawMessage *get_new_msg_arr(size_t k) {
    RawMessage *m = (RawMessage *)malloc(MESSAGE_SIZE * k);

    for (size_t i = 0; i < k; ++i) {
      auto *e = (RawMessage *)((char *)m + MESSAGE_SIZE * i);
      e->msg_size = 0;
    }

    return m;
  }

  static void put_new_msg(RawMessage *m) { free(m); }
  static void put_new_msg_arr(RawMessage *m) { free(m); }

private:
  RawMessage() { printf("why call this constructor?\n"); }

} __attribute__((packed));

class RawMessageConnection : public AbstractMessageConnection {

public:
  RawMessageConnection(RdmaContext &ctx, ibv_cq *cq, uint32_t messageNR);

  void initSend();
  void sendRawMessage(RawMessage *m, uint32_t remoteQPN, ibv_ah *ah);

  void sendRawMessageWithData(RawMessage *m, Slice data, uint32_t data_lkey,
                              uint32_t remoteQPN, ibv_ah *ah);
};

#endif /* __RAWMESSAGECONNECTION_H__ */
