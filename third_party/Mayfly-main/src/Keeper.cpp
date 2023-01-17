#include "Keeper.h"
#include "mayfly_config.h"
#include <fstream>
#include <iostream>
#include <random>

char *getIP();

std::string trim(const std::string &s) {
  std::string res = s;
  if (!res.empty()) {
    res.erase(0, res.find_first_not_of(" "));
    res.erase(res.find_last_not_of(" ") + 1);
  }
  return res;
}

const char *Keeper::SERVER_NUM_KEY = "serverNum";

Keeper::Keeper(uint32_t maxServer)
    : maxServer(maxServer), curServer(0), memc(NULL) {}

Keeper::~Keeper() {
  //   listener.detach();

  disconnectMemcached();
}

bool Keeper::connectMemcached() {
  memcached_server_st *servers = NULL;
  memcached_return rc;

  std::ifstream conf(MAYFLY_PATH "/memcached.conf");

  if (!conf) {
    fprintf(stderr, "can't open memcached.conf\n");
    return false;
  }
  const char *env_p = std::getenv("LC_DEBUG_XMH");
  std::string addr, port;
  if (env_p != nullptr && strcmp(env_p, "LC_DEBUG_XMH") == 0) {
    std::cout << "use memcached in 130:21111" << std::endl;
    addr = "10.0.2.130";
    port = "21111";
  } else {
    std::getline(conf, addr);
    std::getline(conf, port);
    std::cout << "use memcached in " << trim(addr) << ":" << trim(port)
              << std::endl;
  }

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, trim(addr).c_str(),
                                         std::stoi(trim(port)), &rc);
  rc = memcached_server_push(memc, servers);

  if (rc != MEMCACHED_SUCCESS) {
    fprintf(stderr, "Counld't add server:%s\n", memcached_strerror(memc, rc));
    return false;
  }

  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
  return true;
}

bool Keeper::disconnectMemcached() {
  if (memc) {
    memcached_quit(memc);
    memcached_free(memc);
    memc = NULL;
  }
  return true;
}

void Keeper::serverEnter(int globalID) {
  memcached_return rc;
  uint64_t serverNum;
  if (globalID == -1) {
    while (true) {
      rc = memcached_increment(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), 1,
                               &serverNum);
      if (rc == MEMCACHED_SUCCESS) {

        myNodeID = serverNum - 1;

        printf("I am servers %d\n", myNodeID);
        return;
      }
      fprintf(stderr,
              "Server %d Counld't incr value and get ID: %s, retry...\n",
              myNodeID, memcached_strerror(memc, rc));
      usleep(10000);
    }
  } else {
    while (1) {
      if (globalID == 0)
        break;
      auto str =
          memGet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), nullptr);
      if (std::atoi(str) == globalID)
        break;
    }
    while (true) {
      rc = memcached_increment(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), 1,
                               &serverNum);
      if (rc == MEMCACHED_SUCCESS) {

        myNodeID = serverNum - 1;

        printf("I am servers %d\n", myNodeID);
        auto v = std::to_string(globalID + 1);
        memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), v.c_str(),
               v.size());
        return;
      }
      fprintf(stderr,
              "Server %d Counld't incr value and get ID: %s, retry...\n",
              myNodeID, memcached_strerror(memc, rc));
      usleep(10000);
    }
  }
}

void Keeper::serverConnect() {

  size_t l;
  uint32_t flags;
  memcached_return rc;

  while (curServer < maxServer) {
    // std::cout << "poll in server connect";
    char *serverNumStr = memcached_get(memc, SERVER_NUM_KEY,
                                       strlen(SERVER_NUM_KEY), &l, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
      fprintf(stderr, "Server %d Counld't get serverNum: %s, retry\n", myNodeID,
              memcached_strerror(memc, rc));
      continue;
    }
    uint32_t serverNum = atoi(serverNumStr);
    free(serverNumStr);

    // /connect server K
    for (size_t k = curServer; k < serverNum; ++k) {
      if (k != myNodeID) {
        connectNode(k);
        printf("I connect server %zu\n", k);
      }
    }
    curServer = serverNum;
  }
}

void Keeper::memSet(const char *key, uint32_t klen, const char *val,
                    uint32_t vlen) {

  memcached_return rc;
  while (true) {
    rc = memcached_set(memc, key, klen, val, vlen, (time_t)0, (uint32_t)0);
    if (rc == MEMCACHED_SUCCESS) {
      break;
    }
    usleep(400);
  }
}

char *Keeper::memGet(const char *key, uint32_t klen, size_t *v_size) {

  size_t l;
  char *res;
  uint32_t flags;
  memcached_return rc;

  while (true) {

    res = memcached_get(memc, key, klen, &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS) {
      break;
    }
    usleep(400 * myNodeID);
  }

  if (v_size != nullptr) {
    *v_size = l;
  }

  return res;
}

uint64_t Keeper::memFetchAndAdd(const char *key, uint32_t klen) {
  uint64_t res;
  while (true) {
    memcached_return rc = memcached_increment(memc, key, klen, 1, &res);
    if (rc == MEMCACHED_SUCCESS) {
      return res;
    }
    // usleep(10000);
  }
}
