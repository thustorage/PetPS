#if !defined(_TIMER_H_)
#define _TIMER_H_

#include <cstdint>
#include <cstdio>
#include <time.h>

class Timer {
public:
  Timer() = default;

  void begin() { clock_gettime(CLOCK_REALTIME, &s); }

  uint64_t end(uint64_t loop = 1) {
    this->loop = loop;
    clock_gettime(CLOCK_REALTIME, &e);
    uint64_t ns_all =
        (e.tv_sec - s.tv_sec) * 1000000000ull + (e.tv_nsec - s.tv_nsec);
    ns = ns_all / loop;

    return ns;
  }

  void print() {

    if (ns < 1000) {
      printf("%ldns per loop\n", ns);
    } else {
      printf("%lfus per loop\n", ns * 1.0 / 1000);
    }
  }

  static void sleep(uint64_t sleep_ns) {
    Timer clock;

    clock.begin();
    while (true) {
      if (clock.end() >= sleep_ns) {
        return;
      }
    }
  }

  static uint64_t get_time_ns() {
    timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    uint64_t ns_all = t.tv_sec * 1000000000ull + t.tv_nsec;

    return ns_all;
  }

  void end_print(uint64_t loop = 1) {
    end(loop);
    print();
  }

private:
  timespec s, e;
  uint64_t loop;
  uint64_t ns;
};

#endif // _TIMER_H_
