#include <iostream>
#include <thread>
#include <cstring>
#include "transmit.h"
#include "log.h"

using namespace std;

int main() {
  log_init(LL_DEBUG, "log", ".");
  Transmit transmit;
  thread t_transmit(&Transmit::start, transmit);
  t_transmit.join();
}
