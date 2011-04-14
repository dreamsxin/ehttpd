#pragma once
#include <string>

using namespace std;

class Session {
 public:
  string user_id;
  string macaddress;
  time_t timestamp;
};
