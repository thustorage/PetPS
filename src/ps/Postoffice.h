#pragma once
#include <string>

class memcached_st;
class XPostoffice {
 public:
  static XPostoffice *GetInstance() {
    static XPostoffice instance;
    return &instance;
  }

  bool IsServer() const {
    return actor_ == ACTOR_SERVER;
  }

  bool IsClient() const {
    return actor_ == ACTOR_CLIENT;
  }

  int ServerID() const {
    return server_id_;
  }

  int ClientID() const {
    return client_id_;
  }

  int GlobalID() const {
    return global_id_;
  }

  int NumServers() const {
    return num_servers_;
  }
  int NumClients() const {
    return num_clients_;
  }

  std::string MemCachedGet(const std::string &key);

  void MemCachedSet(const std::string &key, const std::string &value);

 private:
  XPostoffice();
  void ConnectMemcached();
  int num_servers_;
  int num_clients_;
  int global_id_;
  int server_id_;
  int client_id_;
  enum ActorEnum { ACTOR_SERVER, ACTOR_CLIENT };
  ActorEnum actor_;
  memcached_st *memc_;
};