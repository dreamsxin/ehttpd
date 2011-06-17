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

#include "dr_mysql.h"
using namespace sql;

scoped_ptr<sql::Connection> DrMysql::con;
sql::Driver* DrMysql::driver = NULL;
pthread_mutex_t DrMysql::mutex_login = PTHREAD_MUTEX_INITIALIZER;

DrMysql::DrMysql() {
}

void DrMysql::connect() {
}

void DrMysql::close() {
}

bool DrMysql::login(string const &email, string const &password,
                    string *user_id,
                    string *macaddress) {
  try{
    pthread_mutex_lock(&mutex_login);
    driver->threadInit();
    string query = "SELECT *,password('" + password + "') as enc from co_user where email = '" + email + "'";
    scoped_ptr<Statement> stmt(con->createStatement());
    scoped_ptr<ResultSet> res(stmt->executeQuery(query));

    if (!res->next()) {
      throw runtime_error("Non-existing user");
    }

    if (password != res->getString("installkey") &&
        res->getString("enc") != res->getString("encpassword")) {
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
    close();
    driver->threadEnd();
    pthread_mutex_unlock(&mutex_login);
    return false;
  } catch (std::runtime_error &e) {
    cout << "ERROR: runtime_error in " << __FILE__;
    cout << " (" << __func__ << ") on line " << __LINE__ << endl;
    close();
    driver->threadEnd();
    pthread_mutex_unlock(&mutex_login);
    return false;
  }
  close();
  driver->threadEnd();
  pthread_mutex_unlock(&mutex_login);
  return true;
}

