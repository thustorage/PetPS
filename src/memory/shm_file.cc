#include "shm_file.h"

#include <algorithm>
#include <iostream>

namespace base {

bool ShmFile::InitializeFsDax(const std::string &filename, int64 size) {
  ClearFsDax();
  if (!fs::exists(filename)) {
    fs::create_directory(fs::path(filename).parent_path());
    LOG(INFO) << "Create ShmFile: " << filename << ", size: " << size;

    system(folly::sformat("fallocate -l {} {}", size, filename).c_str());
  }

  size_ = fs::file_size(filename);

  if (size_ != size) {
    LOG(ERROR) << "Size Error: " << size_ << " vs " << size;
    return false;
  }

  fd_ = open(filename.c_str(), 0666);
  if (fd_ < 0) {
    LOG(ERROR) << "Failed to open file " << filename << ": " << strerror(errno);
    return false;
  }

  data_ = reinterpret_cast<char *>(
      mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
  CHECK_NE(data_, MAP_FAILED) << "map failed";

  filename_ = filename;

  return true;
}

bool ShmFile::InitializeDevDax(const std::string &filename, int64 size) {
  {
    static std::mutex m;
    std::lock_guard<std::mutex> _(m);
    data_ =
        (char *)PMMmapRegisterCenter::GetInstance()->Register(filename, size);
    filename_ = filename;
  }

  if (!fs::exists(filename)) {
    fs::create_directory(fs::path(filename).parent_path());
    LOG(INFO) << "Create ShmFile: " << filename << ", size: " << size;
    std::ofstream output(filename);
    output.write("a", 1);
    output.close();
  }
  size_ = size;
  return true;
}

bool ShmFile::Initialize(const std::string &filename, int64 size) {
  if (fs::exists("/dev/dax0.0") || PMMmapRegisterCenter::GetConfig().use_dram) {
    LOG(INFO) << "ShmFile, devdax mode";
    return InitializeDevDax(filename, size);
  } else {
    LOG(INFO) << "ShmFile, fsdax mode";
    return InitializeFsDax(filename, size);
  }
}

void ShmFile::ClearDevDax() {}

void ShmFile::ClearFsDax() {
  if (fd_ >= 0) {
    LOG(INFO) << "ummap shm file: " << filename_ << ", size: " << size_
              << ", fd: " << fd_;
    filename_.clear();
    munmap(data_, size_);
    close(fd_);
    data_ = NULL;
    size_ = 0;
    fd_ = -1;
  }
}
void ShmFile::Clear() {
  if (fs::exists("/dev/dax0.0")) {
    ClearDevDax();
  } else {
    ClearFsDax();
  }
}

} // namespace base
