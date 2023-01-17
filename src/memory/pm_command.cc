#include "memory/shm_file.h"

#include <folly/init/Init.h>

DEFINE_string(command, "----", "reinit");
DEFINE_int32(numa_id, 0, "");

int main(int argc, char **argv) {
  folly::Init(&argc, &argv);
  if (FLAGS_command == "reinit") {
    base::PMMmapRegisterCenter::GetConfig().use_dram = false;
    base::PMMmapRegisterCenter::GetConfig().numa_id = FLAGS_numa_id;
    base::PMMmapRegisterCenter::GetInstance()->ReInitialize();
    auto command = folly::sformat("rm -rf /media/aep0/*;"
                                  "rm -rf /media/aep1/*;");
    LOG(WARNING) << command;
    system(command.c_str());
  } else {
    LOG(FATAL) << "false command";
  }
  return 0;
}
