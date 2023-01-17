#include "hash_api.h"
#include <libvmem.h>
#include <stdlib.h>
unsigned long PM_POOL_SZ = 32UL * 1024 * 1024 * 1024;

VMEM *vmp;

void creatPM(const char *dir, size_t size) {
  std::string cmd("mkdir ");
  cmd = cmd + dir;
  int ret = system(cmd.c_str());
  vmp = vmem_create(dir, size);
  if (vmp == nullptr) {
    std::cerr << "create vmem failure " << dir << "; size=" << size
              << std::endl;
    std::exit(1);
  }
}

void deletePM() { vmem_delete(vmp); }
void vmem_print() { vmem_stats_print(vmp, "1"); }