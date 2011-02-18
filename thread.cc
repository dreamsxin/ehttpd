#include <sstream>
#include <pthread.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <deque>

#include "./embedhttp.h"
#include "./connection.h"
#include "./dr_mysql.h"
#include "./log.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace std;

#define MAX_THREAD 20
#define PORT 8080

int cnt=0;
int listenfd;
int cookie_index=1;
typedef struct {
  pthread_t tid;
  deque<int> *conn_pool;
} Thread;

typedef shared_ptr<ehttp> ehttp_ptr;

class connection {
public:
  ehttp_ptr fd_polling;
  ehttp_ptr fd_request;

  string command;
  string requestpath;
  string key;
  int status;

  connection() {};

  connection(ehttp_ptr fd_request,
             string command,
             string requestpath,
             string key,
             ehttp_ptr fd_polling,
             int status) : fd_request(fd_request),
                           command(command),
                           requestpath(requestpath),
                           key(key),
                           fd_polling(fd_polling),
                           status(status) {};
};

map <string, connection > connections;
map <string, map<string, string> > session;

DrMysql db;

void nonblock(int sockfd) {
  int opts;
  opts = fcntl(sockfd, F_GETFL);
  if(opts < 0) {
    perror("fcntl(F_GETFL)\n");
    exit(1);
  }
  opts = (opts | O_NONBLOCK);
  if(fcntl(sockfd, F_SETFL, opts) < 0) {
    perror("fcntl(F_SETFL)\n");
    exit(1);
  }
}

void loginFail (ehttp_ptr obj) {
  obj->out_set_file("stringtemplate.json");
  obj->out_replace_token("string","Login required");
  obj->out_replace();
  obj->out_commit();
  obj->close();
}

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

void removeConnection(string userid) {
  log(0) << "remove Connection" << endl;
  if (connections.count(userid) == 0) {
    return;
  }
  if (connections[userid].fd_polling.get() != NULL) {
    connections[userid].fd_polling->close(); 
  }
  if (connections[userid].fd_request.get() != NULL) {
    connections[userid].fd_request->close();
  }
  connections.erase(userid);
}

void dieConnection(string userid, const string &error_message) {
  log(0) << "Die Connection" << endl;
  if (connections.count(userid) == 0) {
    return;
  }
  if (connections[userid].fd_polling.get() != NULL) {
    connections[userid].fd_polling->error(error_message);
  }
  if (connections[userid].fd_request.get() != NULL) {
    connections[userid].fd_request->error(error_message);
  }
  connections.erase(userid);
}

int handleDefault(ehttp_ptr obj) {
  obj->out_set_file("helloworld_template.html");
  obj->out_replace_token("MESSAGE", "Hello World");
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

int login_handler(ehttp_ptr obj) {
  log(0) << "Login Handler!" << endl;
  if ((obj->getPostParams()).count("email") > 0) {
    log(0) << "POST" << endl;
    string email = obj->getPostParams()["email"];
    string padpasskey = obj->getPostParams()["padpasskey"];
    string installkey = obj->getPostParams()["installkey"];
    string user_id, macaddress;
    if (padpasskey.length() > 0 && db.login(email, padpasskey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid;
      session[sessionid]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      log(0) << user_id << " login success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid;
      session[sessionid]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      log(0) << user_id << " login success" << endl;
    } else {
      string msg = user_id + " login fail";
      obj->error(msg);
    }
    log(0) << "mac : " << macaddress << endl;
  } else {
  log(0) << "GET" << endl;
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

int execute_polling(string userid) {
  log(0) << "Execute_polling(" << userid << "/" << (connections[userid].fd_polling.get() != NULL) << "/" << (connections[userid].fd_request.get() != NULL) << ")" << endl;
  if (connections[userid].fd_polling.get() != NULL && connections[userid].fd_request.get() != NULL) {
    log(0) << "Execute_polling : fd_polling = " << connections[userid].fd_polling->getFD() << "   <==>  fd_request = " << connections[userid].fd_request->getFD() << endl;
    ehttp_ptr obj = connections[userid].fd_polling;
    obj->out_set_file("polling.json");
    obj->out_replace_token("incrkey",connections[userid].key);
    obj->out_replace_token("command","getfolderlist");
    string requestpath = connections[userid].requestpath;
    obj->addslash(&requestpath);

    obj->out_replace_token("requestpath",requestpath);
    obj->out_replace();
    int ret = obj->out_commit();
    obj->close();
    log(0) << "(" << obj->getFD() << ") Waiting uploading... Status change ( 1-> 2 )" << endl;
    connections[userid].status = 2;
    return ret;
  }

 return 0;
}

int request_handler( ehttp_ptr obj ) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];
  if (obj->getUrlParams()["command"] == "logout") {
    session.erase(session_id);
    obj->out_set_file("request.json");
    obj->out_replace_token("jsondata","");
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  }

  log(0) << "Request Handler accepted...(" << userid << "/" << (connections[userid].fd_polling.get() != NULL) << "/" << (connections[userid].fd_request.get() != NULL) << ")" << endl;
  if (connections.count(userid) > 0 && connections[userid].fd_request.get() != NULL) {
    removeConnection("Clean connection for new request");
  }
  // log(0) << "connection created(" << userid << ")" << endl;

  connections[userid].fd_request = ehttp_ptr(obj);
  connections[userid].command = obj->getUrlParams()["command"];
  connections[userid].requestpath = obj->getUrlParams()["requestpath"];
  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();
  connections[userid].key = to_string(u);
  connections[userid].status = 1;
  log(0) << "Set request info (fd_request=" << (connections[userid].fd_request)->getFD() << ", command=" << connections[userid].command << ", requestpath=" << connections[userid].requestpath <<", key=" << connections[userid].key << ",status=" << connections[userid].status << ")" << endl;
  return execute_polling(userid);
}

int polling_handler( ehttp_ptr obj ) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  //  dprintf("Polling Handler accepted...(%s/%d/%d)\n",userid.c_str(),connections[userid].fd_polling.get() != NULL, connections[userid].fd_request.get() != NULL);
  if (connections.count(userid) > 0 && connections[userid].fd_polling.get() != NULL) {
    connections[userid].fd_polling->error("Wrong polling");
    connections[userid].fd_polling.reset();// = ehttp_ptr();
  }
  //  dprintf("connection created(%s)\n", userid.c_str());
  connections[userid].fd_polling = ehttp_ptr(obj);
  connections[userid].status = 1;
  return execute_polling(userid);
}

int upload_handler( ehttp_ptr obj ) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  log(0) << "Upload Handler accepted..." << endl;
  if (connections.count(userid) == 0) {
    obj->error("Wrong uploading _ connections doesn't exist");
    return EHTTP_ERR_GENERIC;
  }

  log(0) << "Set agent info" << endl;
  connections[userid].fd_polling = ehttp_ptr(obj);
  if (connections[userid].status == 1) {
    obj->error("Wrong uploading _ status=1");
    return EHTTP_ERR_GENERIC;
  }

  string jsondata = obj->getPostParams()["jsondata"];
  if (connections[userid].key == obj->getPostParams()["incrkey"]) {
    ehttp_ptr request = connections[userid].fd_request;
    if (request->isClose()) {
      dieConnection(userid, "Wrong uploading _ request is closed");
      return EHTTP_ERR_GENERIC;
    }
    request->out_set_file("request.json");

    log(0) << "jsondata : " << jsondata << endl;
    request->out_replace_token("jsondata",jsondata);
    request->out_replace();
    request->out_commit();
    request->close();

    obj->out_set_file("polling.json");
    obj->out_replace_token("incrkey","");
    obj->out_replace_token("command","");
    obj->out_replace_token("requestpath","");
    obj->out_replace();
    int ret = obj->out_commit();
    obj->close();

    log(0) << "Connection close..." << endl;
    connections.erase(userid);
    return ret;
  } else {
    obj->error("FAIL : Key doesn't match!");
    return EHTTP_ERR_GENERIC;
  }
}

void *main_thread(void *arg) {
  Thread *thread = (Thread *)arg;

  while (1) {
    // fetch a new job.
    int socket;
    for(;;) {
      sleep(1);
      pthread_mutex_lock(&new_connection_mutex);
      if (!thread->conn_pool->empty()) {
        socket = thread->conn_pool->front();
        thread->conn_pool->pop_front();
        pthread_mutex_unlock(&new_connection_mutex);
        break;
      }
      pthread_mutex_unlock(&new_connection_mutex);
    }

    // job
    ehttp_ptr http = ehttp_ptr(new ehttp());
    http->init();
    http->add_handler("/polling", polling_handler);
    http->add_handler("/upload", upload_handler);
    http->add_handler("/request", request_handler);
    http->add_handler("/login", login_handler);
    http->add_handler(NULL, handleDefault);

    http->parse_request(socket); 

    // if (http->isClose()) {
    //   delete http;
    // }

    // pthread_mutex_lock(&new_connection_mutex);
    // pthread_mutex_unlock(&new_connection_mutex);

  }
}




int main() {
  struct sockaddr_in srv;
  int clifd;
  int i;

  signal(SIGPIPE, SIG_IGN);

  deque<int> conn_pool;
  Thread threads[MAX_THREAD];

  /* create threads */
  for(i = 0; i < MAX_THREAD; i++) {
    threads[i].conn_pool = &conn_pool;
    pthread_create(&threads[i].tid, NULL, &main_thread, (void *)&threads[i]);
  }

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("sockfd\n");
    exit(1);
  }

  int yes = 1;
  setsockopt(listenfd, SOL_SOCKET,SO_REUSEADDR, (void *)&yes, sizeof(int));

  bzero(&srv, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_addr.s_addr = htonl(INADDR_ANY);
  srv.sin_port = htons(PORT);
  if( bind(listenfd, (struct sockaddr *) &srv, sizeof(srv)) < 0) {
    perror("bind\n");
    exit(1);
  }

  listen(listenfd, 1024);

  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(struct sockaddr_in);

  log(0) << "Listen..." << endl;
  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      perror("accept\n");
      exit(1);
    }
    //    nonblock(clifd);
    pthread_mutex_lock(&new_connection_mutex);
    log(0) << "Accepted... " << clifd << "  / Queue size : " << clifd << " / count : " << conn_pool.size() << endl;
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}
