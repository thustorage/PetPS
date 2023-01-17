#ifndef SOFT_UTILS_H_
#define SOFT_UTILS_H_
#define STATE_MASK 0x3

namespace softUtils {

enum state {
  INSERTED = 0,
  INTEND_TO_DELETE = 1,
  INTEND_TO_INSERT = 2,
  DELETED = 3
};

template <class Node>
static inline Node *getRef(Node *ptr) {
  auto ptrLong = (uintptr_t)(ptr);
  ptrLong &= ~STATE_MASK;
  return (Node *)(ptrLong);
}

template <class Node>
static inline Node *createRef(Node *p, state s) {
  auto ptrLong = (uintptr_t)(p);
  ptrLong &= ~STATE_MASK;
  ptrLong |= s;
  return (Node *)(ptrLong);
}

template <class Node>
static inline bool stateCAS(std::atomic<Node *> &atomicP, state expected,
                            state newVal) {
  Node *p = atomicP.load();
  Node *before = static_cast<Node *>(softUtils::createRef(p, expected));
  Node *after = static_cast<Node *>(softUtils::createRef(p, newVal));
  return atomicP.compare_exchange_strong(before, after);
}

static inline state getState(void *p) {
  return static_cast<state>((uintptr_t)(p)&STATE_MASK);
}

static inline bool isOut(state s) {
  return s == state::DELETED || s == state::INTEND_TO_INSERT;
}

static inline bool isOut(void *ptr) { return isOut(softUtils::getState(ptr)); }

}  // namespace softUtils

#endif
