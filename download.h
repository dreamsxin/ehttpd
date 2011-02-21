#include "embedhttp.h"
#include <string>
using namespace std;

class download {
public:
  ehttp_ptr fd_upload;
  ehttp_ptr fd_download;

  string filename;

  download() {};

  download(ehttp *fd_upload,
          ehttp *fd_download) : fd_upload(fd_upload),
                                fd_download(fd_download) {};
};
