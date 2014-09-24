#ifndef RECEIVE_H
#define RECEIVE_H

#include <thread>
#include <string>
#include "common.h"
using namespace std;

class Receive {
  public:
    Receive();
    void start();
    char local_ip[100];
};

#endif
