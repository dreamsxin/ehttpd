/* Standard C++ headers */
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <stdexcept>

/* MySQL Connector/C++ specific headers */
#include <driver.h>
#include <connection.h>
#include <statement.h>
#include <prepared_statement.h>
#include <resultset.h>
#include <metadata.h>
#include <resultset_metadata.h>
#include <exception.h>
#include <warning.h>
#include <exception>

#define DBHOST "tcp://directreader.co.kr:3306"
#define USER "namilee"
#define PASSWORD "dlp1004"
#define DATABASE "DIRECTREADER"

#include "dr_mysql.h"
using namespace sql;

DrMysql::DrMysql() {
  driver = get_driver_instance();
}

void DrMysql::connect() {
  con.reset(driver->connect(DBHOST, USER, PASSWORD));
  con->setSchema(DATABASE);
}

void DrMysql::close() {
  try{
    con -> close();
  }catch(std::exception const& e) {
  }
}

bool DrMysql::login(string const &email, string const &password,
                    string *user_id,
                    string *macaddress) {
  try{

    connect();
    string query = "SELECT * from co_user where email = '" + email + "'";
    scoped_ptr<Statement> stmt(con->createStatement());
    scoped_ptr<ResultSet> res(stmt->executeQuery(query));

    if (!res->next()) {
      throw runtime_error("No user");
    }

    if (password != res->getString("installkey") &&
        password != res->getString("password")) {
      throw runtime_error("Invild password");
    }

    if (user_id) {
      *user_id = res->getString("userkey");
    }
    if (macaddress) {
      *macaddress = res->getString("macaddress");
    }

  } catch (SQLException &e) {
    cout << "ERROR: SQLException in " << __FILE__;
    cout << " (" << __func__<< ") on line " << __LINE__ << endl;
    cout << "ERROR: " << e.what();
    cout << " (MySQL error code: " << e.getErrorCode();
    cout << ", SQLState: " << e.getSQLState() << ")" << endl;

    close();
    return false;
  } catch (std::runtime_error &e) {
    cout << "ERROR: runtime_error in " << __FILE__;
    cout << " (" << __func__ << ") on line " << __LINE__ << endl;
    cout << "ERROR: " << e.what() << endl;

    close();
    return false;
  }
  close();
  return true;
}

