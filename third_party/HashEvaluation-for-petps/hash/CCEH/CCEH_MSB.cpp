

#include "CCEH.h"
#include "../common/hash.h"
#include "../common/persist.h"

using namespace cceh_allocator;
using namespace hasheval;

#define NOUSEVMEM 1

extern size_t perfCounter;
hash_api *create_hashtable_ccehvm(const hashtable_options_t &opt, unsigned sz,
                                 unsigned tnum) {

  bool file_exist = cceh_allocator::FileExists(opt.pool_path.c_str());

  hash_api * hash_table_ = nullptr;

  // NOTE(fyy)
#ifdef NOUSEVMEM
  cceh_allocator::Allocator::Initialize(opt.pool_path.c_str(), opt.pool_size);
  std::cout << opt.pool_path <<  " " << opt.pool_size << std::endl;

  hash_table_ = reinterpret_cast<hash_api *>(cceh_allocator::Allocator::GetRoot(
          sizeof(CCEH)));

  if(file_exist) {
    return new (hash_table_) CCEH();
  }
  
#else
  std::cout << opt.pool_path <<  " " << opt.pool_size << std::endl;
  creatPM(opt.pool_path.c_str(), opt.pool_size);
#endif
  
  if (sz)
    sz = sz / Segment::kNumSlot;
  else
    sz = 65536;
  return new (hash_table_) CCEH (sz >= 2 ? sz : 2);
}

int Segment::Insert(Key_t &key, Value_t value, size_t loc, size_t key_hash) {
#ifdef INPLACE
  if ((volatile int64_t)sema == -1) return 3;
  if ((key_hash >> (8 * sizeof(key_hash) - local_depth)) != pattern) return 2;
  auto lock = sema;
  int ret = 1;
  while (!CAS(&sema, &lock, lock + 1)) {
    lock = sema;
  }
  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (loc + i) % Segment::kNumSlot;
    if (_[slot].key == key) {
      ret = -1;
      lock = sema;
      while (!CAS(&sema, &lock, lock - 1)) {
        lock = sema;
      }
      return ret;
    }
  }
  Key_t LOCK = INVALID;
  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (loc + i) % kNumSlot;
    auto _key = _[slot].key;
    if ((h(&_[slot].key, sizeof(Key_t)) >>
         (8 * sizeof(key_hash) - local_depth)) != pattern) {
      CAS(&_[slot].key, &_key, INVALID);
    }
    if (CAS(&_[slot].key, &LOCK, SENTINEL)) {
      _[slot].value = value;
      mfence();
      _[slot].key = key;
      ret = 0;
      break;
    } else {
      LOCK = INVALID;
    }
  }
  lock = sema;
  while (!CAS(&sema, &lock, lock - 1)) {
    lock = sema;
  }
  return ret;
#else
  if (sema == -1) return 2;
  if ((key_hash >> (8 * sizeof(key_hash) - local_depth)) != pattern) return 2;
  auto lock = sema;
  int ret = 1;
  while (!CAS(&sema, &lock, lock + 1)) {
    lock = sema;
  }
  Key_t LOCK = INVALID;
  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (loc + i) % kNumSlot;
    if (CAS(&_[slot].key, &LOCK, SENTINEL)) {
      _[slot].value = value;
      mfence();
      _[slot].key = key;
      ret = 0;
      break;
    } else {
      LOCK = INVALID;
    }
  }
  lock = sema;
  while (!CAS(&sema, &lock, lock - 1)) {
    lock = sema;
  }
  return ret;
#endif
}

void Segment::Insert4split(Key_t &key, Value_t value, size_t loc) {
  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (loc + i) % kNumSlot;
    if (_[slot].key == INVALID) {
      _[slot].key = key;
      _[slot].value = value;
      return;
    }
  }
}

Segment **Segment::Split(void) {
  using namespace std;
  int64_t lock = 0;
  if (!CAS(&sema, &lock, -1)) return nullptr;

#ifdef INPLACE
  Segment **split = new Segment *[2];
  split[0] = this;
  split[1] = new Segment(local_depth + 1);
  split[1]->sema = 0;
  split[1]->local_depth = local_depth + 1;
  for (unsigned i = 0; i < kNumSlot; ++i) {
    auto key_hash = h(&_[i].key, sizeof(Key_t));
    if (key_hash & ((size_t)1 << ((sizeof(Key_t) * 8 - local_depth - 1)))) {
      split[1]->Insert4split(_[i].key, _[i].value,
                             (key_hash & kMask) * kNumPairPerCacheLine);
    }
  }

  clflush((char *)split[1], sizeof(Segment));
  local_depth = local_depth + 1;
  clflush((char *)&local_depth, sizeof(size_t));

  return split;
#else

  Segment **split;
  cceh_allocator::Allocator::Allocate((void **) &split, sizeof(void *) * 2);
  split[0] = new Segment(local_depth + 1);
  split[1] = new Segment(local_depth + 1);

  for (unsigned i = 0; i < kNumSlot; ++i) {
    auto key_hash = h(&_[i].key, sizeof(Key_t));
    if (key_hash & ((size_t)1 << ((sizeof(Key_t) * 8 - local_depth - 1)))) {
      split[1]->Insert4split(_[i].key, _[i].value,
                             (key_hash & kMask) * kNumPairPerCacheLine);
    } else {
      split[0]->Insert4split(_[i].key, _[i].value,
                             (key_hash & kMask) * kNumPairPerCacheLine);
    }
  }
  clflush((char *)split, sizeof(Segment**));
  clflush((char *)split[0], sizeof(Segment));
  clflush((char *)split[1], sizeof(Segment));
  return split;
#endif
}

#ifdef NOUSEVMEM
CCEH::CCEH(void) {
  std::cout << "Reinitialize up" << std::endl;
  printf("dir: %p\n", dir);
  printf("dir->_: %p\n", dir->_);
  printf("dir->_[0]: %p\n", dir->_[0]);
}
#else
CCEH::CCEH(void) : dir{new Directory(0)} {
  for (unsigned i = 0; i < dir->capacity; ++i) {
    dir->_[i] = new Segment(0);
    dir->_[i]->pattern = i;
  }
}
#endif

CCEH::CCEH(size_t initCap)
    : dir{new Directory(static_cast<size_t>(log2(initCap)))} {
  for (unsigned i = 0; i < dir->capacity; ++i) {
    dir->_[i] = new Segment(static_cast<size_t>(log2(initCap)));
    dir->_[i]->pattern = i;
    // clflush((char *)&dir->_[i], sizeof(Segment *));
    // clflush((char *)&(dir->_[i]->pattern), sizeof(size_t));
  }
  // clflush((char *) & dir, sizeof(Directory *));

  printf("dir: %p\n", dir);
  printf("dir->_: %p\n", dir->_);
  printf("dir->_[0]: %p\n", dir->_[0]);
}

void CCEH::exist_flush(void)
{
  printf("-------------------------\n");
  clflush((char *)dir, sizeof(Directory));
  clflush((char *)& dir, sizeof(Directory *));
  clflush((char *)& (dir->_), sizeof(Segment **));
  clflush((char *) dir->_, sizeof(Segment *));
  clflush((char *)& dir->_[0], sizeof(void *) * dir->capacity);

  // flush all secment???

  printf("dir: %p\n", dir);
  printf("dir->_: %p\n", dir->_);
  printf("*dir->_: %p\n", *dir->_);
  printf("dir->_[0]: %p\n", dir->_[0]);
}

void Directory::LSBUpdate(int local_depth, int global_depth, int dir_cap, int x,
                          Segment **s) {
  int depth_diff = global_depth - local_depth;
  if (depth_diff == 0) {
    if ((x % dir_cap) >= dir_cap / 2) {
      _[x - dir_cap / 2] = s[0];
      clflush((char *)&_[x - dir_cap / 2], sizeof(Segment *));
      _[x] = s[1];
      clflush((char *)&_[x], sizeof(Segment *));
    } else {
      _[x] = s[0];
      clflush((char *)&_[x], sizeof(Segment *));
      _[x + dir_cap / 2] = s[1];
      clflush((char *)&_[x + dir_cap / 2], sizeof(Segment *));
    }
  } else {
    if ((x % dir_cap) >= dir_cap / 2) {
      LSBUpdate(local_depth + 1, global_depth, dir_cap / 2, x - dir_cap / 2, s);
      LSBUpdate(local_depth + 1, global_depth, dir_cap / 2, x, s);
    } else {
      LSBUpdate(local_depth + 1, global_depth, dir_cap / 2, x, s);
      LSBUpdate(local_depth + 1, global_depth, dir_cap / 2, x + dir_cap / 2, s);
    }
  }
  return;
}

bool CCEH::Insert(Key_t &key, Value_t value) {
  bool resized = false;
STARTOVER:
  auto key_hash = h(&key, sizeof(key));
  auto y = (key_hash & kMask) * kNumPairPerCacheLine;

RETRY:
  auto x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
  auto target = dir->_[x];
  // printf("~~~~~~~~~~~~ %p, %d\n", target, (int)x);
  if(target == 0) {
    printf("~~~~~~~~~~~~ %p, %d\n", target, (int)x);
  }
  auto ret = target->Insert(key, value, y, key_hash);

  if (ret == 1) {
    resized = true;
    Segment **s = target->Split();
    if (s == nullptr) {
      // another thread is doing split
      // std::cout << "sema:" << target->sema << std::endl;
      goto RETRY;
    }

    s[0]->pattern = (key_hash >> (8 * sizeof(key_hash) - s[0]->local_depth + 1))
                    << 1;
    s[1]->pattern =
        ((key_hash >> (8 * sizeof(key_hash) - s[1]->local_depth + 1)) << 1) + 1;

    // Directory management
    while (!dir->Acquire()) {
      asm("nop");
    }
    {  // CRITICAL SECTION - directory update
      x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
#ifdef INPLACE
      if (dir->_[x]->local_depth - 1 < dir->depth) {  // normal split
#else
      if (dir->_[x]->local_depth < dir->depth) {  // normal split
#endif
        unsigned depth_diff = dir->depth - s[0]->local_depth;
        if (depth_diff == 0) {
          if (x % 2 == 0) {
            dir->_[x + 1] = s[1];
#ifdef INPLACE
            clflush((char *)&dir->_[x + 1], 8);
#else
            mfence();
            dir->_[x] = s[0];
            clflush((char *)&dir->_[x], 16);
#endif
          } else {
            dir->_[x] = s[1];
#ifdef INPLACE
            clflush((char *)&dir->_[x], 8);
#else
            mfence();
            dir->_[x - 1] = s[0];
            clflush((char *)&dir->_[x - 1], 16);
#endif
          }
        } else {
          int chunk_size = pow(2, dir->depth - (s[0]->local_depth - 1));
          x = x - (x % chunk_size);
          for (unsigned i = 0; i < chunk_size / 2; ++i) {
            dir->_[x + chunk_size / 2 + i] = s[1];
          }
          clflush((char *)&dir->_[x + chunk_size / 2],
                  sizeof(void *) * chunk_size / 2);
#ifndef INPLACE
          for (unsigned i = 0; i < chunk_size / 2; ++i) {
            dir->_[x + i] = s[0];
          }
          clflush((char *)&dir->_[x], sizeof(void *) * chunk_size / 2);
#endif
        }
        while (!dir->Release()) {
          asm("nop");
        }
      } else {  // directory doubling
        resized = true;
        auto dir_old = dir;
        auto d = dir->_;
        // auto _dir = new Segment*[dir->capacity*2];
        auto _dir = new Directory(dir->depth + 1);
        printf("_dir->_ %p\n", _dir->_);
        for (unsigned i = 0; i < dir->capacity; ++i) {
          if (i == x) {
            _dir->_[2 * i] = s[0];
            _dir->_[2 * i + 1] = s[1];
          } else {
            _dir->_[2 * i] = d[i];
            _dir->_[2 * i + 1] = d[i];
          }
        }
        clflush((char *)&_dir->_[0], sizeof(Segment *) * _dir->capacity);
        clflush((char *)&_dir->_, sizeof(Segment **));
        clflush((char *)&_dir, sizeof(Directory));
        dir = _dir;
        clflush((char *)&dir, sizeof(void *));

        cceh_allocator::Allocator::Free(dir_old);
        // vmem_free(vmp, dir_old);
      }
#ifdef INPLACE
      s[0]->sema = 0;
#endif
    }  // End of critical section
    // std::cout << 2 << std::endl;
    goto RETRY;
  } else if (ret == 2) {
    // std::cout << 3 << std::endl;
    goto STARTOVER;
  } else if (ret == -1) {
    ;
  } else {
    clflush((char *)&dir->_[x]->_[y], 64);
  }
  return resized;
}

// This function does not allow resizing
bool CCEH::InsertOnly(Key_t &key, Value_t value) {
  auto key_hash = h(&key, sizeof(key));
  auto x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
  auto y = (key_hash & kMask) * kNumPairPerCacheLine;

  auto ret = dir->_[x]->Insert(key, value, y, key_hash);
  if (ret == 0) {
    clflush((char *)&dir->_[x]->_[y], 64);
    return true;
  }

  return false;
}

bool CCEH::Delete(Key_t &key) {
  auto key_hash = h(&key, sizeof(key));
  auto x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
  auto y = (key_hash & kMask) * kNumPairPerCacheLine;

  auto dir_ = dir->_[x];

#ifdef INPLACE
  auto sema = dir->_[x]->sema;
  while (!CAS(&dir->_[x]->sema, &sema, sema + 1)) {
    sema = dir->_[x]->sema;
  }
#endif

  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (y + i) % Segment::kNumSlot;
    if (dir_->_[slot].key == key) {
      dir_->_[slot].key = INVALID;
      break;
    }
  }

#ifdef INPLACE
  sema = dir->_[x]->sema;
  while (!CAS(&dir->_[x]->sema, &sema, sema - 1)) {
    sema = dir->_[x]->sema;
  }
#endif
  clflush((char *)&dir->_[x]->_[y], 8);
  return NONE;
}

Value_t CCEH::Get(Key_t &key) {
  auto key_hash = h(&key, sizeof(key));
  auto x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
  auto y = (key_hash & kMask) * kNumPairPerCacheLine;

  auto dir_ = dir->_[x];

#ifdef INPLACE
  auto sema = dir_->sema;
  while (!CAS(&dir_->sema, &sema, sema + 1)) {
    sema = dir_->sema;
  }
#endif

  for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
    auto slot = (y + i) % Segment::kNumSlot;
    if (dir_->_[slot].key == key) {
#ifdef INPLACE
      sema = dir_->sema;
      while (!CAS(&dir_->sema, &sema, sema - 1)) {
        sema = dir_->sema;
      }
#endif
      return dir_->_[slot].value;
    }
  }

#ifdef INPLACE
  sema = dir_->sema;
  while (!CAS(&dir_->sema, &sema, sema - 1)) {
    sema = dir_->sema;
  }
#endif
  return NONE;
}

hash_Utilization CCEH::Utilization(void) {
  size_t sum = 0;
  std::unordered_map<Segment *, bool> set;
  for (size_t i = 0; i < dir->capacity; ++i) {
    set[dir->_[i]] = true;
  }
  for (auto &elem : set) {
    for (unsigned i = 0; i < Segment::kNumSlot; ++i) {
#ifdef INPLACE
      auto key_hash = h(&elem.first->_[i].key, sizeof(elem.first->_[i].key));
      if (key_hash >> (8 * sizeof(key_hash) - elem.first->local_depth) ==
          elem.first->pattern)
        if (elem.first->_[i].key != (unsigned long long)-1) sum++;
#else
      if (elem.first->_[i].key != INVALID) sum++;
#endif
    }
  }
  // std::cout << "Sum:" << sum << " Size:" << set.size() * Segment::kNumSlot
  //           << std::endl;
  hash_Utilization h;
  h.load_factor =
      ((float)sum) / ((float)set.size() * Segment::kNumSlot) * 100.0;
  h.utilization =
      ((float)sum * 16) /
      (set.size() * sizeof(Segment) + sizeof(Segment *) * dir->capacity) *
      100.0;
  return h;
}

size_t CCEH::Capacity(void) {
  std::unordered_map<Segment *, bool> set;
  for (size_t i = 0; i < dir->capacity; ++i) {
    set[dir->_[i]] = true;
  }
  return set.size() * Segment::kNumSlot;
}

size_t Segment::numElem(void) {
  size_t sum = 0;
  for (unsigned i = 0; i < kNumSlot; ++i) {
    if (_[i].key != INVALID) {
      sum++;
    }
  }
  return sum;
}

bool CCEH::Recovery(void) {
  bool recovered = false;
  size_t i = 0;
  while (i < dir->capacity) {
    size_t depth_cur = dir->_[i]->local_depth;
    size_t stride = pow(2, dir->depth - depth_cur);
    size_t buddy = i + stride;
    if (buddy == dir->capacity) break;
    for (int j = buddy - 1; i < j; j--) {
      if (dir->_[j]->local_depth != depth_cur) {
        dir->_[j] = dir->_[i];
      }
    }
    i = i + stride;
  }
  if (recovered) {
    clflush((char *)&dir->_[0], sizeof(void *) * dir->capacity);
  }
  return recovered;
}

// for debugging
Value_t CCEH::FindAnyway(Key_t &key) {
  using namespace std;
  for (size_t i = 0; i < dir->capacity; ++i) {
    for (size_t j = 0; j < Segment::kNumSlot; ++j) {
      if (dir->_[i]->_[j].key == key) {
        auto key_hash = h(&key, sizeof(key));
        auto x = (key_hash >> (8 * sizeof(key_hash) - dir->depth));
        auto y = (key_hash & kMask) * kNumPairPerCacheLine;
        cout << bitset<32>(i) << endl
             << bitset<32>((x >> 1)) << endl
             << bitset<32>(x) << endl;
        return dir->_[i]->_[j].value;
      }
    }
  }
  return NONE;
}

void Directory::SanityCheck(void *addr) {
  using namespace std;
  for (unsigned i = 0; i < capacity; ++i) {
    if (_[i] == addr) {
      cout << i << " " << _[i]->sema << endl;
      exit(1);
    }
  }
}
