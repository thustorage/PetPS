#include "DPUService.h"
#include "DSM.h"
#include "HostService.h"

// ./server server_nr client_nr server_thread_nr
int main(int argc, char **argv) {

  if (argc != 4 && argc != 5) {
    fprintf(stderr, "Usage: ./server server_nr client_nr server_thread_nr\n");
    exit(-1);
  }

  uint32_t server_nr = std::atoi(argv[1]);
  uint32_t client_nr = std::atoi(argv[2]);
  uint32_t server_thread_nr = std::atoi(argv[3]);

  ClusterInfo cluster;
  cluster.serverNR = server_nr;
  cluster.clientNR = client_nr;

  DSMConfig config(CacheConfig(), cluster, 8, false);
  DSM *dsm = DSM::getInstance(config);

  kv::DPUService *dpu_service = nullptr;
  kv::HostService *host_service;
  printf("Server is running...\n");
  host_service = new kv::HostService(dsm, server_thread_nr);

  (void)host_service;

  while (true) {
    sleep(1024);
  }
}