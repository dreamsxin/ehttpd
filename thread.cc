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

using namespace std;

#define MAX_THREAD 100
#define PORT 3357

#define dprintf printf
int nprintf(const char *format, ...) {
  return 0;
}

int listenfd;
int cookie_index=1;
typedef shared_ptr<ehttp> ehttp_ptr;
typedef struct {
  pthread_t tid;
  deque<int> *conn_pool;
} Thread;

class connection {
public:
  shared_ptr<ehttp> fd_pad;
  string command;
  string requestpath;
  string key;
  ehttp_ptr fd_agent;
  int status;
  connection(){};
  connection(ehttp* fd_pad, string command, string requestpath, string key, ehttp* fd_agent, int status):fd_pad(fd_pad),command(command),requestpath(requestpath),key(key),fd_agent(fd_agent),status(status){};
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

int handleDefault(ehttp *obj) {
  dprintf("HandleDefault!\n");
  obj->out_set_file("helloworld_template.html");
  obj->out_replace_token("MESSAGE","Hello World");
  obj->out_replace();
  int ret = obj->out_commit();

  dprintf("Close(%d)\n",obj->getFD());
  close(obj->getFD());
  dprintf("Connection close...\n");

  return ret;
}

int login_handler(ehttp *obj) {
  dprintf("Login Handler!\n");
  if ((obj->getPostParams()).count("email") > 0) {
    dprintf("POST\n");
    string email = obj->getPostParams()["email"];
    string password = obj->getPostParams()["password"];
    dprintf("%s || %s\n",email.c_str(),password.c_str());
    string user_id, macaddress;
    string result = "Fail";
    if (db.login(email, password, &user_id, &macaddress)) {
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + user_id;
      session[user_id]["user_id"] = user_id;
      result = "OK";
    }
    obj->out_set_file("helloworld_template.html");
    obj->out_replace_token("MESSAGE", result);
  } else {
    dprintf("GET\n");
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  dprintf("Close(%d)\n",obj->getFD());
  close(obj->getFD());
  dprintf("Connection close...\n");
  return ret;
}

int execute_connection(string userid) {
  if (connections[userid].fd_agent != NULL && connections[userid].fd_pad != NULL) {
    printf("Execute_connection : fd_agent = %d    <==>  fd_pad = %d\n",connections[userid].fd_agent->getFD(), connections[userid].fd_pad->getFD());
    ehttp *obj = connections[userid].fd_agent.get();
    obj->out_set_file("helloworld_template.html");
    obj->out_replace_token("MESSAGE","{command:" + connections[userid].command + ",requestpath:" + connections[userid].requestpath);
    obj->out_replace();
    int ret = obj->out_commit();
    dprintf("Close(%d)\n",(connections[userid].fd_agent)->getFD());
    close((connections[userid].fd_agent)->getFD());
    dprintf("Status change ( 1-> 2 )\n");
    connections[userid].status = 2;
    return ret;
  }
 
 return 0;
}

int pad_handler( ehttp *obj ) {
  string userid = "bigeye";

  dprintf("PAD Handler accepted...\n");
  if (connections.count(userid) == 0) {
    dprintf("connection created(%s)\n",userid.c_str());
    connections[userid] = connection(NULL, "", "", "", NULL, 1);
  }

  connections[userid].fd_pad = ehttp_ptr(obj);
  connections[userid].command = obj->getUrlParams()["command"];
  connections[userid].requestpath = obj->getUrlParams()["requestpath"];
  connections[userid].key=userid;
  connections[userid].status=1;
  dprintf("Set pad info (fd_pad=%d, command=%s, requestpath=%s, key=%s,status=%d)\n",(connections[userid].fd_pad)->getFD(), connections[userid].command.c_str(),connections[userid].requestpath.c_str(),connections[userid].key.c_str(), connections[userid].status);
  return execute_connection(userid);
}

int agent_handler( ehttp *obj ) {
  string userid = "bigeye";

  dprintf("AGENT Handler accepted...\n");
  if (connections.count(userid) == 0) {
    dprintf("connection created(%s)\n",userid.c_str());
    connections[userid] = connection(NULL, "", "", "", NULL, 1);
  }
  dprintf("Set agent info\n");
  connections[userid].fd_agent = ehttp_ptr(obj);
  if (connections[userid].status == 1) {
    return execute_connection(userid);
  } else {
    string jsondata = obj->getPostParams()["jsondata"];
    if (connections[userid].key == obj->getPostParams()["key"]) {
      ehttp *pad = connections[userid].fd_pad.get();
      pad->out_set_file("helloworld_template.html");
      pad->out_replace_token("MESSAGE","OK : " + jsondata);
      pad->out_replace();
      pad->getResponseHeader()["jsondata"] = jsondata;
      pad->out_commit();
      dprintf("Close(%d)\n",pad->getFD());
      close(pad->getFD());
      obj->out_set_file("helloworld_template.html");
      obj->out_replace_token("MESSAGE","OK : " + jsondata);
      obj->out_replace();
      int ret = obj->out_commit();
      dprintf("Close(%d)\n",obj->getFD());
      close(obj->getFD());
      dprintf("Connection close...\n");
      connections.erase(userid);
      return ret;
    } else {
      obj->out_set_file("helloworld_template.html");
      obj->out_replace_token("MESSAGE","FAIL : Key doesn't match!");
      obj->out_replace();
      int ret = obj->out_commit();
      dprintf("Close(%d)\n",obj->getFD());
      close(obj->getFD());
      return ret;
    }
  }
}

void *main_thread(void *arg) {
  Thread *thread = (Thread *)arg;

  while (1) {
    // fetch a new job.
    int socket;
    for(;;) {
      pthread_mutex_lock(&new_connection_mutex);
      if (!thread->conn_pool->empty()) {
        socket = thread->conn_pool->front();
        thread->conn_pool->pop_front();
        pthread_mutex_unlock(&new_connection_mutex);
        break;
      }
      pthread_mutex_unlock(&new_connection_mutex);
      sleep(1);
    }

    // job
    dprintf("\n=============\n");
    dprintf("Start parsing\n");
    dprintf("=============\n");
    ehttp *http = new ehttp();
    http->init();
    http->add_handler("/agent", agent_handler);
    http->add_handler("/pad", pad_handler);
    http->add_handler("/login", login_handler);
    http->add_handler(NULL, handleDefault);

    map<string, string> cookie = map<string, string>();
    http->parse_request(socket, &cookie);
    dprintf("=============\n");
    dprintf("End parsing\n");
    dprintf("=============\n\n");
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
    //    nonblock(clifd);
    dprintf("Accepted... %d\n", clifd);
    pthread_mutex_lock(&new_connection_mutex);
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}

