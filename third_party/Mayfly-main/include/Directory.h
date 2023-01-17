#ifndef __DIRECTORY_H__
#define __DIRECTORY_H__

#include <thread>

#include <unordered_map>

#include "Common.h"

#include "Connection.h"
#include "Rdma.h"

namespace kv {
class SegmentAlloc;
}

class DSM;

class Directory {
public:
  Directory(DirectoryConnection *dCon, RemoteConnection *remoteInfo, DSM *dsm,
            uint16_t dirID, uint16_t nodeID);

  ~Directory();

  bool ready() { return is_run.load(); }


private:

  DirectoryConnection *dCon;
  RemoteConnection *remoteInfo;

  DSM *dsm;
  uint16_t dirID;
  uint16_t nodeID;

  std::thread *dirTh;

  std::atomic_bool is_run;

  kv::SegmentAlloc *segment_alloc;

  void dirThread();

  void sendData2App(const RawMessage *m);

  void process_message(const RawMessage *m);

public:

};

#endif /* __DIRECTORY_H__ */
