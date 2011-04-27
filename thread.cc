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
int cnt=0;
int listenfd;
int cookie_index=1;
int PORT = 8000;
int timeout_sec_default = 30;
int timeout_sec_polling = 60;
int session_expired_time = 3600;

long long request_call_count = 0;
long long polling_call_count = 0;
long long upload_call_count = 0;
long long download_call_count = 0;
long long upload_file_size = 0;

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

map <string, Session> session;

pthread_mutex_t mutex_session = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutex_queue_requests = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue_pollings = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue_requests_key = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue_uploads = PTHREAD_MUTEX_INITIALIZER;

DrMysql db;

string hostname = "";

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

string get_userid_from_session(EhttpPtr obj) {
  string session_id = obj->ptheCookie["SESSIONID"];
  TLOCK(mutex_session);
  string userid = session[session_id].user_id;
  session[session_id].timestamp = time(NULL);
  TUNLOCK(mutex_session);
  return userid;
}

int status_handler (EhttpPtr obj) {
  obj->out_set_file("stringtemplate.json");
  stringstream ss;
  ss  << "requestCallCount:" << request_call_count << " ";
  ss << "pollingCallCount:" << polling_call_count << " ";
  ss << "downloadCallCount:" << download_call_count << " ";
  ss << "uploadCallCount:" << upload_call_count << " ";
  ss << "uploadFileSize:" << upload_file_size << endl;
  obj->getResponseHeader()["Content-Type"] = "text/plain";
  obj->out_replace_token("string",ss.str());
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
      TLOCK(mutex_session);
      session[sessionid].user_id = user_id;
      session[sessionid].macaddress = macaddress;
      session[sessionid].timestamp = time(NULL);
      TUNLOCK(mutex_session);
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->out_replace_token("hostname", hostname);
      log(1) << email << " login success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      boost::uuids::basic_random_generator<boost::mt19937> gen;
      boost::uuids::uuid u = gen();
      string sessionid = to_string(u);
      obj->getResponseHeader()["Set-Cookie"] = "SESSIONID=" + sessionid;
      TLOCK(mutex_session);
      session[sessionid].user_id = user_id;
      session[sessionid].macaddress = macaddress;
      session[sessionid].timestamp = time(NULL);
      TUNLOCK(mutex_session);
      obj->out_set_file("login.json");
      obj->out_replace_token("macaddress", macaddress);
      obj->out_replace_token("hostname", hostname);
      log(1) << email << " login success" << endl;
    } else {
      string msg = user_id + " login fail";
      return obj->error(msg);
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

int mac_handler(EhttpPtr obj) {
  log(1) << "Mac Handler called" << endl;
  if ((obj->getPostParams()).count("email") > 0) {
    log(0) << "POST" << endl;
    string email = obj->getPostParams()["email"];
    string padpasskey = obj->getPostParams()["padpasskey"];
    string installkey = obj->getPostParams()["installkey"];
    string user_id, macaddress;
    if (padpasskey.length() > 0 && db.login(email, padpasskey, &user_id, &macaddress)) {
      obj->out_set_file("mac.json");
      obj->out_replace_token("macaddress", macaddress);
      log(1) << email << " mac success" << endl;

    } else if (installkey.length() > 0 && db.login(email, installkey, &user_id, &macaddress)) {
      obj->out_set_file("mac.json");
      obj->out_replace_token("macaddress", macaddress);
      log(1) << email << " mac success" << endl;

    } else {
      string msg = user_id + " mac fail";
      return obj->error(msg);
    }
  }
  obj->out_replace();
  int ret = obj->out_commit();
  obj->close();
  return ret;
}

void saveToFile(UploadPtr up, const char *buffer, int r) {
  string &key = up->key;
  size_t found = key.find("-");
  if (found == string::npos) {
    return;
  }
  string date = key.substr(0, found);
  string path = Ehttp::get_save_path() + "/" + date + "/" + key;
  FILE *fp = fopen(path.c_str(), "a");
  if (fp == NULL) {
    return;
  }
  fwrite(buffer, 1, r, fp);
  fclose(fp);
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
  if (found == string::npos || mimetype.count(filename.substr(found+1)) == 0) {
    dn->ehttp->getResponseHeader()["Content-Type"] = "application/octet-stream";
  } else {
    dn->ehttp->getResponseHeader()["Content-Type"] = mimetype[filename.substr(found+1)];
  }
  dn->ehttp->out_commit();

  int res = dn->ehttp->pSend(dn->ehttp->getFD(),
              up->ehttp->message.c_str(),
              up->ehttp->message.length());

  log(1) << "Download start:" << filename << " " << strContentLength.str() << " " << res << endl;

  if (res < 0 || res != (int)up->ehttp->message.length()) {
    log(1) << "Error first sending" << endl;
    dn->ehttp->close();
    up->ehttp->close();
    return EHTTP_ERR_GENERIC;
  }

  saveToFile(up, up->ehttp->message.c_str(), up->ehttp->message.length());

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

  TLOCK(mutex_requests_key);
  requests_key[request->key] = request;
  TUNLOCK(mutex_requests_key);
  TLOCK(mutex_queue_requests_key);
  queue_requests_key.push(request);
  TUNLOCK(mutex_queue_requests_key);

  log(1) << "execute_polling: End and assign request:" << request->ehttp->getFD() << endl;
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

  //TODO(bigeye): logout is deprecated.
  if (obj->getUrlParams()["command"] == "logout") {
    session.erase(session_id);
    obj->out_set_file("request.json");
    obj->out_replace_token("jsondata", "");
    obj->out_replace();
    obj->out_commit();
    obj->close();
    return EHTTP_ERR_OK;
  } else if (obj->getUrlParams()["command"] == "sendemail") {
    obj->out_set_file("helloworld_template.html");
    obj->out_replace();
    int ret = obj->out_commit();
    obj->close();
    return ret;
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
  TLOCK(mutex_requests);
  if (pollings.count(userid)) {
    polling = pollings[userid];
    pollings.erase(userid);
  } else {
    requests[userid] = request;
    TLOCK(mutex_queue_requests);
    queue_requests.push(request);
    TUNLOCK(mutex_queue_requests);
  }
  TUNLOCK(mutex_requests);
  TUNLOCK(mutex_pollings);

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
  ++polling_call_count;
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string userid = get_userid_from_session(obj);

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
    pollings[userid] = polling;
  }
  TUNLOCK(mutex_requests);
  TUNLOCK(mutex_pollings);

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

  string jsondata = obj->getPostParams()["jsondata"];
  string key = obj->getPostParams()["incrkey"];
  string command = obj->getPostParams()["command"];

  // fetch request.
  RequestPtr request;
  TLOCK(mutex_requests_key);
  if (requests_key.count(key)) {
    request = requests_key[key];
    requests_key.erase(key);
  }
  TUNLOCK(mutex_requests_key);

  if (request.get() == NULL) {
    return obj->error("Wrong uploading Key doesn't exist");
  }

  if (command == "getfile") {
    request->ehttp->out_set_file("request.json");
    request->ehttp->out_replace_token("jsondata","{\"filedownurl\":\"http://" + hostname + "/download?incrkey=" + key + "&code=0\"}");
    request->ehttp->out_replace();
    request->ehttp->out_commit();
    request->ehttp->close();

    UploadPtr upload(new Upload);
    upload->ehttp = obj;
    upload->userid = request->userid;
    upload->key = request->key;
    upload->command = request->command;
    upload->requestpath = request->requestpath;

    TLOCK(mutex_uploads);
    uploads[upload->key] = upload;
    TUNLOCK(mutex_uploads);
    TLOCK(mutex_queue_uploads);
    queue_uploads.push(upload);
    TUNLOCK(mutex_queue_uploads);

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

int download_handler(EhttpPtr obj) {
  ++download_call_count;
  string key = obj->getPostParams()["incrkey"];

  // fetch request.
  UploadPtr upload;
  TLOCK(mutex_uploads);
  if (uploads.count(key)) {
    upload = uploads[key];
    uploads.erase(key);
  }
  TUNLOCK(mutex_uploads);

  if (upload.get() == NULL) {
    log(1) << "start saved file: " << key << endl;

    // return obj->error("Wrong uploading Key doesn't exist");
    size_t found = key.find("-");
    if (found == string::npos) {
      log(1) << "close saved file 1: " << key << endl;
      return 1;
    }
    string date = key.substr(0, found);
    string path = Ehttp::get_save_path() + "/" + date + "/" + key;

    size_t size = filesize(path.c_str());

    stringstream strContentLength;
    strContentLength << size;
    obj->getResponseHeader()["Cache-Control"] = "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
    obj->getResponseHeader()["Pragma"] = "no-cache";
    obj->getResponseHeader()["Expires"] = "0";
    obj->getResponseHeader()["Content-Length"] = strContentLength.str();
    obj->getResponseHeader()["Content-Transfer-Encoding"] = "binary";
    obj->getResponseHeader()["Connection"] = "close";
//    obj->getResponseHeader()["Content-Disposition"] = "attachment; filename=download.mp3";
    obj->getResponseHeader()["Content-Type"] = "application/octet-stream";
    log(1) << "DN size: " << strContentLength.str() << endl;
    obj->out_commit();

    log(1) << "send saved file: " << key << endl;

    FILE *fp = fopen(path.c_str(), "r");
    if (fp == NULL) {
      log(1) << "close saved file 2: " << key << " " << path.c_str() << endl;
      return 1;
    }

    char buffer[10000];
    int r;
    while(1){
      r = fread(&buffer, 1, sizeof(buffer), fp);
      if (r <= 0) break;
      obj->pSend(obj->getFD(), buffer, r);
    }
    fclose(fp);
    obj->close();
    return 1;
  }
  if (!(obj->ptheCookie.count("SESSIONID") > 0 && session.count(obj->ptheCookie["SESSIONID"]) > 0)) {
    loginFail(obj);
    return EHTTP_ERR_GENERIC;
  }
  string userid = get_userid_from_session(obj);

  DownloadPtr download(new Download);
  download->ehttp = obj;
  download->userid = upload->userid;
  download->key = upload->key;
  download->command = upload->command;
  download->requestpath = upload->requestpath;

  execute_downloading(upload, download);
  return 1;
}

void *session_killer(void *arg) {
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

void *timeout_killer(void *arg) {
  while(1) {
    sleep(1);
    time_t now = time(NULL);
    TLOCK(mutex_queue_requests);
    while (!queue_requests.empty()) {
      RequestPtr ptr = queue_requests.front();
      if (ptr->ehttp->timestamp + timeout_sec_default <= now) {
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
        log(1) << "polling queue: " << ptr->userid << endl;
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
      RequestPtr ptr = queue_requests_key.front();
      if (ptr->ehttp->timestamp + timeout_sec_default <= now) {
        queue_requests_key.pop();
        if (!ptr.unique()) {
          TLOCK(mutex_requests_key);
          requests_key.erase(ptr->key);
          TUNLOCK(mutex_requests_key);
        } else {
          ptr->ehttp->timeout();
        }
      } else {
        break;
      }
    }
    TUNLOCK(mutex_queue_requests_key);

    TLOCK(mutex_queue_uploads);
    while(!queue_uploads.empty()) {
      UploadPtr ptr = queue_uploads.front();
      if (ptr->ehttp->timestamp + timeout_sec_default <= now) {
        queue_uploads.pop();
        log(1) << "UPLOADQUEUE:" << ptr.use_count() << "(" << ptr->ehttp->timestamp <<" / "<<now<<")"<<endl;
        if (!ptr.unique()) {
          TLOCK(mutex_uploads);
          uploads.erase(ptr->key);
          TUNLOCK(mutex_uploads);
        } else {
          ptr->ehttp->timeout();
        }
      } else {
        break;
      }
    }
    TUNLOCK(mutex_queue_uploads);
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
      TLOCK(new_connection_mutex);
      if (!thread->conn_pool->empty()) {
        socket = thread->conn_pool->front();
        thread->conn_pool->pop_front();
        TUNLOCK(new_connection_mutex);
        break;
      }
      TUNLOCK(new_connection_mutex);
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
    http->add_handler("/mac", mac_handler);
    http->add_handler("/status", status_handler);
    http->add_handler(NULL, handleDefault);

    http->parse_request(socket);

    log(0) << "FETCH WORK END! THREAD:" << thread->tid << endl;


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

int main(int argc, char** args) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("run", "run in foreground")
    ("h", po::value<string>(), "hostname")
    ("template_path", po::value<string>(), "template path")
    ("p", po::value<int>(), "port")
    ("save_path", po::value<string>(), "save path")
    ;

  po::variables_map vm;
  store(po::parse_command_line(argc, args, desc), vm);
  po::notify(vm);

  if (vm.count("h")) {
    hostname = vm["h"].as<string>();
    log(1) << hostname << endl;
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

  struct sockaddr_in srv;
  int clifd;

  signal(SIGPIPE, SIG_IGN);

  DrMysql::connection_init();

  deque<int> conn_pool;
  Thread threads[MAX_THREAD];

  /* create threads */
  for(int i = 0; i < MAX_THREAD; i++) {
    threads[i].conn_pool = &conn_pool;
    pthread_create(&threads[i].tid, NULL, &main_thread, (void *)&threads[i]);
  }

  pthread_t tid;
  pthread_create(&tid, NULL, &timeout_killer, NULL);

  pthread_t tid_session;
  pthread_create(&tid_session, NULL, &session_killer, NULL);

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

  log(2) << "DR Server Start! Listen..." << endl;
  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      exit(1);
    }
    //    nonblock(clifd);
    // nonblock(clifd);
    TLOCK(new_connection_mutex);
    log(1) << "Accepted... " << clifd << "  / Queue size : " << conn_pool.size() << " / Thread: " << MAX_THREAD << endl;
    conn_pool.push_back(clifd);
    TUNLOCK(new_connection_mutex);
  }

  return 0;
}
