#include <iostream>
#include <thread>
#include <cstring>
#include "receive.h"
#include "log.h"

using namespace std;

int main() {
  log_init(LL_DEBUG, "log", ".");
  Receive receive;
  thread t_receive(&Receive::start, receive);
  t_receive.join();
}
