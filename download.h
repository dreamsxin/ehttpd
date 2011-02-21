#pragma once
#include "embedhttp.h"
#include <string>
#include <boost/shared_ptr.hpp>
using namespace std;

class download {
public:
  ehttp_ptr fd_upload;
  ehttp_ptr fd_download;

  string key;
  string filename;
  int active;
  int remaining;

  download() {
    active = 1;
  };

  download(ehttp_ptr fd_upload,
           ehttp_ptr fd_download,
           string key,
           string filename) : fd_upload(fd_upload),
                              fd_download(fd_download),
                              key(key),
                              filename(filename),
                              active(1) {};

  ~download() {
    close();
  };

  void close() {
    active = 0;
    fd_upload->close();
    fd_download->close();
  };

};

typedef shared_ptr<download> download_ptr;
