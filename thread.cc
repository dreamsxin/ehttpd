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
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace std;

#define MAX_THREAD 20
#define PORT 8080

#define dprintf printf
// int nprintf(const char *format, ...) {
//   return 0;
// }

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

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

void removeConnection(string userid) {
  dprintf("remove Connection\n");
  if (connections.count(userid) == 0) {
    return;
  }
  if (connections[userid].fd_polling != NULL) {
    connections[userid].fd_polling->close(); 
  }
  if (connections[userid].fd_request != NULL) {
    connections[userid].fd_request->close();
  }
  connections.erase(userid);
}

void dieConnection(string userid, const string &error_message) {
  dprintf("Die Connection\n");
  if (connections.count(userid) == 0) {
    return;
  }
  if (connections[userid].fd_polling != NULL) {
    connections[userid].fd_polling->error(error_message);
  }
  if (connections[userid].fd_request != NULL) {
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
  dprintf("Login Handler!\n");
  if ((obj->getPostParams()).count("email") > 0) {
    dprintf("POST\n");
    string email = obj->getPostParams()["email"];
    string padpasskey = obj->getPostParams()["padpasskey"];
    string installkey = obj->getPostParams()["installkey"];
    string user_id, macaddress;
    if (padpasskey.length() > 0 && db.login(email, padpasskey, &user_id, &macaddress)) {
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + user_id;
      session[user_id]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      dprintf("%s login success\n",user_id.c_str());

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + user_id;
      session[user_id]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      dprintf("%s login success\n",user_id.c_str());

    } else {
      string msg = user_id + " login fail";
      obj->error(msg);

    }
    dprintf("mac : %s\n",macaddress.c_str());

  } else {

    dprintf("GET\n");
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

int execute_polling(string userid) {
  if (connections[userid].fd_polling.get() != NULL && connections[userid].fd_request.get() != NULL) {
    printf("Execute_polling : fd_polling = %d    <==>  fd_request = %d\n",connections[userid].fd_polling->getFD(), connections[userid].fd_request->getFD());
    ehttp_ptr obj = connections[userid].fd_polling;
    obj->out_set_file("polling.json");
    obj->out_replace_token("incrkey",connections[userid].key);
    obj->out_replace_token("command","getfolderlist");
    string requestpath = connections[userid].requestpath;
    obj->addslash(&requestpath);

    obj->out_replace_token("requestpath",requestpath);
    obj->out_replace();
    int ret = obj->out_commit();
    //    delete connections[userid].fd_polling.get();
    connections[userid].fd_polling->close();
    dprintf("(%d) Waiting uploading... Status change ( 1-> 2 )\n", obj->getFD());
    connections[userid].status = 2;
    return ret;
  }

 return 0;
}

int request_handler( ehttp_ptr obj ) {
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  dprintf("Request Handler accepted...\n");
  if (connections.count(userid) > 0) {
    removeConnection("die");
  }
  dprintf("connection created(%s)\n",userid.c_str());

  ehttp_ptr null_pointer;
  connections[userid] = connection(ehttp_ptr(obj), "", "", "", null_pointer, 1);
  connections[userid].command = obj->getUrlParams()["command"];
  connections[userid].requestpath = obj->getUrlParams()["requestpath"];
  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();
  connections[userid].key = to_string(u);
  connections[userid].status=1;
  dprintf("Set request info (fd_request=%d, command=%s, requestpath=%s, key=%s,status=%d)\n",(connections[userid].fd_request)->getFD(), connections[userid].command.c_str(),connections[userid].requestpath.c_str(),connections[userid].key.c_str(), connections[userid].status);
  return execute_polling(userid);
}

int polling_handler( ehttp_ptr obj ) {
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  dprintf("Polling Handler accepted...\n");
  if (connections.count(userid) > 0 && connections[userid].status == 2) {
    connections[userid].fd_polling->error("Wrong polling");
  } else if (connections.count(userid) == 0) {
    dprintf("connection created(%s)\n",userid.c_str());
    ehttp_ptr null_pointer;
    connections[userid] = connection(null_pointer, "", "", "", null_pointer, 1);
  }
  dprintf("Set agent info\n");
  connections[userid].fd_polling = ehttp_ptr(obj);
  return execute_polling(userid);
}

int upload_handler( ehttp_ptr obj ) {
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  dprintf("Upload Handler accepted...\n");
  if (connections.count(userid) == 0) {
    return EHTTP_ERR_GENERIC;
  }

  dprintf("Set agent info\n");
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

    dprintf("jsondata : %s\n",jsondata.c_str());
    request->out_replace_token("jsondata",jsondata);
    request->out_replace();
    request->out_commit();
    //    delete request;
    request->close();

    obj->out_set_file("polling.json");
    obj->out_replace_token("incrkey","");
    obj->out_replace_token("command","");
    obj->out_replace_token("requestpath","");
    obj->out_replace();
    int ret = obj->out_commit();
    //    delete obj;
    obj->close();
    dprintf("Connection close...\n");
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
    // dprintf("\n=============\n");
    // cnt++;
    // dprintf("Start parsing(%d)\n",cnt);
    // dprintf("=============\n");
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
    // dprintf("=============\n");
    // cnt--;
    // dprintf("End parsing(%d)\n",cnt);
    // dprintf("=============\n\n");
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

  dprintf("Listen...\n");
  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      perror("accept\n");
      exit(1);
    }
    nonblock(clifd);
    pthread_mutex_lock(&new_connection_mutex);
    dprintf("Accepted... %d  / Queue size : %d / count : %d\n", clifd, conn_pool.size(),cnt);
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}
