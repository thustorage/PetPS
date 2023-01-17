#include "Level_hashing.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../common/hash.h"


using namespace std;

#define F_HASH(key) (h(&key, sizeof(Key_t), f_seed))
#define S_HASH(key) (h(&key, sizeof(Key_t), s_seed))
#define F_IDX(hash, capacity) (hash % (capacity / 2))
#define S_IDX(hash, capacity) ((hash % (capacity / 2)) + (capacity / 2))

void LevelHashing::generate_seeds(void)
{
  srand(time(NULL));

  do
  {
    f_seed = rand();
    s_seed = rand();
    f_seed = f_seed << (rand() % 63);
    s_seed = s_seed << (rand() % 63);
  } while (f_seed == s_seed);
}
// extern "C" hash_api *create_hashtable(const hashtable_options_t &opt, unsigned sz, unsigned tnum)
// {
//   if (sz)
//     sz = log2(2 * sz / 3 / ASSOC_NUM);
//   else
//     sz = 23;
//   return new LevelHashing(sz >= 2 ? sz : 2);
// }

hash_api *create_hashtable_level(const hashtable_options_t &opt, unsigned sz, unsigned tnum)
{
  #ifdef NOUSEVMEM
  std::cout << opt.pool_path << std::endl;
  bool file_exist = level_allocator::FileExists(opt.pool_path.c_str());
  hash_api * hash_table_ = nullptr;

  level_allocator::Allocator::Initialize(opt.pool_path.c_str(), opt.pool_size);
  hash_table_ = reinterpret_cast<hash_api *>(level_allocator::Allocator::GetRoot(
          sizeof(LevelHashing)));

  if(file_exist) {
    return new (hash_table_) LevelHashing();
  }

  if (sz)
    sz = log2(2 * sz / 3 / ASSOC_NUM);
  else
    sz = 23;
  return new (hash_table_) LevelHashing(sz >= 2 ? sz : 2);

  #else
  creatPM(opt.pool_path.c_str(), opt.pool_size);

  if (sz)
    sz = log2(2 * sz / 3 / ASSOC_NUM);
  else
    sz = 23;
  return new LevelHashing(sz >= 2 ? sz : 2);

  #endif
}


LevelHashing::LevelHashing(void) {

  
  std::cout << "Reinitialize up" << std::endl;

  locksize = 256;
  nlocks = (3 * addr_capacity / 2) / locksize + 1;
  mutex = new std::shared_mutex[nlocks];
}


bool LevelHashing::Recovery()
{
  if (resizingPtr != nullptr)
  {
    #ifdef NOUSEVMEM
    #else
    vmem_free(vmp, resizingPtr);
    #endif
    return true;
  }
  return false;
}
LevelHashing::~LevelHashing(void)
{
  delete[] mutex;
  #ifdef NOUSEVMEM
  #else
  vmem_free(vmp, buckets[0]);
  vmem_free(vmp, buckets[1]);
  #endif
  deletePM();
}

LevelHashing::LevelHashing(size_t _levels)
    : levels{_levels},
      addr_capacity{(uint64_t)pow(2, levels)},
      total_capacity{(uint64_t)pow(2, levels) + (uint64_t)pow(2, levels - 1)},
      resize_num{0}
{
  locksize = 256;
  nlocks = (3 * addr_capacity / 2) / locksize + 1;
  mutex = new std::shared_mutex[nlocks];

  generate_seeds();
  buckets[0] = new Node[addr_capacity];
  buckets[1] = new Node[addr_capacity / 2];
  level_item_num[0] = 0;
  level_item_num[1] = 0;

  interim_level_buckets = NULL;
  #ifdef NOUSEVMEM
  level_allocator::Allocator::Allocate((void**)&resizingPtr, sizeof(char *));
  #else
  resizingPtr = (char *)vmem_malloc(vmp, sizeof(char *));
  #endif
}

bool LevelHashing::Insert(Key_t &key, Value_t value)
{
  bool resized = false;
RETRY:
  while (resizing_lock == 1)
  {
    asm("nop");
  }
  uint64_t f_hash = F_HASH(key);
  uint64_t s_hash = S_HASH(key);
  uint32_t f_idx = F_IDX(f_hash, addr_capacity);
  uint32_t s_idx = S_IDX(s_hash, addr_capacity);
  int i, j;
  for (i = 0; i < 2; i++)
  {
    for (j = 0; j < ASSOC_NUM; j++)
    {
      {
        std::unique_lock<std::shared_mutex> lock(mutex[f_idx / locksize]);
        if (buckets[i][f_idx].token[j] == 1 &&
            buckets[i][f_idx].slot[j].key == key)
        {
          return false;
        }
      }
      {
        std::unique_lock<std::shared_mutex> lock(mutex[s_idx / locksize]);
        if (buckets[i][s_idx].token[j] == 1 &&
            buckets[i][s_idx].slot[j].key == key)
        {
          return false;
        }
      }
    }
    f_idx = F_IDX(f_hash, addr_capacity / 2);
    s_idx = S_IDX(s_hash, addr_capacity / 2);
  }
  f_idx = F_IDX(f_hash, addr_capacity);
  s_idx = S_IDX(s_hash, addr_capacity);
  for (i = 0; i < 2; i++)
  {
    for (j = 0; j < ASSOC_NUM; j++)
    {
      {
        std::unique_lock<std::shared_mutex> lock(mutex[f_idx / locksize]);
        if (buckets[i][f_idx].token[j] == 0)
        {
          buckets[i][f_idx].slot[j].value = value;
          mfence();
          buckets[i][f_idx].slot[j].key = key;
          buckets[i][f_idx].token[j] = 1;
          clflush((char *)&buckets[i][f_idx], sizeof(Node));
          level_item_num[i]++;
          return resized;
        }
      }
      {
        std::unique_lock<std::shared_mutex> lock(mutex[s_idx / locksize]);
        if (buckets[i][s_idx].token[j] == 0)
        {
          buckets[i][s_idx].slot[j].value = value;
          mfence();
          buckets[i][s_idx].slot[j].key = key;
          buckets[i][s_idx].token[j] = 1;
          clflush((char *)&buckets[i][s_idx], sizeof(Node));
          level_item_num[i]++;
          return resized;
        }
      }
    }
    f_idx = F_IDX(f_hash, addr_capacity / 2);
    s_idx = S_IDX(s_hash, addr_capacity / 2);
  }

  f_idx = F_IDX(f_hash, addr_capacity);
  s_idx = S_IDX(s_hash, addr_capacity);

  int empty_loc;
  auto lock = 0;
  if (CAS(&resizing_lock, &lock, 1))
  {
    for (i = 0; i < 2; i++)
    {
      if (!try_movement(f_idx, i, key, value))
      {
        resizing_lock = 0;
        return resized;
      }
      if (!try_movement(s_idx, i, key, value))
      {
        resizing_lock = 0;
        return resized;
      }
      f_idx = F_IDX(f_hash, addr_capacity / 2);
      s_idx = S_IDX(s_hash, addr_capacity / 2);
    }

    if (resize_num > 0)
    {
      {
        std::unique_lock<std::shared_mutex> lock(mutex[f_idx / locksize]);

        empty_loc = b2t_movement(f_idx);

        if (empty_loc != -1)
        {
          buckets[1][f_idx].slot[empty_loc].value = value;
          mfence();
          buckets[1][f_idx].slot[empty_loc].key = key;
          buckets[1][f_idx].token[empty_loc] = 1;
          clflush((char *)&buckets[1][f_idx], sizeof(Node));
          level_item_num[1]++;
          resizing_lock = 0;
          return resized;
        }
      }
      {
        std::unique_lock<std::shared_mutex> lock(mutex[s_idx / locksize]);

        empty_loc = b2t_movement(s_idx);

        if (empty_loc != -1)
        {
          buckets[1][s_idx].slot[empty_loc].value = value;
          mfence();
          buckets[1][s_idx].slot[empty_loc].key = key;
          buckets[1][s_idx].token[empty_loc] = 1;
          clflush((char *)&buckets[1][s_idx], sizeof(Node));
          level_item_num[1]++;
          resizing_lock = 0;
          return resized;
        }
      }
    }

    resize();
    resized = true;
    resizing_lock = 0;
  }
  goto RETRY;
}

bool LevelHashing::InsertOnly(Key_t &key, Value_t value) { return true; }

void LevelHashing::resize(void)
{
  auto lock = new std::unique_lock<std::shared_mutex> *[nlocks];
  for (int i = 0; i < nlocks; i++)
  {
    lock[i] = new std::unique_lock<std::shared_mutex>(mutex[i]);
  }
  std::shared_mutex *old_mutex = mutex;

  int prev_nlocks = nlocks;
  nlocks = nlocks + 2 * addr_capacity / locksize + 1;
  mutex = new std::shared_mutex[nlocks];

  size_t new_addr_capacity = pow(2, levels + 1);
  interim_level_buckets = new Node[new_addr_capacity];
  if (!interim_level_buckets)
  {
    perror("The expanding fails");
  }
  clflush((char *)&interim_level_buckets, sizeof(Node));
  resizingPtr = (char *)interim_level_buckets;
  clflush((char *)&resizingPtr, sizeof(char *));
  uint64_t new_level_item_num = 0;
  uint64_t old_idx;
  for (old_idx = 0; old_idx < pow(2, levels - 1); old_idx++)
  {
    uint64_t i, j;
    for (i = 0; i < ASSOC_NUM; i++)
    {
      if (buckets[1][old_idx].token[i] == 1)
      {
        Key_t key = buckets[1][old_idx].slot[i].key;
        Value_t value = buckets[1][old_idx].slot[i].value;

        uint32_t f_idx = F_IDX(F_HASH(key), new_addr_capacity);
        uint32_t s_idx = S_IDX(S_HASH(key), new_addr_capacity);

        uint8_t insertSuccess = 0;
        for (j = 0; j < ASSOC_NUM; j++)
        {
          if (interim_level_buckets[f_idx].token[j] == 0)
          {
            interim_level_buckets[f_idx].slot[j].value = value;

            mfence();

            interim_level_buckets[f_idx].slot[j].key = key;
            interim_level_buckets[f_idx].token[j] = 1;

            clflush((char *)&interim_level_buckets[f_idx], sizeof(Node));

            insertSuccess = 1;
            new_level_item_num++;
            break;
          }
          else if (interim_level_buckets[s_idx].token[j] == 0)
          {
            interim_level_buckets[s_idx].slot[j].value = value;

            mfence();

            interim_level_buckets[s_idx].slot[j].key = key;
            interim_level_buckets[s_idx].token[j] = 1;

            clflush((char *)&interim_level_buckets[s_idx], sizeof(Node));

            insertSuccess = 1;
            new_level_item_num++;
            break;
          }
        }

        buckets[1][old_idx].token[i] = 0;

        clflush((char *)&buckets[1][old_idx].token[i], sizeof(uint8_t));
      }
    }
  }
  clflush((char *)&buckets[1][0], sizeof(Node) * pow(2, levels - 1));
  clflush((char *)&interim_level_buckets[0], sizeof(Node) * new_addr_capacity);
  resizingPtr = nullptr;
  clflush((char *)&resizingPtr, sizeof(char *));
  levels++;
  resize_num++;

  #ifdef NOUSEVMEM
  #else
  vmem_free(vmp, buckets[1]);
  #endif

  buckets[1] = buckets[0];
  buckets[0] = interim_level_buckets;
  interim_level_buckets = NULL;

  level_item_num[1] = level_item_num[0];
  level_item_num[0] = new_level_item_num;

  addr_capacity = new_addr_capacity;
  total_capacity = pow(2, levels) + pow(2, levels - 1);

  for (int i = 0; i < prev_nlocks; i++)
  {
    delete lock[i];
  }
  delete[] lock;
  delete[] old_mutex;
}

uint8_t LevelHashing::try_movement(uint64_t idx, uint64_t level_num, Key_t &key,
                                   Value_t value)
{
  uint64_t i, j, jdx;
  {
    std::unique_lock<std::shared_mutex> *lock[2];
    lock[0] = new std::unique_lock<std::shared_mutex>(mutex[idx / locksize]);
    for (i = 0; i < ASSOC_NUM; i++)
    {
      Key_t m_key = buckets[level_num][idx].slot[i].key;
      Value_t m_value = buckets[level_num][idx].slot[i].value;
      uint64_t f_hash = F_HASH(m_key);
      uint64_t s_hash = S_HASH(m_key);
      uint64_t f_idx = F_IDX(f_hash, addr_capacity / (1 + level_num));
      uint64_t s_idx = S_IDX(s_hash, addr_capacity / (1 + level_num));

      if (f_idx == idx)
        jdx = s_idx;
      else
        jdx = f_idx;

      if ((jdx / locksize) != (idx / locksize))
      {
        lock[1] =
            new std::unique_lock<std::shared_mutex>(mutex[jdx / locksize]);
      }

      for (j = 0; j < ASSOC_NUM; j++)
      {
        if (buckets[level_num][jdx].token[j] == 0)
        {
          buckets[level_num][jdx].slot[j].value = m_value;
          mfence();
          buckets[level_num][jdx].slot[j].key = m_key;
          buckets[level_num][jdx].token[j] = 1;
          clflush((char *)&buckets[level_num][jdx], sizeof(Node));
          buckets[level_num][idx].token[i] = 0;
          clflush((char *)&buckets[level_num][idx].token[i], sizeof(uint8_t));

          buckets[level_num][idx].slot[i].value = value;
          mfence();
          buckets[level_num][idx].slot[i].key = key;
          buckets[level_num][idx].token[i] = 1;
          clflush((char *)&buckets[level_num][idx], sizeof(Node));
          level_item_num[level_num]++;

          if ((jdx / locksize) != (idx / locksize))
            delete lock[1];
          delete lock[0];

          return 0;
        }
      }
      if ((jdx / locksize) != (idx / locksize))
        delete lock[1];
    }
    delete lock[0];
  }

  return 1;
}

int LevelHashing::b2t_movement(uint64_t idx)
{
  Key_t key;
  Value_t value;
  uint64_t s_hash, f_hash;
  uint64_t s_idx, f_idx;
  uint64_t i, j;

  std::unique_lock<shared_mutex> *lock;
  for (i = 0; i < ASSOC_NUM; i++)
  {
    key = buckets[1][idx].slot[i].key;
    value = buckets[1][idx].slot[i].value;
    f_hash = F_HASH(key);
    s_hash = S_HASH(key);
    f_idx = F_IDX(f_hash, addr_capacity);
    s_idx = S_IDX(s_hash, addr_capacity);

    for (j = 0; j < ASSOC_NUM; j++)
    {
      if ((idx / locksize) != (f_idx / locksize))
        lock = new std::unique_lock<std::shared_mutex>(mutex[f_idx / locksize]);
      if (buckets[0][f_idx].token[j] == 0)
      {
        buckets[0][f_idx].slot[j].value = value;
        mfence();
        buckets[0][f_idx].slot[j].key = key;
        buckets[0][f_idx].token[j] = 1;
        clflush((char *)&buckets[0][f_idx], sizeof(Node));
        buckets[1][idx].token[i] = 0;
        clflush((char *)&buckets[1][idx].token[i], sizeof(uint8_t));
        level_item_num[0]++;
        level_item_num[1]--;

        if ((idx / locksize) != (f_idx / locksize))
          delete lock;
        return i;
      }
      if ((idx / locksize) != (f_idx / locksize))
        delete lock;
      if ((idx / locksize) != (s_idx / locksize))
        lock = new std::unique_lock<std::shared_mutex>(mutex[s_idx / locksize]);

      if (buckets[0][s_idx].token[j] == 0)
      {
        buckets[0][s_idx].slot[j].value = value;
        mfence();
        buckets[0][s_idx].slot[j].key = key;
        buckets[0][s_idx].token[j] = 1;
        clflush((char *)&buckets[0][s_idx], sizeof(Node));
        buckets[1][idx].token[i] = 0;
        clflush((char *)&buckets[0][s_idx].token[j], sizeof(uint8_t));

        level_item_num[0]++;
        level_item_num[1]--;

        if ((idx / locksize) != (s_idx / locksize))
          delete lock;
        return i;
      }
      if ((idx / locksize) != (s_idx / locksize))
        delete lock;
    }
  }
  return -1;
}

Value_t LevelHashing::Get(Key_t &key)
{
  uint64_t f_hash = F_HASH(key);
  uint64_t s_hash = S_HASH(key);
  uint32_t f_idx = F_IDX(f_hash, addr_capacity);
  uint32_t s_idx = S_IDX(s_hash, addr_capacity);
  int i = 0, j;

  for (i = 0; i < 2; i++)
  {
    {
      std::shared_lock<std::shared_mutex> lock(mutex[f_idx / locksize]);
      for (j = 0; j < ASSOC_NUM; j++)
      {
        if (buckets[i][f_idx].token[j] == 1 &&
            buckets[i][f_idx].slot[j].key == key)
        {
          return buckets[i][f_idx].slot[j].value;
        }
      }
    }
    {
      std::shared_lock<std::shared_mutex> lock(mutex[s_idx / locksize]);
      for (j = 0; j < ASSOC_NUM; j++)
      {
        if (buckets[i][s_idx].token[j] == 1 &&
            buckets[i][s_idx].slot[j].key == key)
        {
          return buckets[i][s_idx].slot[j].value;
        }
      }
    }
    f_idx = F_IDX(f_hash, addr_capacity / 2);
    s_idx = S_IDX(s_hash, addr_capacity / 2);
  }

  return NONE;
}

bool LevelHashing::Delete(Key_t &key)
{
  while (resizing_lock == 1)
  {
    asm("nop");
  }
  uint64_t f_hash = F_HASH(key);
  uint64_t s_hash = S_HASH(key);
  uint64_t f_idx = F_IDX(f_hash, addr_capacity);
  uint64_t s_idx = S_IDX(s_hash, addr_capacity);

  uint64_t i, j;
  for (i = 0; i < 2; i++)
  {
    for (j = 0; j < ASSOC_NUM; j++)
    {
      std::unique_lock<std::shared_mutex> lock(mutex[f_idx / locksize]);
      if (buckets[i][f_idx].token[j] == 1 &&
          buckets[i][f_idx].slot[j].key == key)
      {
        buckets[i][f_idx].token[j] = 0;
        clflush((char *)&buckets[i][f_idx].token[j], sizeof(uint8_t));
        return 1;
      }
    }
    for (j = 0; j < ASSOC_NUM; j++)
    {
      std::unique_lock<std::shared_mutex> lock(mutex[s_idx / locksize]);
      if (buckets[i][s_idx].token[j] == 1 &&
          buckets[i][s_idx].slot[j].key == key)
      {
        buckets[i][s_idx].token[j] = 0;
        clflush((char *)&buckets[i][s_idx].token[j], sizeof(uint8_t));
        return 1;
      }
    }
    f_idx = F_IDX(f_hash, addr_capacity / 2);
    s_idx = S_IDX(s_hash, addr_capacity / 2);
  }

  return 0;
}

hash_Utilization LevelHashing::Utilization(void)
{
  size_t sum = 0;
  for (unsigned i = 0; i < addr_capacity; ++i)
  {
    for (unsigned j = 0; j < ASSOC_NUM; ++j)
    {
      if (buckets[0][i].token[j] != 0)
      {
        sum++;
      }
    }
  }
  for (unsigned i = 0; i < addr_capacity / 2; ++i)
  {
    for (unsigned j = 0; j < ASSOC_NUM; ++j)
    {
      if (buckets[1][i].token[j] != 0)
      {
        sum++;
      }
    }
  }
  // std::cout << sum << " " << total_capacity * ASSOC_NUM << std::endl;
  hash_Utilization h;
  h.load_factor = ((float)(sum) / (float)(total_capacity * ASSOC_NUM) * 100);
  h.utilization =
      ((float)(sum * 16) / (float)(total_capacity * sizeof(Node)) * 100);
  return h;
}
