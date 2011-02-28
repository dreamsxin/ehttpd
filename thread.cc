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
#include <queue>
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

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>

using namespace std;
namespace po = boost::program_options;

#define MAX_THREAD 20
int cnt=0;
int listenfd;
int cookie_index=1;
int PORT = 8000;

typedef struct {
  pthread_t tid;
  deque<int> *conn_pool;
} Thread;

map <string, RequestPtr > requests;  // userid as key
map <string, PollingPtr > pollings;  // userid as key

map <string, RequestPtr > requests_key;  // inckey as key
map <string, UploadPtr > uploads;  // inckey as key

queue <RequestPtr> queue_requests;
queue <PollingPtr> queue_pollings;
queue <RequestPtr> queue_requests_key;
queue <UploadPtr> queue_uploads;

pthread_mutex_t mutex_requests = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_pollings = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutex_requests_key = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_uploads = PTHREAD_MUTEX_INITIALIZER;

map <string, map<string, string> > session;

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

DrMysql db;

string hostname = "dlp.jiran.com:8080";

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

void loginFail (EhttpPtr obj) {
  obj->out_set_file("stringtemplate.json");
  obj->out_replace_token("string","Login required");
  obj->out_replace();
  obj->out_commit();
  obj->close();
}

int handleDefault(EhttpPtr obj) {
  obj->out_set_file("helloworld_template.html");
  obj->out_replace_token("MESSAGE", "Hello World");
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

int login_handler(EhttpPtr obj) {
  log(1) << "Login Handler called" << endl;
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
      obj->out_replace_token("hostname", hostname);
      log(1) << email << " login success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid;
      session[sessionid]["user_id"] = user_id;
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->out_replace_token("hostname", hostname);
      log(1) << email << " login success" << endl;
    } else {
      string msg = user_id + " login fail";
      obj->error(msg);
    }
    //    log(0) << "mac : " << macaddress << endl;
  } else {
    log(0) << "GET" << endl;
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  //  log(1) << "Login Handler finished" << endl;

  return ret;
}

int execute_downloading(UploadPtr up, DownloadPtr dn) {
  //TODO: execute downloading using epoll
  //TODO(bigeye): move to ...
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

  string filename = dn->requestpath;
  size_t found = filename.rfind("\\");
  if (found != (int)string::npos) {
    filename = filename.substr(found+1);
  }

  stringstream strContentLength;
  strContentLength << up->ehttp->getContentLength();
  dn->ehttp->getResponseHeader()["Cache-Control"] = "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  dn->ehttp->getResponseHeader()["Pragma"] = "no-cache";
  dn->ehttp->getResponseHeader()["Expires"] = "0";
  dn->ehttp->getResponseHeader()["Content-Disposition"] = "attachment; filename=" + filename;
  dn->ehttp->getResponseHeader()["Content-Length"] = strContentLength.str();
  dn->ehttp->getResponseHeader()["Content-Transfer-Encoding"] = "binary";
  dn->ehttp->getResponseHeader()["Connection"] = "close";

  found = filename.rfind(".");
  if (found == string::npos || mimetype.count(filename.substr(found+1)) == 0) {
    dn->ehttp->getResponseHeader()["Content-Type"] = "application/octet-stream";
  } else {
    dn->ehttp->getResponseHeader()["Content-Type"] = mimetype[filename.substr(found+1)];
  }
  dn->ehttp->out_commit();

  int res = dn->ehttp->pSend((void *) (dn->ehttp->getFD()),
              up->ehttp->message.c_str(),
              up->ehttp->message.length());

  log(1) << "Download start:" << filename << " " << strContentLength.str() << " " << res << endl;

  if (res < 0 || res != (int)up->ehttp->message.length()) {
    log(1) << "Error first sending" << endl;
    dn->ehttp->close();
    up->ehttp->close();
    return EHTTP_ERR_GENERIC;
  }

  dn->remaining = up->ehttp->getContentLength();
  dn->remaining -= res;

  TransferPtr transfer(new Transfer);
  transfer->up = up;
  transfer->dn = dn;

  DrEpoll::get_mutable_instance().add(transfer);
  return 0;
}

int execute_polling(RequestPtr request, PollingPtr polling) {
  polling->userid = request->userid;
  polling->key = request->key;
  polling->command = request->command;
  polling->requestpath = request->requestpath;

  polling->ehttp->out_set_file("polling.json");
  polling->ehttp->out_replace_token("incrkey", polling->key);
  polling->ehttp->out_replace_token("command", polling->command);
  string path = polling->requestpath;

  polling->ehttp->addslash(&path);
  polling->ehttp->out_replace_token("requestpath", path);
  polling->ehttp->out_replace();
  int ret = polling->ehttp->out_commit();
  polling->ehttp->close();
  if (ret != 0) {  // if error
    return ret;
  }

  pthread_mutex_lock(&mutex_requests_key);
  requests_key[request->key] = request;
  pthread_mutex_unlock(&mutex_requests_key);
  queue_requests_key.push(request);

  log(1) << "execute_polling: End and assign request:" << request->ehttp->getFD() << endl;
  return ret;
}

///////////////////////////////////////////////////

int request_handler(EhttpPtr obj) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }

  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];
  if (obj->getUrlParams()["command"] == "logout") {
    session.erase(session_id);
    obj->out_set_file("request.json");
    obj->out_replace_token("jsondata", "");
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  }

  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();

  RequestPtr request(new Request);
  request->ehttp = obj;
  request->userid = userid;
  request->command = obj->getUrlParams()["command"];
  request->requestpath = obj->getUrlParams()["requestpath"];
  request->key = to_string(u); //TODO(bigeye): make more shorter uuid

  // fetch polling.
  PollingPtr polling;
  pthread_mutex_lock(&mutex_pollings);
  if (pollings.count(userid)) {
    polling = pollings[userid];
    pollings.erase(userid);
  } else {
    pthread_mutex_lock(&mutex_requests);
    requests[userid] = request;
    pthread_mutex_unlock(&mutex_requests);
    queue_requests.push(request);
  }
  pthread_mutex_unlock(&mutex_pollings);

  if (polling.get() != NULL) {
    log(1) << "Request: Execute polling ("
           << request->ehttp->getFD() << ","
           << polling->ehttp->getFD() << ")"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;

    execute_polling(request, polling);
  } else {
    log(1) << "Request: Waiting ("
           << obj->getFD() << ")"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;
  }
  return 0;
}


int polling_handler(EhttpPtr obj) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  PollingPtr polling(new Polling);
  polling->ehttp = obj;

  // fetch request.
  RequestPtr request;
  pthread_mutex_lock(&mutex_requests);
  if (requests.count(userid)) {
    request = requests[userid];
    requests.erase(userid);
  } else {
    pthread_mutex_lock(&mutex_pollings);
    pollings[userid] = polling;
    pthread_mutex_unlock(&mutex_pollings);
  }
  pthread_mutex_unlock(&mutex_requests);

  if (request.get() != NULL) {
    log(1) << "Request: Execute polling ("
           << request->ehttp->getFD() << ","
           << polling->ehttp->getFD() << ")"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;

    execute_polling(request, polling);
  } else {
    log(1) << "Request: Waiting ("
           << obj->getFD() << ")" << endl;
    queue_pollings.push(polling);
  }
  return 0;
}

int upload_handler(EhttpPtr obj) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }

  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  string jsondata = obj->getPostParams()["jsondata"];
  string key = obj->getPostParams()["incrkey"];
  string command = obj->getPostParams()["command"];

  // fetch request.
  RequestPtr request;
  pthread_mutex_lock(&mutex_requests_key);
  if (requests_key.count(key)) {
    request = requests_key[key];
    requests_key.erase(key);
  }
  pthread_mutex_unlock(&mutex_requests_key);

  if (request.get() == NULL) {
    return obj->error("Wrong uploading Key doesn't exist");
  }

  if (command == "getfile") {
    request->ehttp->out_set_file("request.json");
    request->ehttp->out_replace_token("jsondata","{\"filedownurl\":\"http://" + hostname + "/download?incrkey=" + key + "\"}");
    request->ehttp->out_replace();
    request->ehttp->out_commit();
    request->ehttp->close();

    UploadPtr upload(new Upload);
    upload->ehttp = obj;
    upload->userid = request->userid;
    upload->key = request->key;
    upload->command = request->command;
    upload->requestpath = request->requestpath;

    pthread_mutex_lock(&mutex_uploads);
    uploads[upload->key] = upload;
    pthread_mutex_unlock(&mutex_uploads);
    queue_uploads.push(upload);

    // TODO LOG

  } else {
    log(0) << "jsondata : " << jsondata << endl;
    request->ehttp->out_set_file("request.json");
    request->ehttp->out_replace_token("jsondata", jsondata);
    request->ehttp->out_replace();
    request->ehttp->out_commit();
    request->ehttp->close();

    obj->out_set_file("polling.json");
    obj->out_replace_token("incrkey","");
    obj->out_replace_token("command","");
    obj->out_replace_token("requestpath","");
    obj->out_replace();
    obj->out_commit();
    obj->close();

    // TODO LOG
  }

  return 0;
}

int download_handler(EhttpPtr obj) {
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = session[session_id]["user_id"];

  string key = obj->getPostParams()["incrkey"];

  // fetch request.
  UploadPtr upload;
  pthread_mutex_lock(&mutex_uploads);
  if (uploads.count(key)) {
    upload = uploads[key];
    uploads.erase(key);
  }
  pthread_mutex_unlock(&mutex_uploads);

  if (upload.get() == NULL) {
    return obj->error("Wrong uploading Key doesn't exist");
  }

  DownloadPtr download(new Download);
  download->ehttp = obj;
  download->userid = upload->userid;
  download->key = upload->key;
  download->command = upload->command;
  download->requestpath = upload->requestpath;

  execute_downloading(upload, download);
  return 1;
}

void *timeout_killer(void *arg) {
  while(1) {
    sleep(1);
    time_t now = time(NULL);
    while(!queue_requests.empty()) {
      RequestPtr ptr = queue_requests.front();
      if (ptr->ehttp->timestamp + 10 <= now) {
        queue_requests.pop();
        if (!ptr.unique()) {
          pthread_mutex_lock(&mutex_requests);
          requests.erase(ptr->userid);
          pthread_mutex_unlock(&mutex_requests);
        }
      } else {
        break;
      }
    }

    while(!queue_pollings.empty()) {
      PollingPtr ptr = queue_pollings.front();
      if (ptr->ehttp->timestamp + 10 <= now) {
        queue_pollings.pop();
        if (!ptr.unique()) {
          pthread_mutex_lock(&mutex_pollings);
          pollings.erase(ptr->userid);
          pthread_mutex_unlock(&mutex_pollings);
        }
      } else {
        break;
      }
    }

    while(!queue_requests_key.empty()) {
      RequestPtr ptr = queue_requests_key.front();
      if (ptr->ehttp->timestamp + 10 <= now) {
        queue_requests_key.pop();
        if (!ptr.unique()) {
          pthread_mutex_lock(&mutex_requests_key);
          requests_key.erase(ptr->key);
          pthread_mutex_unlock(&mutex_requests_key);
        }
      } else {
        break;
      }
    }

    while(!queue_uploads.empty()) {
      UploadPtr ptr = queue_uploads.front();
      if (ptr->ehttp->timestamp + 10 <= now) {
        queue_uploads.pop();
        log(1) << "UPLOADQUEUE:" << ptr.use_count() << "(" << ptr->ehttp->timestamp <<" / "<<now<<")"<<endl;
        if (!ptr.unique()) {
          pthread_mutex_lock(&mutex_uploads);
          uploads.erase(ptr->key);
          pthread_mutex_unlock(&mutex_uploads);
        }
      } else {
        break;
      }
    }
  }
}
void *main_thread(void *arg) {
  Thread *thread = (Thread *)arg;

  while (1) {
    // fetch a new job.
    int socket;
    log(0) << "FETCH START! THREAD:" << thread->tid << endl;
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
    log(0) << "FETCH END! THREAD:" << thread->tid << endl;

    // job
    EhttpPtr http = EhttpPtr(new Ehttp());
    http->init();
    http->add_handler("/polling", polling_handler);
    http->add_handler("/upload", upload_handler);
    http->add_handler("/request", request_handler);
    http->add_handler("/login", login_handler);
    http->add_handler("/download", download_handler);
    http->add_handler(NULL, handleDefault);

    http->parse_request(socket);

    log(0) << "FETCH WORK END! THREAD:" << thread->tid << endl;


    // if (http->isClose()) {
    //   delete http;
    // }

    // pthread_mutex_lock(&new_connection_mutex);
    // pthread_mutex_unlock(&new_connection_mutex);
  }
}

void handle_sigs(int signo) {
  log(0) << "Signal(" << signo << ") is ignored" << endl;
}

int main(int argc, char** args) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("run", "run in foreground")
    ("h", po::value<string>(), "hostname")
    ("template_path", po::value<string>(), "template path")
    ("p", po::value<int>(), "port")
    ;

  po::variables_map vm;
  store(po::parse_command_line(argc, args, desc), vm);
  po::notify(vm);

  if (vm.count("h")) {
    hostname = vm["h"].as<string>(); 
  }
  
  if (vm.count("p")) {
    PORT = vm["p"].as<int>(); 
  }

  if (vm.count("template_path")) {
    Ehttp::set_template_path(vm["template_path"].as<string>()); 
  }
    
  

  if (vm.count("run") == 0) {
    if (fork() != 0) {
      exit(0);
    }
    signal(SIGHUP, handle_sigs);
    signal(SIGINT, handle_sigs);
    signal(SIGTERM, handle_sigs);
    signal(SIGQUIT, handle_sigs);
  }

  fstream pidfile;
  //TODO(donghyun): option
  pidfile.open("/var/drserver/drserver.pid", fstream::out);
  pidfile << getpid() << endl;
  pidfile.close();

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

  pthread_t tid;
  pthread_create(&tid, NULL, &timeout_killer, NULL);

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

  log(1) << "DR Server Start! Listen..." << endl;
  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      exit(1);
    }
    //    nonblock(clifd);
    // nonblock(clifd);
    pthread_mutex_lock(&new_connection_mutex);
    log(1) << "Accepted... " << clifd << "  / Queue size : " << conn_pool.size() << " / Thread: " << MAX_THREAD << endl;
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}
