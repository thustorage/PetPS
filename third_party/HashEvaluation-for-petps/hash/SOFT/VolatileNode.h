#ifndef VOLATILE_NODE_H_
#define VOLATILE_NODE_H_

#include <atomic>

#include "PNode.h"

template <class T>
class Node {
 public:
  uintptr_t key;
  T value;
  PNode<T> *pptr;
  bool pValidity;
  std::atomic<Node *> next;

  Node(uintptr_t key, T value, PNode<T> *pptr, bool pValidity)
      : key(key),
        value(value),
        pptr(pptr),
        pValidity(pValidity),
        next(nullptr) {}
};

#endif
