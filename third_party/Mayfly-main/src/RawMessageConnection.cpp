#include "RawMessageConnection.h"

#include <cassert>

RawMessageConnection::RawMessageConnection(RdmaContext &ctx, ibv_cq *cq,
                                           uint32_t messageNR)
    : AbstractMessageConnection(IBV_QPT_UD, 0, 40, ctx, cq, messageNR) {}

void RawMessageConnection::initSend() {}

void RawMessageConnection::sendRawMessage(RawMessage *m, uint32_t remoteQPN,
                                          ibv_ah *ah) {

  if ((sendCounter & SIGNAL_BATCH) == 0 && sendCounter > 0) {
    ibv_wc wc;
    pollWithCQ(send_cq, 1, &wc);
  }

  rdmaSend(message, (uint64_t)m - sendPadding, m->size() + sendPadding,
           messageLkey, ah, remoteQPN, (sendCounter & SIGNAL_BATCH) == 0);

  ++sendCounter;
}

void RawMessageConnection::sendRawMessageWithData(RawMessage *m, Slice data,
                                                  uint32_t data_lkey,
                                                  uint32_t remoteQPN,
                                                  ibv_ah *ah) {

  if ((sendCounter & SIGNAL_BATCH) == 0 && sendCounter > 0) {
    ibv_wc wc;
    pollWithCQ(send_cq, 1, &wc);
  }

  auto message_size = m->msg_size;
  m->msg_size += data.len; // include data
  rdmaSend2Sge(message, (uint64_t)m - sendPadding, message_size + sendPadding,
               (uint64_t)data.s, data.len, messageLkey, data_lkey, ah,
               remoteQPN, (sendCounter & SIGNAL_BATCH) == 0);

  ++sendCounter;
}
