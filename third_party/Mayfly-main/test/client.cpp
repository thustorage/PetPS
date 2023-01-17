#include "DSM.h"
#include "Rdma.h"
#include "Timer.h"

DSM *dsm;
uint32_t client_thread_nr;
uint64_t io_size = 8;

uint64_t tp_counter[64][8];

uint64_t kAEPSize = 64 * define::MB;

#define RPC_TEST

void test(int thread_id) {

  dsm->registerThread();

  char *buf = dsm->get_rdma_buffer();

  *(buf) = kMagicNumer;

  const int pipeline_wdn = 8;

  GlobalAddress start;
  start.nodeID = 1;
  start.offset = (dsm->getMyNodeID() - dsm->get_conf()->cluster_info.serverNR) *
                     client_thread_nr * kAEPSize +
                 kAEPSize * thread_id + kAEPSize;

  (void)start;

  uint64_t cnt_per_loop = kAEPSize / io_size;
  uint64_t cur = 0;

  auto m = RawMessage::get_new_msg();
  for (int i = 0; i < pipeline_wdn; ++i) {

#ifdef RPC_TEST
    dsm->rpc_call(m, start.nodeID, cur++ % 8);
#else
    dsm->write(buf, GADD(start, io_size * cur), io_size);
    cur = (cur + 1) % cnt_per_loop;
#endif
  }
  while (true) {

#ifdef RPC_TEST
    dsm->rpc_wait(nullptr);
    dsm->rpc_call(m, start.nodeID, cur++ % 8);
#else
    ibv_wc wc;
    pollWithCQ(dsm->getThreadCon()->cq, 1, &wc);
    cur = (cur + 1) % cnt_per_loop;

    dsm->write(buf, GADD(start, io_size * cur), io_size);
#endif

    tp_counter[thread_id][0]++;
  }
}

// ./client server_nr client_nr client_thread_nr
int main(int argc, char **argv) {

  if (argc != 4) {
    fprintf(stderr, "Usage: ./client server_nr client_nr client_thread_nr\n");
    exit(-1);
  }

  uint32_t server_nr = std::atoi(argv[1]);
  uint32_t client_nr = std::atoi(argv[2]);
  client_thread_nr = std::atoi(argv[3]);

  ClusterInfo cluster;
  cluster.serverNR = server_nr;
  cluster.clientNR = client_nr;

  DSMConfig config(CacheConfig(), cluster, 1, true);
  dsm = DSM::getInstance(config);

  for (size_t i = 0; i < client_thread_nr; ++i) {
    new std::thread(test, i);
  }

  Timer timer;

  uint64_t pre_tp = 0;

  timer.begin();
  while (true) {

    sleep(1);

    auto ns = timer.end();

    uint64_t tp = 0;

    for (size_t i = 0; i < client_thread_nr; ++i) {
      tp += tp_counter[i][0];
    }

    double t_tp = (tp - pre_tp) * 1.0 / (1.0 * ns / 1000 / 1000 / 1000);
    pre_tp = tp;

    timer.begin();
    printf("IOPS: %lf  --- all %ld\n", t_tp, tp);
  }

  while (true) {

    /* code */
  }
}