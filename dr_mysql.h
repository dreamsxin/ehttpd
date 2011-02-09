#pragma once

#include <boost/scoped_ptr.hpp>
using namespace boost;

/* MySQL Connector/C++ specific headers */
#include <driver.h>
#include <connection.h>

using namespace std;

class DrMysql{
  scoped_ptr<sql::Connection> con;
  sql::Driver* driver;

public:
  DrMysql();
  void connect();
  void close();
  bool login(string const &email, string const &password,
             string *user_id = NULL,
             string *macaddress = NULL);
};
