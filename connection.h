#pragma once
#include <string>
#include "embedhttp.h"

using namespace std;

class Request {
public:
  EhttpPtr ehttp;
  string userid;
  string key;
  string command;
  string requestpath;
};
typedef shared_ptr<Request> RequestPtr;


class Polling {
public:
  EhttpPtr ehttp;
  string userid;
  string key;
  string command;
  string requestpath;
};
typedef shared_ptr<Polling> PollingPtr;


class Upload {
public:
  EhttpPtr ehttp;
  string userid;
  string key;
  string command;
  string requestpath;
};
typedef shared_ptr<Upload> UploadPtr;


class Download {
public:
  EhttpPtr ehttp;
  string userid;
  string key;
  string command;
  string requestpath;
  int remaining;
};
typedef shared_ptr<Download> DownloadPtr;


class Transfer {
public:
  UploadPtr up;
  DownloadPtr dn;

  void close() {
    up->ehttp->uploadend();
    up->ehttp->close();
    dn->ehttp->close();
  };
  ~Transfer() {
    close();
  };
};
typedef shared_ptr<Transfer> TransferPtr;
