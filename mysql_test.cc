/* Standard C++ headers */
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <stdexcept>

#include "dr_mysql.h"

int main(int argc, const char *argv[]) {
  DrMysql db;
  db.login("siwonred@gmail.com", "qwert");
  return 0;
}

