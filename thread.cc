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
#include "dr_mysql.h"
#include "session.h"
#include "log.h"
#include "ssl.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>

#define TLOCK(name) pthread_mutex_lock(&(name))
#define TUNLOCK(name) pthread_mutex_unlock(&(name))

using namespace std;
namespace po = boost::program_options;

#define MAX_THREAD 64
time_t uptime[MAX_THREAD];
time_t uptime_threshold = 60;
int cnt=0;
int cookie_index=1;
int PORT = 8000;
int timeout_sec_default = 10;
int timeout_sec_polling = 60;
int timeout_sec_requests_key = 30;
int session_expired_time = 3600;

long long request_call_count = 0;
long long polling_call_count = 0;
long long upload_call_count = 0;
long long download_call_count = 0;
long long upload_file_size = 0;

string key_path = "./";


typedef struct {
  pthread_t tid;
  int order;
  deque<EhttpPtr> *conn_pool;
} Thread;

map <string, RequestPtr > requests;  // userid as key
map <string, PollingPtr > pollings;  // userid as key

map <string, RequestPtr > requests_key;  // inckey as key

queue <RequestPtr> queue_requests;
queue <PollingPtr> queue_pollings;
queue <RequestPtr> queue_requests_key;

pthread_mutex_t mutex_requests = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_pollings = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutex_requests_key = PTHREAD_MUTEX_INITIALIZER;

map <string, Session> session;

pthread_mutex_t mutex_session = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutex_queue_requests = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue_pollings = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue_requests_key = PTHREAD_MUTEX_INITIALIZER;

DrMysql db;

string hostname = "";

SSL_CTX *ctx;

int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata) {
  strcpy(buf,"dlp1004");
  return 7;
}

int nonblock(int fd, int nblockFlag = 1) {
   int flags;

   flags = fcntl( fd, F_GETFL, 0);
   if ( nblockFlag == 1 )
      return fcntl( fd, F_SETFL, flags | O_NONBLOCK);
   else
      return fcntl( fd, F_SETFL, flags & (~O_NONBLOCK));
}

void loginFail (EhttpPtr obj) {
  obj->log(1) << "Login Fail In" << endl;
  obj->out_set_file("sessionerror.json");
  obj->out_replace_token("fail", "Session is closed. Please sign in.");
  obj->out_replace();
  obj->out_commit();
  obj->close();
}

int handleDefault(EhttpPtr obj) {
  obj->out_set_file("default.html");
  obj->out_replace_token("MESSAGE", "Hello World");
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

string get_userid_from_session(EhttpPtr obj) {
  string session_id = obj->ptheCookie["SESSIONID"];
  TLOCK(mutex_session);
  string userid = session[session_id].user_id;
  session[session_id].timestamp = time(NULL);
  TUNLOCK(mutex_session);
  return userid;
}

string get_username_from_session(EhttpPtr obj) {
  string session_id = obj->ptheCookie["SESSIONID"];
  TLOCK(mutex_session);
  string userid = session[session_id].username;
  obj->username = userid;
  session[session_id].timestamp = time(NULL);
  TUNLOCK(mutex_session);
  return userid;
}

int status_handler (EhttpPtr obj) {
  log(1) << "Status Handler In" << endl;
  obj->out_set_file("stringtemplate.json");
  int alive_count = 0;
  time_t now = time(NULL);
  for (int i = 0; i < MAX_THREAD; ++i) {
    if (now - uptime[i] <= uptime_threshold)
      alive_count ++;
  }

  stringstream ss;
  ss  << "requestCallCount:" << request_call_count << " ";
  ss << "pollingCallCount:" << polling_call_count << " ";
  ss << "downloadCallCount:" << download_call_count << " ";
  ss << "uploadCallCount:" << upload_call_count << " ";
  ss << "aliveThreadCount:" << alive_count << " ";
  ss << "uploadFileSize:" << upload_file_size << endl;
  obj->getResponseHeader()["Content-Type"] = "text/plain";
  obj->out_replace_token("string",ss.str());
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

int login_handler(EhttpPtr obj) {
  log(1) << "Login Handler In" << endl;
  obj->log(1) << "Login Handler called" << endl;
  if ((obj->getPostParams()).count("email") > 0) {
    obj->log(0) << "POST" << endl;
    string email = obj->getPostParams()["email"];
	size_t atpos = email.find("@");
	string username = email;
	if (atpos != string::npos) {
	  username = email.substr(0, atpos);
	}
    string password = obj->getPostParams()["password"];
    string installkey = obj->getPostParams()["installkey"];
    string user_id, macaddress;

    if (password.empty()) {
      password = obj->getPostParams()["padpasskey"];
    }

    if (password.length() > 0 && db.login(email, password, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid + "; path=/";
      TLOCK(mutex_session);
      session[sessionid].user_id = user_id;
      session[sessionid].email = email;
      session[sessionid].username = username;
      obj->username = username;
      session[sessionid].macaddress = macaddress;
      session[sessionid].timestamp = time(NULL);
      TUNLOCK(mutex_session);

      obj->out_set_file("login.json");

      obj->out_replace_token("macaddress", macaddress);
      obj->out_replace_token("hostname", hostname);
      obj->log(1) << email << " login success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid + "; path=/";
      TLOCK(mutex_session);
      session[sessionid].user_id = user_id;
      session[sessionid].email = email;
      session[sessionid].username = username;
      obj->username = username;
      session[sessionid].macaddress = macaddress;
      session[sessionid].timestamp = time(NULL);
      TUNLOCK(mutex_session);
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->out_replace_token("hostname", hostname);
      obj->log(1) << email << " login success" << endl;
    } else {
      string msg = user_id + " login fail";
      return obj->error(msg);
    }
    //    obj->log(0) << "mac : " << macaddress << endl;
  } else {
    obj->log(0) << "GET" << endl;
    obj->out_set_file("login_page.html");
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  //  obj->log(1) << "Login Handler finished" << endl;

  return ret;
}

int mac_handler(EhttpPtr obj) {
  log(1) << "Mac Handler called" << endl;
  if ((obj->getPostParams()).count("email") > 0) {
    obj->log(0) << "POST" << endl;
    string email = obj->getPostParams()["email"];
    string password = obj->getPostParams()["password"];

    if (password.empty()) {
      password = obj->getPostParams()["padpasskey"];
    }

    string installkey = obj->getPostParams()["installkey"];
    string user_id, macaddress;
    if (password.length() > 0 && db.login(email, password, &user_id, &macaddress)) {
      obj->out_set_file("mac.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->log(1) << email << " mac success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      obj->out_set_file("mac.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->log(1) << email << " mac success" << endl;

    } else {
      string msg = user_id + " mac fail";
      return obj->error(msg);
    }
  
    obj->out_replace();
    int ret = obj->out_commit();
    obj->close();
    return ret;
  } else {
    return obj->error("No email");
  }
}

int execute_downloading(UploadPtr up, DownloadPtr dn) {
  //TODO: execute downloading using epoll
  //TODO(bigeye): move to ...
  up->ehttp->log(1) << "Execute Downloading In" << endl;
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
  mimetype["txt"] = "text/plain; charset=euc_kr";
  mimetype["key"] = "application/octet-stream";
  mimetype["dwg"] = "application/dwg";

  string filename = dn->requestpath;
  size_t found = filename.rfind("\\");
  if (found != string::npos) {
    filename = filename.substr(found+1);
  }

  stringstream strContentLength;
  strContentLength << up->ehttp->getContentLength();
  dn->ehttp->getResponseHeader()["Cache-Control"] = "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  dn->ehttp->getResponseHeader()["Pragma"] = "no-cache";
  dn->ehttp->getResponseHeader()["Expires"] = "0";
  dn->ehttp->getResponseHeader()["Content-Disposition"] = "attachment; filename=" + filename;
  dn->ehttp->getResponseHeader()["Content-Length"] = strContentLength.str();
  dn->ehttp->getResponseHeader()["Content-Transfer-Encoding"]  = "binary";
  dn->ehttp->getResponseHeader()["Connection"] = "close";
  upload_file_size += up->ehttp->getContentLength();

  found = filename.rfind(".");
  string extension = filename.substr(found+1);
  boost::to_lower(extension);
  if (found == string::npos || mimetype.count(extension) == 0) {
    dn->ehttp->getResponseHeader()["Content-Type"] = "application/octet-stream";
  } else {
    dn->ehttp->getResponseHeader()["Content-Type"] = mimetype[extension];
  }
  dn->ehttp->out_commit();

  up->ehttp->log(1) << up->ehttp->message.length() << endl;

  int res;
  if (!up->ehttp->message.empty())  {
    res  = dn->ehttp->send(
              up->ehttp->message.c_str(),
              up->ehttp->message.length());
  } else {
    res = 0;
  }

  up->ehttp->log(1) << "Download start:" << filename << " " << strContentLength.str() << " " << res << endl;

  up->ehttp->log(1) << "RES : " << res << endl;
  if (res < 0 || res != (int)up->ehttp->message.length()) {
    up->ehttp->log(1) << "Error first sending" << endl;
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
  polling->ehttp->log(1) << "Execute Polling In" << endl;
  TLOCK(mutex_requests_key);
  requests_key[request->key] = request;
  TUNLOCK(mutex_requests_key);

  TLOCK(mutex_queue_requests_key);
  queue_requests_key.push(request);
  TUNLOCK(mutex_queue_requests_key);

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
  polling->ehttp->log(1) << "execute_polling: End and assign request:" << endl;
  return ret;
}

///////////////////////////////////////////////////

string getUUID() {
  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();
  stringstream ss;
  ss << time(NULL) / 24 / 60 / 60;
  ss << "-";
  ss << to_string(u).substr(0, 18);
  return ss.str();
}

int request_handler(EhttpPtr obj) {
  ++request_call_count;
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }

  string session_id = obj->ptheCookie["SESSIONID"];
  string userid = get_userid_from_session(obj);
  string username = get_username_from_session(obj);
  obj->username = username;
  obj->log(1) << "Request handler In" << endl;

  //TODO(bigeye): logout is deprecated.
  if (obj->getUrlParams()["command"] == "logout") {
    TLOCK(mutex_session);
    session.erase(session_id);
    TUNLOCK(mutex_session);
    obj->out_set_file("request.json");
    obj->out_replace_token("jsondata", "");
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  } else if (obj->getUrlParams()["command"] == "sendemail") {
    obj->out_set_file("default.html");
    obj->out_replace();
    int ret = obj->out_commit();
    obj->close();
    return ret;
  }

  if (obj->getUrlParams().count("device") == 0 || obj->getUrlParams()["device"] == "") {
    obj->out_set_file("device.json");
    obj->out_replace_token("username", username);
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  } else if (obj->getUrlParams().count("device") > 0 && obj->getUrlParams()["device"] != "com") {
    obj->out_set_file("errormessage.json");
    obj->out_replace_token("fail", "Device doesn't exist");
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  }

  RequestPtr request(new Request);
  request->ehttp = obj;
  request->userid = userid;
  request->command = obj->getUrlParams()["command"];
  request->requestpath = obj->getUrlParams()["requestpath"];
  request->key = getUUID();

  // fetch polling.
  PollingPtr polling;
  TLOCK(mutex_pollings);
  TLOCK(mutex_queue_requests);
  TLOCK(mutex_requests);
  if (pollings.count(userid)) {
    polling = pollings[userid];
    pollings.erase(userid);
  } else {
    requests[userid] = request;
    queue_requests.push(request);
  }
  TUNLOCK(mutex_requests);
  TUNLOCK(mutex_queue_requests);
  TUNLOCK(mutex_pollings);

  if (polling.get() != NULL) {
    obj->log(1) << "Request: Execute polling ("
           << request->ehttp->getFD() << ","
           << polling->ehttp->getFD() << ")"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;

    execute_polling(request, polling);
  } else {
    obj->log(1) << "Request: Waiting"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;
  }
  return 0;
}


int polling_handler(EhttpPtr obj) {
  ++polling_call_count;
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string userid = get_userid_from_session(obj);
  string username = get_username_from_session(obj);
  obj->username = username;
  obj->log(1) << "Polling handler In" << endl;

  PollingPtr polling(new Polling);
  polling->ehttp = obj;
  polling->userid = userid;

  // fetch request.
  RequestPtr request;
  TLOCK(mutex_pollings);
  TLOCK(mutex_requests);
  if (requests.count(userid)) {
    request = requests[userid];
    requests.erase(userid);
  } else {
    if (pollings.count(userid) > 0) {
      pollings[userid]->ehttp->timeout();
      pollings.erase(userid);
    }
    pollings[userid] = polling;
  }
  TUNLOCK(mutex_requests);
  TUNLOCK(mutex_pollings);

  if (request.get() != NULL) {
    obj->log(1) << "Request: Execute polling ("
           << request->ehttp->getFD() << ","
           << polling->ehttp->getFD() << ")"
           << " with command:" << request->command
           << ", requestpath:" << request->requestpath
           << ", key:" << request->key << endl;

    execute_polling(request, polling);
  } else {
    obj->log(1) << "Request: Waiting" << endl;
    TLOCK(mutex_queue_pollings);
    queue_pollings.push(polling);
    TUNLOCK(mutex_queue_pollings);
  }
  return 0;
}

int upload_handler(EhttpPtr obj) {
  ++upload_call_count;
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }

  string userid = get_userid_from_session(obj);
  string username = get_username_from_session(obj);
  obj->username = username;
  obj->log(1) << "Upload handler In" << endl;

  string jsondata = obj->getPostParams()["jsondata"];
  string key = obj->getPostParams()["incrkey"];
  string command = obj->getPostParams()["command"];

  obj->log(2) << "UPLOAD HANDLER: " << key << endl;

  // fetch request.
  RequestPtr request;
  TLOCK(mutex_requests_key);
  if (requests_key.count(key)) {
    obj->log(1) << "incrkey " << key << " is erased!" << endl;
    request = requests_key[key];
    requests_key.erase(key);
    obj->log(1) << "request count " << request.use_count() << endl;
  }
  TUNLOCK(mutex_requests_key);

  if (request.get() == NULL) {
    return obj->error("Wrong uploading Key doesn't exist");
  }

  if (command == "getfile") {
    UploadPtr upload(new Upload);
    upload->ehttp = obj;
    upload->userid = request->userid;
    upload->key = request->key;
    upload->command = request->command;
    upload->requestpath = request->requestpath;

    DownloadPtr download(new Download);
    download->ehttp = request->ehttp;
    download->userid = request->userid;
    download->key = request->key;
    download->command = request->command;
    download->requestpath = request->requestpath;

    execute_downloading(upload, download);
    obj->log(1) << "execute_downloading end!! request count " << request.use_count() << endl;
  } else {
    obj->log(0) << "jsondata : " << jsondata << endl;
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
  }

  return 0;
}

// for temp
#include <sys/stat.h>
size_t filesize(const char *filename){
  struct stat st;
  size_t retval=0;
  if(stat(filename,&st) ){
    printf("cannot stat %s\n",filename);
  }else{
    retval=st.st_size;
  }
  return retval;
}

void *session_killer(void *arg) {
  log(1) << "Session Killer In" << endl;
  while(1) {
    sleep(600);
    time_t now = time(NULL);
    TLOCK(mutex_session);
    for (map<string, Session>::iterator it = session.begin(); it != session.end(); ) {
      if ((*it).second.timestamp + session_expired_time <= now) {
        cout << "Session Expired : " << (*it).first << endl;
	map<string, Session>::iterator temp_iterator = it;
	++it;
        session.erase((*temp_iterator).first);
      } else {
	++it;
      }
    }
    TUNLOCK(mutex_session);
  }
}

void *mysql_updator(void *arg) {
  log(1) << "Mysql Updater In" << endl;
  while(1) {
    DrMysql::connection_init();
    sleep(600);
  }
}

void *timeout_killer(void *arg) {
  log(1) << "Timeout Killer In" << endl;
  while(1) {
    sleep(1);
    time_t now = time(NULL);
    TLOCK(mutex_queue_requests);
    while (!queue_requests.empty()) {
      RequestPtr ptr = queue_requests.front();
      if (ptr->ehttp->timestamp + timeout_sec_default <= now) {
        ptr->ehttp->log(1) << "request queue timeout" << endl;
        queue_requests.pop();
        TLOCK(mutex_requests);
        if (requests.count(ptr->userid) > 0 && requests[ptr->userid].get() == ptr.get()) {
          ptr->ehttp->timeout();
          requests.erase(ptr->userid);
        }
        TUNLOCK(mutex_requests);
      } else {
        break;
      }
    }
    TUNLOCK(mutex_queue_requests);


    TLOCK(mutex_queue_pollings);
    while(!queue_pollings.empty()) {
      PollingPtr ptr = queue_pollings.front();
      if (ptr->ehttp->timestamp + timeout_sec_polling <= now) {
        ptr->ehttp->log(1) << "polling queue timeout" << endl;
        ptr->ehttp->log(1) << "polling queue: " << ptr->userid << endl;
        queue_pollings.pop();
        TLOCK(mutex_pollings);
        if (pollings.count(ptr->userid) > 0 && pollings[ptr->userid].get() == ptr.get()) {
          ptr->ehttp->timeout();
          pollings.erase(ptr->userid);
        }
        TUNLOCK(mutex_pollings);
      } else {
        break;
      }
    }
    TUNLOCK(mutex_queue_pollings);

    TLOCK(mutex_queue_requests_key);
    while(!queue_requests_key.empty()) {
      log(0) << "requests key queue timeout"  << queue_requests_key.front().use_count() << " " << queue_requests_key.front()->key  << endl;
      RequestPtr ptr = queue_requests_key.front();
      if (ptr->ehttp->timestamp + timeout_sec_requests_key <= now) {
        queue_requests_key.pop();

        /*
        if (!ptr.unique()) {
          TLOCK(mutex_requests_key);
          ptr->ehttp->log(1) << "erase req key " << ptr->key << endl;
          requests_key.erase(ptr->key);
          TUNLOCK(mutex_requests_key);
        } else {
          ptr->ehttp->log(1) << "call ehttp timeout " << endl;
          ptr->ehttp->timeout();
          }*/

        TLOCK(mutex_requests_key);
        ptr->ehttp->log(0) << "erase req key " << ptr->key << endl;
        ptr->ehttp->timeout();
        requests_key.erase(ptr->key);
        TUNLOCK(mutex_requests_key);

      } else {
        break;
      }
    }
    TUNLOCK(mutex_queue_requests_key);
  }
}
void *main_thread(void *arg) {
  Thread *thread = (Thread *)arg;

  while (1) {
    // fetch a new job.
    EhttpPtr http;

    for(;;) {
      sleep(1);
      TLOCK(new_connection_mutex);
      if (!thread->conn_pool->empty()) {
        http = thread->conn_pool->front();
        thread->conn_pool->pop_front();
        TUNLOCK(new_connection_mutex);
        break;
      }
      TUNLOCK(new_connection_mutex);
    }
    log(0) << "FETCH END! THREAD:" << thread->tid << endl;
	uptime[thread->order] = time(NULL);
	log(0) << "uptime updated" << endl;

    if (http->isSsl) {
	  log(0) << "isSSL Check" << endl;
      if (http->initSSL(ctx) < 0) {
        log(2) << "INIT SSL ERROR" << endl;
        http->close();
        continue;
      }
    }

    // job
	log(0) << "init start" << endl;
    http->init();
	log(0) << "add_handler" << endl;
    http->add_handler("/polling", polling_handler);
    http->add_handler("/upload", upload_handler);
    http->add_handler("/request", request_handler);
    http->add_handler("/login", login_handler);
    http->add_handler("/agent/login", login_handler);
    http->add_handler("/mobile/login", login_handler);

    http->add_handler("/mac", mac_handler);
    http->add_handler("/status", status_handler);
	log(0) << "add_NULL_handler" << endl;
    http->add_handler(NULL, handleDefault);

    // nonblock(http->getFD());

    log(0) << "FETCH WORK END! THREAD:" << thread->tid << endl;

    if (http->parse_request() < 0) {
      http->close();
    }

    // if (http->isClose()) {
    //   delete http;
    // }

    // TLOCK(new_connection_mutex);
    // TUNLOCK(new_connection_mutex);
  }
}

void handle_sigs(int signo) {
  log(0) << "Signal(" << signo << ") is ignored" << endl;
}


void *http_listen (void *arg) {
  deque<EhttpPtr> *conn_pool = (deque<EhttpPtr> *)arg;
  int listenfd;

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("sockfd\n");
    exit(1);
  }

  int yes = 1;
  setsockopt(listenfd, SOL_SOCKET,SO_REUSEADDR, (void *)&yes, sizeof(int));

  struct sockaddr_in srv;
  bzero(&srv, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_addr.s_addr = htonl(INADDR_ANY);
  srv.sin_port = htons(PORT);

  if( bind(listenfd, (struct sockaddr *) &srv, sizeof(srv)) < 0) {
    cout << "bind http port:" <<  PORT << endl;
    exit(1);
  }

  listen(listenfd, 4096);

  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(struct sockaddr);

  log(1) << "DR Server Start! Listen...(HTTP)" << endl;
  for( ; ; ) {
    int clifd;
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      exit(1);
    }
    static int linger[2] = {0,0};
    setsockopt(clifd, SOL_SOCKET,SO_LINGER,&linger, sizeof(linger));
    TLOCK(new_connection_mutex);
    log(0) << "Http Accepted... " << clifd << "  / Queue size : " << conn_pool->size() << " / Thread: " << MAX_THREAD << endl;

    EhttpPtr http = EhttpPtr(new Ehttp());
    http->isSsl = false;
    http->socket = clifd;

    conn_pool->push_back(http);
    TUNLOCK(new_connection_mutex);
  }

}

void *https_listen (void *arg) {
  deque<EhttpPtr> *conn_pool = (deque<EhttpPtr> *)arg;
  int listenfd;

  /*SSL_load_error_strings();
  SSLeay_add_ssl_algorithms();
  ctx=SSL_CTX_new(SSLv3_method());
  SSL_library_init();*/

  SSL_library_init();;
  SSL_load_error_strings();
  ctx = SSL_CTX_new(SSLv23_method());

  SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE  | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  SSL_CTX_set_default_passwd_cb(ctx, &pem_passwd_cb);

  string str = key_path + "server1024.crt";
  if ( SSL_CTX_use_certificate_file(ctx, str.c_str(), SSL_FILETYPE_PEM)<0 ) {
    log(2) << "Can't read cert file" << endl;
  }

  str = key_path + "server1024.key";
  if(!(SSL_CTX_use_PrivateKey_file(ctx, str.c_str(), SSL_FILETYPE_PEM))) {
    log(2) << "Can't read key file" << endl;
  }

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("sockfd\n");
    exit(1);
  }

  int yes = 1;
  setsockopt(listenfd, SOL_SOCKET,SO_REUSEADDR, (void *)&yes, sizeof(int));

  struct sockaddr_in srv;
  bzero(&srv, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_addr.s_addr = htonl(INADDR_ANY);
  srv.sin_port = htons(443);
  if( bind(listenfd, (struct sockaddr *) &srv, sizeof(srv)) < 0) {
    perror("bind 222\n");
    exit(1);
  }

  listen(listenfd, 8196);

  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(struct sockaddr);

  log(1) << "DR Server Start! Listen...(HTTPS)" << endl;
  for( ; ; ) {
    int clifd;
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      exit(1);
    }

    static int linger[2] = {0,0};
    setsockopt(clifd, SOL_SOCKET,SO_LINGER,&linger, sizeof(linger));
    TLOCK(new_connection_mutex);
    log(0) << "Https Accepted... " << clifd << "  / Queue size : " << conn_pool->size() << " / Thread: " << MAX_THREAD << endl;

    EhttpPtr http = EhttpPtr(new Ehttp());
    http->isSsl = true;
    http->socket = clifd;

    conn_pool->push_back(http);
    TUNLOCK(new_connection_mutex);
  }
}


int main(int argc, char** args) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("run", "run in foreground")
    ("h", po::value<string>(), "hostname")
    ("template_path", po::value<string>(), "template path")
    ("p", po::value<int>(), "port")
    ("save_path", po::value<string>(), "save path")
    ("key_path", po::value<string>(), "save path")
    ;

  po::variables_map vm;
  store(po::parse_command_line(argc, args, desc), vm);
  po::notify(vm);

  if (vm.count("h")) {
    hostname = vm["h"].as<string>();
    log(0) << hostname << endl;
  } else {
    log(2) << "NO host!" << endl;
  }

  if (vm.count("p")) {
    PORT = vm["p"].as<int>();
  }

  if (vm.count("template_path")) {
    Ehttp::set_template_path(vm["template_path"].as<string>());
  }

  if (vm.count("save_path")) {
    Ehttp::set_save_path(vm["save_path"].as<string>());
  }

  if (vm.count("key_path")) {
    key_path = vm["key_path"].as<string>();
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
  pidfile.open("/var/drserver/drserver.pid", fstream::out);
  pidfile << getpid() << endl;
  pidfile.close();

  signal(SIGPIPE, SIG_IGN);

  pthread_t tid_mysql;
  pthread_create(&tid_mysql, NULL, &mysql_updator, NULL);

  deque<EhttpPtr> conn_pool;
  Thread threads[MAX_THREAD];

  /* create threads */
  for(int i = 0; i < MAX_THREAD; i++) {
    threads[i].conn_pool = &conn_pool;
	threads[i].order = i;
    pthread_create(&threads[i].tid, NULL, &main_thread, (void *)&threads[i]);
  }

  // pthread_t tid;
  // pthread_create(&tid, NULL, &timeout_killer, NULL);

  pthread_t tid_session;
  pthread_create(&tid_session, NULL, &session_killer, NULL);

  DrEpoll::get_mutable_instance().init();

  pthread_t tid_http_listen;
  pthread_create(&tid_http_listen, NULL, &http_listen, &conn_pool);

  pthread_t tid_https_listen;
  pthread_create(&tid_https_listen, NULL, &https_listen, &conn_pool);

  while(1) {
    sleep(100);
  }

  return 0;
}
