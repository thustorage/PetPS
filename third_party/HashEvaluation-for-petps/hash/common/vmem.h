#pragma once
#include <libvmem.h>
extern unsigned long PM_POOL_SZ;
extern VMEM *vmp;
void creatPM(const char *dir, size_t size);
void deletePM();
void vmem_print();