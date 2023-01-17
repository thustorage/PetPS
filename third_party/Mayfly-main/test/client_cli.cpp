#include "DSM.h"

#include "KVClient.h"
#include <iostream>

// ./client server_nr client_nr client_thread_nr

void split(const std::string &s, std::vector<std::string> &sv,
           const char *delim = " ") {
  sv.clear();
  char *buffer = new char[s.size() + 1];
  buffer[s.size()] = '\0';
  std::copy(s.begin(), s.end(), buffer);
  char *p = std::strtok(buffer, delim);
  do {
    sv.push_back(p);
  } while ((p = std::strtok(NULL, delim)));
  delete[] buffer;
  return;
}

DSM *dsm;

int main(int argc, char **argv) {

  if (argc != 5) {
    fprintf(stderr, "Usage: ./client server_nr client_nr server_thread_nr "
                    "client_thread_nr\n");
    exit(-1);
  }

  uint32_t server_nr = std::atoi(argv[1]);
  uint32_t client_nr = std::atoi(argv[2]);
  uint32_t server_thread_nr = std::atoi(argv[3]);
  uint32_t client_thread_nr = std::atoi(argv[4]);
  (void)client_thread_nr;

  ClusterInfo cluster;
  cluster.serverNR = server_nr;
  cluster.clientNR = client_nr;

  DSMConfig config(CacheConfig(), cluster, 1, true);
  dsm = DSM::getInstance(config);

  printf("client init\n");

  dsm->registerThread();
  auto client = new kv::KVClient(dsm, server_thread_nr);

  uint64_t k = 0;
  (void)k;
  Slice value;
  // while (true) {
  //   k++;
  //   Slice key((char *)&k, sizeof(uint64_t));
  //   assert(client->put(key, key));
  //   assert(client->get(key, value));

  //   assert(value.len == sizeof(uint64_t));
  //   assert(*(uint64_t *)(value.s) == k);

  //   if (k % 10000 == 0) {
  //     printf("request %ld\n", k);
  //   }
  // }

  while (true) {
    std::cout << ">> ";
    std::string cmd;
    std::getline(std::cin, cmd);

    if (cmd.size() == 0) {
      continue;
    }

    std::vector<std::string> sub_cmd;
    split(cmd, sub_cmd, " ");

    if (sub_cmd.size() == 0) {
      continue;
    }

    if (sub_cmd.size() == 1 && (sub_cmd[0] == "quit" || sub_cmd[0] == "exit")) {
      exit(0);
    }

    if (sub_cmd.size() == 1 && sub_cmd[0] == "stats") {
      client->stats();
      continue;
    }

    if (sub_cmd[0] == "put") {
      if (sub_cmd.size() != 3) {
        std::cout << "[ERROR] put k v\n";
        continue;
      }

      auto res = client->put(Slice::from_string(sub_cmd[1]),
                             Slice::from_string(sub_cmd[2]));
      if (res) {
        std::cout << "put ok\n";
      } else {
        std::cout << "put failed\n";
      }

    } else if (sub_cmd[0] == "get") {
      if (sub_cmd.size() != 2) {
        std::cout << "[ERROR CMD] get k\n";
        continue;
      }

      Slice value;
      auto res = client->get(Slice::from_string(sub_cmd[1]), value);
      if (res) {
        if (value.len == 0) {
          std::cout << "(nil)\n";
        } else {
          std::cout << value.to_string() << std::endl;
        }

      } else {
        std::cout << "NOT FOUND \n";
      }
    } else if (sub_cmd[0] == "del") {
      if (sub_cmd.size() != 2) {
        std::cout << "[ERROR CMD] del k\n";
        continue;
      }
      auto res = client->del(Slice::from_string(sub_cmd[1]));
      if (res) {
        std::cout << "del ok\n";
      } else {
        std::cout << "NOT FOUND\n";
      }
    } else {
      std::cout << "unsupported operation\n";
    }
  }
}