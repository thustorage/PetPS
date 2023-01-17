#include "DSM.h"
#include "Timer.h"
#include "zipf.h"

#include <thread>

/*
./restartMemc.sh && /usr/local/openmpi/bin/mpiexec --allow-run-as-root -hostfile
./host  -np 9 ./atomic_inbound 9 4 0 0 8
*/

const int kMaxTestThread = 24;
const int kBucketPerThread = 32;

std::thread th[kMaxTestThread];
uint64_t tp_counter[kMaxTestThread][8];
uint64_t tp_write_counter[kMaxTestThread][8];
DSM *dsm;

int node_nr, my_node;
int thread_nr;

void send_write(int node_id, int thread_id) {

  bind_core(thread_id);
  dsm->registerThread();

  RawMessage *m = RawMessage::get_new_msg();
  // m.type = ;

  dsm->barrier("XXX");

  while (true) {

    if (dsm->getMyNodeID() == 0) {
      printf("call \n");
      dsm->rpc_call(m, 1, 0);
      printf("call 2\n");
      auto mm = dsm->rpc_wait();
      printf("XXX from node %d, t_id %d\n", mm->node_id, mm->t_id);
    } else {
      printf("recv \n");
      auto mm = dsm->rpc_wait();
      printf("YYY from node %d, t_id %d\n", mm->node_id, mm->t_id);
      dsm->rpc_call(m, mm->node_id, mm->t_id);
    }
  }
}

void read_args(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: ./rpc_test node_nr thread_nr\n");
    exit(-1);
  }

  node_nr = std::atoi(argv[1]);
  thread_nr = std::atoi(argv[2]);

  printf("node_nr [%d], thread_nr [%d]\n", node_nr, thread_nr);
}

extern uint64_t dir_counter;

int main(int argc, char **argv) {

  bind_core(0);

  read_args(argc, argv);

  DSMConfig config(CacheConfig(), 2);
  dsm = DSM::getInstance(config);

  // Timer timer;
  // uint64_t pre_counter = 0;
  // if (dsm->getMyNodeID() == 0) { // server

  //   timer.begin();
  //   while (true) {

  //     Timer::sleep(1000ull * 1000 * 1000);
  //     auto ns = timer.end();
  //     timer.begin();

  //     auto gap = dir_counter - pre_counter;
  //     pre_counter = dir_counter;
  //     printf("tp: %.2lf\n", gap * 1.0 / (1.0 * ns / 1000 / 1000 / 1000));
  //   }
  // }

  bind_core(20);

  for (int i = 0; i < thread_nr; ++i) {
    ;
    th[i] = std::thread(send_write, dsm->getMyNodeID(), i);
  }

  while (true) {
  }
}