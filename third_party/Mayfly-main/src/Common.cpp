#include "Common.h"

#include <arpa/inet.h>
#include <atomic>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

NodeConfig global_node_config[32] = {
    {'0', 3}, // server
    {'1', 3}, // clients
};

int global_socket_id = 0;
int core_table1[kMaxSocketCnt][kLogicCoreCnt] = {
    {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
     36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53},

    {18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
     54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71}};

int core_table2[kMaxSocketCnt][kLogicCoreCnt] = {
    {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
     24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35},

    {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
     36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47}};

void bind_core(uint16_t core) {

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    Debug::notifyError("can't bind core!");
  }
}

void auto_bind_core(int ServerConfig) {
  static std::atomic<int> cur_id{0};
  int core_idx = cur_id.fetch_add(1);
  if (ServerConfig == 0) {
    Debug::notifyInfo("bind to core %d",
                      core_table1[global_socket_id][core_idx]);
    bind_core(core_table1[global_socket_id][core_idx]);
  } else if (ServerConfig == 1) {
    Debug::notifyInfo("bind to core %d",
                      core_table2[global_socket_id][core_idx]);
    bind_core(core_table2[global_socket_id][core_idx]);
  } else {
    std::cerr << "no such config";
    *(int *)0 = 0;
  }
}

char *getIP() {
  struct ifreq ifr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "eno1", IFNAMSIZ - 1);

  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);

  return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

char *getMac() {
  static struct ifreq ifr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "ens2", IFNAMSIZ - 1);

  ioctl(fd, SIOCGIFHWADDR, &ifr);
  close(fd);

  return (char *)ifr.ifr_hwaddr.sa_data;
}

void execute_cmd(const char *cmd, char *result) {
  char buf_ps[1024];
  char ps[1024] = {0};
  FILE *ptr;
  strcpy(ps, cmd);
  if ((ptr = popen(ps, "r")) != NULL) {
    while (fgets(buf_ps, 1024, ptr) != NULL) {
      strcat(result, buf_ps);
      if (strlen(result) > 1024)
        break;
    }
    pclose(ptr);
    ptr = NULL;
  } else {
    printf("popen %s error\n", ps);
  }
}

uint64_t get_dax_physical_addr(int numa_id) {
  char result[64];

  (void)result;
  uint64_t ret = 0;
  if (numa_id == 0) {
    // execute_cmd("cat /sys/bus/nd/devices/dax0.1/resource", result);
    // sscanf(result, "%lx ", &ret);
    return 0x1b54200000;
  } else if (numa_id == 1) {
    // execute_cmd("cat /sys/bus/nd/devices/dax1.1/resource", result);
    // sscanf(result, "%lx ", &ret);
    return 0xeff4200000;
  }

  return ret;
}