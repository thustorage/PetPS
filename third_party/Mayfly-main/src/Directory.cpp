#include "Directory.h"
#include "Common.h"

#include "Connection.h"
#include "SegmentAlloc.h"
#include "Timer.h"

#include "Global.h"

#include "DSM.h"


Directory::Directory(DirectoryConnection *dCon, RemoteConnection *remoteInfo,
                     DSM *dsm, uint16_t dirID, uint16_t nodeID)
    : dCon(dCon), remoteInfo(remoteInfo), dsm(dsm), dirID(dirID),
      nodeID(nodeID), dirTh(nullptr) {

  is_run.store(false);

  segment_alloc = nullptr;

  dirTh = new std::thread(&Directory::dirThread, this);
}

Directory::~Directory() {}

void Directory::dirThread() {

}

void Directory::process_message(const RawMessage *m) {

  RawMessage *send = nullptr;
  switch (m->type) {
    // case RpcType::MALLOC: {

    //   send = (RawMessage *)dCon->message->getSendPool();

    //   // send->addr = chunckAlloc->alloc_chunck();
    //   memcpy(send, m, sizeof(RawMessage));
    //   break;
    // }

  default:
    assert(false);
  }

  if (send) {
    dCon->sendMessage2App(send, m->node_id, m->t_id);
  }
}

// void Directory::sendData2App(const RawMessage *m) {
//   rdmaWrite(dCon->data2app[m->appID][m->nodeID], (uint64_t)dCon->dsmPool,
//             m->destAddr, 1024, dCon->dsmLKey,
//             remoteInfo[m->nodeID].appRKey[m->appID], 11, true, 0);
// }
