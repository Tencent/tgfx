#include <iostream>

#include "Worker.h"

int main() {
  auto worker = inspector::Worker("127.0.0.1", 8086);
  printf("%lld\n", worker.GetLastTime());
  while(true) {

  }
  return 1;
}