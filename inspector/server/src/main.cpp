#include "Worker.h"

int main() {
  auto worker = inspector::Worker("127.0.0.1", 8086);
  printf("%lld", worker.GetLastTime());
  while(true) {

  }
  return 1;
}