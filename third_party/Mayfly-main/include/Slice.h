#if !defined(_SLICE_H_)
#define _SLICE_H_

struct Slice {
  const char *s;
  size_t len;

  Slice() : Slice(nullptr, 0) {}

  Slice(const char *s, size_t len) : s(s), len(len) {}

  Slice(const std::string &str) : s(str.c_str()), len(str.size()) {}

  std::string to_string() { return std::string(s, len); }

  bool equal(const Slice &o) const {
    return len == o.len && memcmp(s, o.s, len) == 0;
  }

  static Slice from_string(std::string &s) {
    return Slice(s.c_str(), s.size());
  }

  static Slice Void() { return Slice(nullptr, 0); }
};

#endif // _SLICE_H_
