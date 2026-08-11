#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error "Native regression tests must compile with assert() enabled"
#endif

int main() {
  bool assertion_executed = false;
  assert((assertion_executed = true));
  if (!assertion_executed) return 1;
  std::cout << "release test assertions enabled\n";
  return 0;
}
