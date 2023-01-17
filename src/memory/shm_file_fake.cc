#error dont use this file
#include "shm_kv/shm_file.h"

namespace base {

bool ShmFile::Initialize(const std::string &filename, int64 size) {

  {
    static std::mutex m;
    std::lock_guard<std::mutex> _(m);
    data_ =
        (char *)PMMmapRegisterCenter::GetInstance()->Register(filename, size);
    filename_ = filename;
  }

  if (!std::filesystem::exists(filename)) {
    std::filesystem::create_directory(
        std::filesystem::path(filename).parent_path());
    LOG(INFO) << "Create ShmFile: " << filename << ", size: " << size;
    std::ofstream output(filename);
    output.write("a", 1);
    output.close();
  }
  size_ = size;
  return true;
}

void ShmFile::Clear() {}
} // namespace base