#include <string>
#include "embedhttp.h"

using namespace std;

class connection {
public:
  ehttp_ptr fd_polling;
  ehttp_ptr fd_request;

  string command;
  string requestpath;
  string key;
  int status;

  connection() {};

  connection(ehttp *fd_request,
    string command,
		string requestpath,
		string key,
		ehttp *fd_polling,
		int Status) : fd_request(fd_request),
									command(command),
									requestpath(requestpath),
									key(key),
									fd_polling(fd_polling),
									status(status) {};
};
