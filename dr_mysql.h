#pragma once

#include <boost/scoped_ptr.hpp>
using namespace boost;

/* MySQL Connector/C++ specific headers */
#include <driver.h>
#include <connection.h>

#define DBHOST "tcp://directreader.net:3306"
#define USER "namilee"
#define PASSWORD "dlp1004"
#define DATABASE "DIRECTREADER"

using namespace std;

class DrMysql{
public:
  static scoped_ptr<sql::Connection> con;
  static sql::Driver* driver;
  
  DrMysql();
  void connect();
  void close();

  static void connection_init() {
    driver = get_driver_instance();
    con.reset(driver->connect(DBHOST, USER, PASSWORD));
    con->setSchema(DATABASE);
  }

  bool login(string const &email, string const &password,
             string *user_id = NULL,
             string *macaddress = NULL);
};
