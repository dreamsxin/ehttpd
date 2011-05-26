#pragma once
#include <string>

using namespace std;

class Session {
 public:
  string user_id;
  string email;
  string username;
  string macaddress;
  time_t timestamp;
};
