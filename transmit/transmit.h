#ifndef TRANSMIT_H
#define TRANSMIT_H

#include <thread>
#include <string>
#include "common.h"
using namespace std;

class Transmit {
  public:
    Transmit();
    void start();
    char local_ip[100];
};

#endif
