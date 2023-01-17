#include "DSM.h"
#include "Timer.h"
#include "zipf.h"

#include <thread>

const int kMaxTestThread = 24;
const int kBucketPerThread = 32;

std::thread th[kMaxTestThread];
uint64_t tp_write_counter[kMaxTestThread][8];
DSM *dsm;

int node_nr, my_node;
int thread_nr;
int kPacketSize = 16;

void send_write(int node_id, int thread_id) {

  const int kDifferLocation = 256;

  bind_core(thread_id);
  dsm->registerThread();

  uint64_t sendCounter = 0;
  char *buffer = dsm->get_rdma_buffer();
  size_t buffer_size = kPacketSize * kDifferLocation;

  GlobalAddress gaddr;
  gaddr.nodeID = thread_id % (node_nr - 1);

  memset(buffer, 0, buffer_size);
  const uint64_t offset_start =
      node_id * (thread_nr * buffer_size) + buffer_size * thread_id;
  printf("[%lx %lx)\n", offset_start, offset_start + buffer_size);

  while (true) {
    if ((sendCounter & SIGNAL_BATCH) == 0 && sendCounter > 0) {
      dsm->poll_rdma_cq(1);
      tp_write_counter[thread_id][0] += SIGNAL_BATCH + 1;
    }

    int kTh = sendCounter % kDifferLocation;

    gaddr.offset = kTh * kPacketSize;

    dsm->read(buffer + gaddr.offset, gaddr, kPacketSize,
              (sendCounter & SIGNAL_BATCH) == 0);

    ++sendCounter;
  }
}

void read_args(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: ./outbound node_nr thread_nr packet_size\n");
    exit(-1);
  }

  node_nr = std::atoi(argv[1]);
  thread_nr = std::atoi(argv[2]);
  kPacketSize = std::atof(argv[3]);

  printf("node_nr [%d], thread_nr [%d], packsize [%d]\n", node_nr, thread_nr,
         kPacketSize);
}

int main(int argc, char **argv) {

  bind_core(0);

  read_args(argc, argv);

  DSMConfig config(CacheConfig(), node_nr);
  dsm = DSM::getInstance(config);

  if (dsm->getMyNodeID() != node_nr - 1) {
    while (true)
      ;
  }

  bind_core(20);

  for (int i = 0; i < thread_nr; ++i) {
    th[i] = std::thread(send_write, dsm->getMyNodeID(), i);
  }

  Timer timer;

  while (true) {
    timer.begin();

    sleep(1);

    auto ns = timer.end();

    uint64_t write_tp = 0;
    for (int i = 0; i < thread_nr; ++i) {
      write_tp += tp_write_counter[i][0];
      tp_write_counter[i][0] = 0;
    }

    double data_tp = write_tp * 1.0 / (1.0 * ns / 1000 / 1000 / 1000);
    printf("node %d, data tp %.2lf\n", dsm->getMyNodeID(), data_tp);
  }
}