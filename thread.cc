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

#include "epoll.h"
#include "embedhttp.h"
#include "connection.h"
#include "download.h"
#include "dr_mysql.h"
#include "log.h"
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

map <string, connection > connections;
map <string, map<string, string> > session;
map <string, download_ptr> downloads;

pthread_mutex_t mutex_connections = PTHREAD_MUTEX_INITIALIZER;

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

void safeClose(string userid, ehttp *obj) {
  obj->close();

  pthread_mutex_lock(&mutex_connections);
  if (connections[userid].fd_request.get() == obj) {
    // Request
    connections[userid].fd_request.reset();
  } else if (connections[userid].fd_polling.get() == obj) {
    // Polling
    connections[userid].fd_polling.reset();
  }
  pthread_mutex_unlock(&mutex_connections);
}

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

void removeConnection(string userid) {
  // Must unlock mutex when function is started.
  log(1) << "remove Connection" << endl;
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
  log(1) << "Die Connection" << endl;
  if (connections.count(userid) == 0) {
    return;
  }
  pthread_mutex_lock(&mutex_connections);
  if (connections[userid].fd_polling.get() != NULL) {
    connections[userid].fd_polling->error(error_message);
  }
  if (connections[userid].fd_request.get() != NULL) {
    connections[userid].fd_request->error(error_message);
  }
  connections.erase(userid);
  pthread_mutex_unlock(&mutex_connections);
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
  log(1) << "Login Handler!" << endl;
  if ((obj->getPostParams()).count("email") > 0) {
    log(1) << "POST" << endl;
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
      log(1) << user_id << " login success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid;
      session[sessionid]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      log(1) << user_id << " login success" << endl;
    } else {
      string msg = user_id + " login fail";
      obj->error(msg);
    }
    log(1) << "mac : " << macaddress << endl;
  } else {
  log(1) << "GET" << endl;
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  //  log(1) << "Login Handler finished" << endl;

  return ret;
}

int execute_downloading(string incrkey) {
  //TODO: execute downloading using epoll

  map<string, string> mimetype;
  mimetype["hwp"] = "application/x-hwp";
  mimetype["doc"] = "application/msword";
  mimetype["docx"] = "application/x-zip-compressed";
  mimetype["xls"] = "application/vnd.ms-excel";
  mimetype["xlsx"] = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
  mimetype["ppt"] = "application/vnd.ms-powerpoint";
  mimetype["pptx"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  mimetype["pdf"] = "application/pdf";
  mimetype["rtf"] = "application/rtf";
  mimetype["gul"] = "application/gul";
  mimetype["gif"] = "image/gif";
  mimetype["jpg"] = "image/jpeg";
  mimetype["png"] = "image/png";
  mimetype["mp3"] = "audio/mpeg";
  mimetype["wav"] = "audio/x-wav";
  mimetype["txt"] = "text/plain";
  mimetype["key"] = "application/octet-stream";
  mimetype["dwg"] = "application/dwg";

  log(1) << "Execute_downloading() called." << endl;

  download_ptr dn = downloads[incrkey];
  downloads.erase(incrkey);

  stringstream strContentLength;
  strContentLength << dn->fd_upload->getContentLength();
  dn->fd_download->getResponseHeader()["Cache-Control"] = "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  dn->fd_download->getResponseHeader()["Pragma"] = "no-cache";
  dn->fd_download->getResponseHeader()["Expires"] = "0";
  dn->fd_download->getResponseHeader()["Content-Disposition"] = "attachment; filename=" + dn->filename;
  dn->fd_download->getResponseHeader()["Content-Length"] = strContentLength.str();
  dn->fd_download->getResponseHeader()["Content-Transfer-Encoding"] = "binary";
  dn->fd_download->getResponseHeader()["Connection"] = "close";

  size_t found = dn->filename.rfind(".");
  if (found == string::npos || mimetype.count(dn->filename.substr(found+1)) == 0) {
    dn->fd_download->getResponseHeader()["Content-Type"] = "application/octet-stream";
  } else {
    dn->fd_download->getResponseHeader()["Content-Type"] = mimetype[dn->filename.substr(found+1)];
  }
  dn->fd_download->out_commit();


  log(1) << "Download start(" << dn->filename <<")" << endl;
  int res = dn->fd_download->pSend((void *) (dn->fd_download->getFD()),
                                   dn->fd_upload->message.c_str(),
                                   dn->fd_upload->message.length());

  if (res < 0 || res != (int)dn->fd_upload->message.length()) {
    log(1) << "Error first sending" << endl;
    dn->close();
    return EHTTP_ERR_GENERIC;
  }

  dn->remaining = dn->fd_upload->getContentLength();
  dn->remaining -= res;

  DrEpoll::get_mutable_instance().add(dn);

  /*
  Byte buffer[INPUT_BUFFER_SIZE];
  while (sent_length < contentlength) {
    int r = fd_upload->pRecv((void*) (fd_upload->getFD()),  buffer, INPUT_BUFFER_SIZE - 1);
    if(r < 0) {
      fd_download->close();
      fd_upload->close();
      return EHTTP_ERR_GENERIC;
    } else if (r == 0) {
      continue;
    }
    int ret = fd_download->pSend((void *) (fd_download->getFD()), buffer, r);
    sent_length += r;
  }
  log(1) << "Download finish(" << filename <<")" << endl;

  fd_download->close();
  fd_upload->close();*/
  return 1;
}

int execute_polling(string userid) {
  pthread_mutex_lock(&mutex_connections);
  connection conn = connections[userid];
  log(1) << "Execute_polling(" << userid << "/" << (conn.fd_polling.get() != NULL) << "/" << (conn.fd_request.get() != NULL) << ")" << endl;
  if (conn.fd_polling.get() == NULL || conn.fd_request.get() == NULL) {
    pthread_mutex_unlock(&mutex_connections);
    return EHTTP_ERR_GENERIC;
  }
  pthread_mutex_unlock(&mutex_connections);

  log(1) << "Execute_polling : fd_polling = " << conn.fd_polling->getFD() << "   <==>  fd_request = " << conn.fd_request->getFD() << endl;
  ehttp_ptr obj = conn.fd_polling;
  obj->out_set_file("polling.json");
  obj->out_replace_token("incrkey", conn.key);
  obj->out_replace_token("command", conn.command);
  string requestpath = conn.requestpath;
  obj->addslash(&requestpath);

  obj->out_replace_token("requestpath", requestpath);
  obj->out_replace();
  int ret = obj->out_commit();
  log(1) << "(" << obj->getFD() << ") Waiting uploading... Status change ( polling -> uploading )" << endl;
  pthread_mutex_lock(&mutex_connections);
  connections[userid].status = "uploading";
  pthread_mutex_unlock(&mutex_connections);
  safeClose(userid, obj.get());
  return ret;
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
    safeClose(userid, obj.get());
    return EHTTP_ERR_OK;
  }
  pthread_mutex_lock(&mutex_connections);
  connection conn = connections[userid];
  log(1) << "Request Handler accepted...(" << userid << "/" << (conn.fd_polling.get() != NULL) << "/" << (conn.fd_request.get() != NULL) << ")" << endl;
  if (connections.count(userid) > 0 && conn.fd_request.get() != NULL) {
    removeConnection(userid);
  }
  connections[userid].fd_request = ehttp_ptr(obj);
  connections[userid].command = obj->getUrlParams()["command"];
  connections[userid].requestpath = obj->getUrlParams()["requestpath"];
  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();
  connections[userid].key = to_string(u); //TODO(bigeye): make more shorter uuid
  connections[userid].status = "polling";
  log(1) << "Set request info (fd_request=" << (connections[userid].fd_request)->getFD() << ", command=" << connections[userid].command << ", requestpath=" << connections[userid].requestpath <<", key=" << connections[userid].key << ",status=" << connections[userid].status << ")" << endl;
  pthread_mutex_unlock(&mutex_connections);
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
  log(1) << "Polling handler accepted..." << endl;

  pthread_mutex_lock(&mutex_connections);
  if (connections.count(userid) > 0 && connections[userid].fd_polling.get() != NULL) {
    connections[userid].fd_polling->error("Wrong polling. Previous polling is dead.");
    connections[userid].fd_polling.reset();// = ehttp_ptr();
  }
  //  dprintf("connection created(%s)\n", userid.c_str());
  connections[userid].fd_polling = ehttp_ptr(obj);
  connections[userid].status = "polling";
  pthread_mutex_unlock(&mutex_connections);
  return execute_polling(userid);
}

int upload_handler( ehttp_ptr obj ) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  log(1) << "Upload Handler accepted..." << endl;
  if (connections.count(userid) == 0) {
    obj->error("Wrong uploading _ connections doesn't exist");
    return EHTTP_ERR_GENERIC;
  }

  log(1) << "Set agent info" << endl;

  pthread_mutex_lock(&mutex_connections);
  connections[userid].fd_polling = ehttp_ptr(obj);
  connection conn = connections[userid];
  pthread_mutex_unlock(&mutex_connections);

  if (conn.status == "polling") {
    obj->error("Wrong uploading _ status=polling");
    return EHTTP_ERR_GENERIC;
  }
  string jsondata = obj->getPostParams()["jsondata"];
  string incrkey = obj->getPostParams()["incrkey"];
  string command = obj->getPostParams()["command"];
  if (conn.key == incrkey) {
    ehttp_ptr request = conn.fd_request;
    if (request->isClose()) {
      dieConnection(userid, "Wrong uploading _ request is closed");
      return EHTTP_ERR_GENERIC;
    }

    if (command == "getfile") {
      request->out_set_file("request.json");
      request->out_replace_token("jsondata","{\"filedownurl\":\"http://dlp.jiran.com:8080/download?incrkey=" + incrkey + "\"}");
      request->out_replace();
      request->out_commit();
      safeClose(userid, request.get());

      string requestpath = conn.requestpath;
      int found = requestpath.rfind("\\");
      if (found != (int)string::npos) {
        requestpath = requestpath.substr(found+1);
      }

      downloads[incrkey] = download_ptr(new download(
                             ehttp_ptr(obj),
                             ehttp_ptr(),
                             incrkey,
                             requestpath));

      pthread_mutex_lock(&mutex_connections);
      connections.erase(userid);
      pthread_mutex_unlock(&mutex_connections);
      return EHTTP_ERR_OK;

    } else {
      request->out_set_file("request.json");

      log(1) << "jsondata : " << jsondata << endl;
      request->out_replace_token("jsondata",jsondata);
      request->out_replace();
      request->out_commit();
      safeClose(userid, request.get());

      obj->out_set_file("polling.json");
      obj->out_replace_token("incrkey","");
      obj->out_replace_token("command","");
      obj->out_replace_token("requestpath","");
      obj->out_replace();
      int ret = obj->out_commit();
      safeClose(userid, obj.get());

      log(1) << "Connection close..." << endl;

      pthread_mutex_lock(&mutex_connections);
      connections.erase(userid);
      pthread_mutex_unlock(&mutex_connections);
      return ret;
    }
  } else {
    obj->error("FAIL : Key doesn't match!");
    return EHTTP_ERR_GENERIC;
  }
}

int download_handler( ehttp_ptr obj ) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  log(1) << "Download Handler accepted..." << endl;
  string incrkey = obj->getPostParams()["incrkey"];
  if (downloads.count(incrkey) == 0) {
    obj->close();
    return EHTTP_ERR_GENERIC;
  }
  downloads[incrkey]->fd_download = ehttp_ptr(obj);
  execute_downloading(incrkey);
  return 1;
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
    http->add_handler("/download", download_handler);
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

  signal(SIGPIPE, SIG_IGN);

  deque<int> conn_pool;
  Thread threads[MAX_THREAD];

  /* create threads */
  for(int i = 0; i < MAX_THREAD; i++) {
    threads[i].conn_pool = &conn_pool;
    pthread_create(&threads[i].tid, NULL, &main_thread, (void *)&threads[i]);
  }

  DrEpoll::get_mutable_instance().init();

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

  log(1) << "Listen..." << endl;
  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      perror("accept\n");
      exit(1);
    }
    //    nonblock(clifd);
    pthread_mutex_lock(&new_connection_mutex);
    log(1) << "Accepted... " << clifd << "  / Queue size : " << clifd << " / count : " << conn_pool.size() << endl;
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}
