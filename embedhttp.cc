/********************************************************************

 EhttpD - EasyHTTP Server/Parser C++ Class

 http://www.littletux.com

 Copyright (c) 2007, Barry Sprajc

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.

 Neither the name of Littletux.com nor the names of its contributors
 may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL,EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 DAMAGE.

********************************************************************/


#include "./embedhttp.h"
#include "./log.h"
#include <assert.h>
#include <sys/poll.h>
#include <openssl/err.h>
#include <fcntl.h>
#include <pthread.h>

#define TLOCK(name) pthread_mutex_lock(&(name))
#define TUNLOCK(name) pthread_mutex_unlock(&(name))


string Ehttp::template_path = "./";
string Ehttp::save_path = "./";

pthread_mutex_t mutex_ssl = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ehttp = PTHREAD_MUTEX_INITIALIZER;

static int checkSslError() {
  unsigned long code = ERR_get_error();
  if (code != 0) {
    char buffer[120];
    if (ERR_error_string(code, buffer)) {
      log(2) << "SSL-Error " << code << ": \"" << buffer << '"' << endl;
      return -1;
    } else {
      log(2) << "unknown SSL-Error " << code << endl;
      return -1;
    }
  }
  return 0;
}

int Ehttp::initSSL(SSL_CTX* ctx) {
  // TLOCK(mutex_ssl);
  ssl = SSL_new(ctx);

  if( !SSL_set_fd(ssl, getFD()) ) {
    log(2) << "set fd failed" << endl;
    // TUNLOCK(mutex_ssl);
    return -1;
  }

  SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
  SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE |
               SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  int err;
  if( (err=SSL_accept(ssl)) < 0 ) {
    log(2) << "SSL Accept Error " << SSL_get_error(ssl,err) << endl;
    if( (err=SSL_accept(ssl)) < 0 ) {
      log(2) << "SSL Accept Error " << SSL_get_error(ssl,err) << endl;
      // TUNLOCK(mutex_ssl);
      return -1;
    }
  }
  // TUNLOCK(mutex_ssl);
  return 0;
}

ssize_t Ehttp::send(const char *buf, size_t len) {
  TLOCK(mutex_ehttp);
  ssize_t ret = __send(buf, len);
  TUNLOCK(mutex_ehttp);
  return ret;
}

ssize_t Ehttp::__send(const char *buf, size_t len) {
  if (isClose()) {
    return -1;
  }

  if (!isSsl)
    return ::send(getFD(), buf, len, 0);

  if (!ssl) return -1;

  int n = 0;
  int s = len;

  while (true) {
    n = SSL_write(ssl, buf, s);
    if (checkSslError() < 0) {
      return -1;
    }

    unsigned long code = ERR_get_error();
    if (code != 0) {
      char buffer[512];
      if (ERR_error_string(code, buffer)) {
        // log_debug("SSL-Error " << code << ": \"" << buffer << '"');
        return -1;
      } else {
        // log_debug("unknown SSL-Error " << code);
        return -1;
      }
    }

    int err;
    if (n > 0) {
      buf += n;
      s -= n;
    } else if (n < 0
               && (err = SSL_get_error(ssl, n)) != SSL_ERROR_WANT_READ
               && err != SSL_ERROR_WANT_WRITE
               && (err != SSL_ERROR_SYSCALL || errno != EAGAIN)) {
      return -1;
    }

    if (s <= 0)
      break;

    usleep(100);
    // poll(err == SSL_ERROR_WANT_READ ? POLLIN : POLLIN|POLLOUT);
  }
  return len;
}


ssize_t Ehttp::recv(void *buf, size_t len) {
  TLOCK(mutex_ehttp);
  ssize_t ret = __recv(buf, len);
  TUNLOCK(mutex_ehttp);
  return ret;
}

ssize_t Ehttp::__recv(void *buf, size_t len) {
  if (isClose()) {
    return -1;
  }

  if (!isSsl)
    return ::recv(getFD(), buf, len, 0);

  if (!ssl) return -1;

  int n;
  int err;

  // try read

  n = ::SSL_read(ssl, buf, len);

  if (n > 0)
    return n;

  if ((err = SSL_get_error(ssl, n)) != SSL_ERROR_WANT_READ
      && err != SSL_ERROR_WANT_WRITE)
    if (checkSslError() < 0) return -1;

  // no read, timeout > 0 - poll
  do {
    usleep(100);

    n = ::SSL_read(ssl, buf, len);
    if (checkSslError() < 0) return -1;

  } while (n < 0
           && ((err = SSL_get_error(ssl, n)) == SSL_ERROR_WANT_READ
               || err == SSL_ERROR_WANT_WRITE
               || err == SSL_ERROR_SYSCALL && errno == EAGAIN));

  return n;

  // blocking

  do {
    n = ::SSL_read(ssl, buf, len);
  } while (n <= 0 &&
           ((err = SSL_get_error(ssl, n)) == SSL_ERROR_WANT_READ
            || err == SSL_ERROR_WANT_WRITE));

  if (checkSslError() < 0) {
    return -1;
  }

  return n;
}


int Ehttp::getRequestType(void) {
  return requesttype;
}

bool Ehttp::isGetRequest(void) {
  return (requesttype==EHTTP_REQUEST_GET);
}

bool Ehttp::isPostRequest(void) {
  return (requesttype==EHTTP_REQUEST_POST);
}

string &Ehttp::getURL(void) {
  return url;
}

string &Ehttp::getFilename( void ) {
  return filename;
}

string Ehttp::getUrlParam(char *key) {
  map<string, string>::iterator it = global_parms.find(key);
  if (it != global_parms.end())
    return it->second;
  else
    return "";
}

string Ehttp::getPostParam(char *key) {
  map<string, string>::iterator it = global_parms.find(key);
  if (it != global_parms.end())
    return it->second;
  else
    return "";
}

map <string, string> & Ehttp::getPostParams( void ) {
  return global_parms;
}

map <string, string> & Ehttp::getUrlParams( void ) {
  return global_parms;
}

map <string, string> & Ehttp::getRequestHeaders( void ) {
  return request_header;
}

string Ehttp::getRequestHeader(char *key) {
  map<string, string>::iterator it = request_header.find(key);
  if (it != request_header.end())
    return it->second;
  else
    return "";
}

void Ehttp::out_replace_token(string tok, string val) {
  TLOCK(mutex_ehttp);
  __out_replace_token(tok, val);
  TUNLOCK(mutex_ehttp);
}

void Ehttp::__out_replace_token(string tok, string val) {
  replace_token[tok]=val;
}

void Ehttp::out_set_file(string fname, int ftype) {
  TLOCK(mutex_ehttp);
  __out_set_file(fname, ftype);
  TUNLOCK(mutex_ehttp);
}

void Ehttp::__out_set_file(string fname, int ftype) {
  outfilename=template_path + fname;
  filetype=ftype;
}

void Ehttp::out_buffer_clear(void) {
  TLOCK(mutex_ehttp);
  __out_buffer_clear();
  TUNLOCK(mutex_ehttp);
}

void Ehttp::__out_buffer_clear(void) {
  outbuffer="";
}

void Ehttp::out_write_str(char *str) {
  TLOCK(mutex_ehttp);
  __out_write_str(str);
  TUNLOCK(mutex_ehttp);
}

void Ehttp::__out_write_str(char *str) {
  outbuffer+=str;
}

void Ehttp::out_write_str(string &str) {
  TLOCK(mutex_ehttp);
  __out_write_str(str);
  TUNLOCK(mutex_ehttp);
}

void Ehttp::__out_write_str(string &str) {
  outbuffer+=str;
}

int Ehttp::out_replace(void) {
  TLOCK(mutex_ehttp);
  int ret = __out_replace();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__out_replace(void) {
  int r;
  int state=0;
  int err=0;
  int line=1;

  static char buffer[10240];
  string token;

  char c;

  if( filetype == EHTTP_BINARY_FILE ) return EHTTP_BINARY_FILE;

  FILE *f=fopen(outfilename.c_str(),"r");

  if( !f ) {
    err=-1;
    outbuffer="<BR>Cannot open the outfile <i>"+outfilename+"</i><BR>";
  }
  while( err== 0 && !feof(f) && f ) {
    r=fread(buffer,1,sizeof(buffer),f);
    if( r <= 0 || fdState == 1) return -1;
    // Read in the buffer and find tokens along the way
    for(int i=0;i<r;i++) {
      c=buffer[i];
      if( c=='\n' ) line++;


      switch( state ) {
        // non token state
      case 0:
        if( c == '#' ) state=1;
        else outbuffer+=c;
        break;

        // try to read the token start
      case 1:
        if( c == '#' ) {
          token="";
          state=2;
        }
        else{
          outbuffer+='#';
          outbuffer+=c;
          state=0;
        }
        break;

        // read the token key name
      case 2:
        if( c == '#' ) state=3;
        else token+=c;
        break;

        // close of token name, replace the token
      case 3:
        if( c == '#' ) {
          state=4;
          outbuffer+=replace_token[token];
          log(0) << "Replacing token [" << token << "] with [" << replace_token[token] << "]" << endl;
          state=0;
        } else {
          log(2) << "(" << line << ")Token Parse Error:" << token << endl;
          fseek(f,0,SEEK_END);
          err=-2;
          state=99;
        }
        break;

      case 99:
        outbuffer+=c;
        break;
      }
    }
  }
  if( f ) fclose(f);
  return EHTTP_ERR_OK;
}


int Ehttp::out_commit_binary(void) {
  TLOCK(mutex_ehttp);
  int ret = __out_commit_binary();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__out_commit_binary(void) {
  int err = 0;
  FILE *f = fopen(outfilename.c_str(), "rb");
  char buffer[10240];
  if (f) {
    while(!feof(f)) {
      int r = fread(buffer, 1, sizeof(10240), f);
      if (r > 0) {
        int remain = r;
        int total = remain;
        while(remain) {
          int w = __send(buffer + (total - remain), remain);
          log(0) << w << endl;
          if(w < 0 || fdState == 1) {
            err = w;
            remain = 0;
            fseek(f, SEEK_END, 0L);
          } else {
            remain -= w;
          }
        }
      }
    }
    fclose(f);
  }
  return err;
}

int Ehttp::__out_commit(int header) {
  TLOCK(mutex_ehttp);
  int ret = __out_commit(header);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::out_commit(int header) {
  int w;
  int err = 0;

  log(0) << "Outbuffer before outcommit [[[" << outbuffer << "]]]" << endl;
  if( filetype == EHTTP_BINARY_FILE) {
    return __out_commit_binary();
  }

  if( header == EHTTP_HDR_OK) {
    string headr("HTTP/1.0 200 OK\r\n");

    if (response_header.count("Content-Length") == 0) {
      stringstream ss;
      ss << outbuffer.size();
      response_header["Content-Length"] = ss.str();
    }

    map <string, string>::const_iterator iter;
    iter = response_header.begin();
    //Send out all the headers you want
    while(iter != response_header.end()) {
      headr += iter->first+string(": ") + iter->second+string("\r\n");
      ++iter;
    }

    outbuffer = headr + string("\r\n") + outbuffer;
  } else if(header == EHTTP_LENGTH_REQUIRED) {
    outbuffer=string("HTTP/1.0 411 Length Required\r\n\r\n");
  }

  log(0) << "Response : [[[" + outbuffer + "]]]" << endl;
  int remain = outbuffer.length();
  int total = remain;
  while (remain > 0) {
    w=__send(outbuffer.c_str() + (total - remain), remain);
    log(0) << w << endl;
    if(w < 0 || fdState == 1) {
      err = w;
      remain = 0;
    } else {
      remain -= w;
    }
  }
  return err;
}

int Ehttp::__init(void) {
  TLOCK(mutex_ehttp);
  int ret = __init();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::init(void) {
  log(0) << "Ehttp init..." << endl;
  pPreRequestHandler = NULL;
  return EHTTP_ERR_OK;
}

void Ehttp::add_handler(char *filename, int (*pHandler)(EhttpPtr obj)) {
  __add_handler(filename, pHandler);
}

void Ehttp::__add_handler(char *filename, int (*pHandler)(EhttpPtr obj)) {
  if( !filename ) {
    pDefaultHandler = pHandler;
  } else {
    handler_map[filename] = pHandler;
  }
}

int Ehttp::read_header(string *header) {
  TLOCK(mutex_ehttp);
  int ret = __read_header(header);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__read_header(string *header) {
  *header = "";
  size_t offset;
  log(0) << "read_header..." << endl;

  Byte buffer[INPUT_BUFFER_SIZE];

  while((offset = header->find("\r\n\r\n")) == string::npos) {
    int r = __recv(buffer, INPUT_BUFFER_SIZE - 1);

    log(0) << "read_header r:" << r << "/fdState : " << fdState << endl;

    if(r <= 0 || fdState == 1) {
      return EHTTP_ERR_GENERIC;
    }

    buffer[r] = 0;
    header->append(string(buffer, buffer + r));
  }
  log(0) << "Entire Request:-->" << *header << "<--" << endl;

  offset += 4;  // \r\n\r\n
  message = header->substr(offset);
  *header = header->substr(0, offset);

  log(0) << "Header:-->" << *header << "<--" << endl;

  log(0) << "Remain Data:-->" << message << "<--" << endl;

  return EHTTP_ERR_OK;
}

int Ehttp::parse_out_pairs(string &remainder, map <string, string> &parms) {
  TLOCK(mutex_ehttp);
  int ret = __parse_out_pairs(remainder, parms);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__parse_out_pairs(string &remainder, map <string, string> &parms) {
  string id;
  string value;
  int state = 0;

  log(0) << "parse_out_pairs..." << endl;
  // run through the string and pick off the parms as we see them
  for (unsigned int i=0; i < remainder.length();) {
    switch (state) {
    case 0:
      id = "";
      value = "";
      state = 1;
      break;
    case 1:
      switch (remainder[i]) {
      case '=':
        state = 2;
        break;
      default:
        id += remainder[i];
        break;
      }
      i++;
      break;
    case 2:
      switch (remainder[i]) {
      case '&':
        __unescape(&id);
        __unescape(&value);
        parms[id] = value;
        global_parms[id] = value;
        log(0) << "Added " << id << " to " << value << endl;
        state = 0;
        break;
      default:
        value += remainder[i];
        break;
      }
      i++;
      break;
    }
  }
  // Add non-nil value to parm list
  if( state == 2 ) {
    __unescape(&id);
    __unescape(&value);
    log(0) << "Added " << id << " to " << value << endl;
    parms[id]=value;
    global_parms[id] = value;
  }

  return EHTTP_ERR_OK;
}

int Ehttp::parse_header(string &header) {
  TLOCK(mutex_ehttp);
  int ret = __parse_header(header);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__parse_header(string &header) {
  char *request=NULL;
  char *request_end=NULL;
  char *pHeader = const_cast<char *>(header.c_str());

  log(0) << "parse_header..." << endl;
  filename="";
  contentlength=0;
  requesttype=-1;

  // Find end of request URL
  log(0) << "pHeader == [" << pHeader << "]" << endl;
  request_end=strstr(pHeader," HTTP/1.0\r\n");
  if (!request_end) {
    request_end=strstr(pHeader," HTTP/1.1\r\n");
  }
  if (!request_end) {
    log(2) << "ERROR " << __LINE__ << ":" << __FUNCTION__ << endl;
    return EHTTP_ERR_GENERIC;
  }

  // Is this a GET request
  if (requesttype == -1 ) {
    request=strstr(pHeader,"GET ");
  }
  if (request) {
    request+=4;
    requesttype=EHTTP_REQUEST_GET;
  }

  // Is this a POST request
  if (requesttype == -1) {
    request=strstr(pHeader,"POST ");
    if( request ) {
      request+=5;
      requesttype=EHTTP_REQUEST_POST;
    }
  }

  // Is this a PUT request
  if (requesttype == -1 ) {
    request=strstr(pHeader,"PUT ");
    if (request) {
      request+=4;
      requesttype=EHTTP_REQUEST_PUT;
    }
  }


  // didn't find a get,post,etc...
  if (requesttype == -1) {
    log(2) << "ERROR " << __LINE__ << ":" << __FUNCTION__ << endl;
    return EHTTP_ERR_GENERIC;
  }

  // malformed request
  if (request_end <= request) {
    log(2) << "ERROR " << __LINE__ << ":" << __FUNCTION__ << endl;
    return EHTTP_ERR_GENERIC;
  }

  // get the url requested
  while (request != request_end) {
    filename+=*request++;
  }

  // move to end of request
  request_end+=11;// length of " HTTP/1.1\r\n"

  string request_string_type;
  switch (requesttype) {
  case EHTTP_REQUEST_GET: request_string_type = "GET"; break;
  case EHTTP_REQUEST_POST: request_string_type = "POST"; break;
  case EHTTP_REQUEST_PUT: request_string_type = "PUT"; break;
  default: request_string_type = "DEFAULT";
  }
  log(0) << "URL: " << filename << " [" << request_string_type << "]" << endl;

  // Save the complete URL
  url=filename;

  // See if there are params passed on the request
  size_t idx=filename.find("?");
  if (idx != string::npos) {
    // Yank out filename minus parms which follow
    string remainder=filename.substr(idx+1);
    filename=filename.substr(0,idx);
    __parse_out_pairs(remainder, url_parms);
  }

  // Find request headers,
  while (*request_end!='\r' && *request_end!='\0') {
    char *keyend=strstr(request_end,": ");

    // get the key
    if (keyend && keyend>request_end) {
      string key,value;
      key="";
      value="";
      while (keyend > request_end) {
        key+=(*request_end++);
      }
      //get the value of the key
      request_end=keyend+2;
      char *valueend=strstr(request_end,"\r\n");

      //are we at end of header section
      if (!valueend) {
        valueend=request_end+strlen(request_end);
      }
      // read in the value
      if (valueend) {
        while (request_end < valueend) {
          value+=*request_end++;
        }
        //add key value pair to map
        to_upper(key);
        request_header[key]=value;
        log(0) << "Header : ..." << key << "... == ..." << value << "..." << endl;
        if (*request_end) {
          request_end+=2;
        }
      }
    } else {
      //Somthing went wrong
      log(2) << "ERROR " << __LINE__ << ":" << __FUNCTION__ << endl;
      return EHTTP_ERR_GENERIC;
    }
  }
  // Parse Cookie
  int ret = __parse_cookie(request_header["COOKIE"]);
  if (ret != EHTTP_ERR_OK) {
    return ret;
  }

  // Find content length
  contentlength = atoi(request_header["CONTENT-LENGTH"].c_str());
  return EHTTP_ERR_OK;
}

int Ehttp::getFD() {
  return socket;
}

int Ehttp::getContentLength() {
  return contentlength;
}

int Ehttp::unescape(string *str) {
  TLOCK(mutex_ehttp);
  int ret = __unescape(str);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__unescape(string *str) {
  size_t found = -1;
  while((found = str->find('%', found+1)) != string::npos) {
    if (found+2 >= str->size()) {
      return EHTTP_ERR_GENERIC;
    }
    int convert;
    std::istringstream iss(str->substr(found+1,2));
    iss >> hex >> convert;
    char replace_char = (char) convert;
    *str = str->substr(0, found) + replace_char + str->substr(found+3);
  }
  return EHTTP_ERR_OK;
}

int Ehttp::addslash(string *str) {
  TLOCK(mutex_ehttp);
  int ret = __addslash(str);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__addslash(string *str) {
  string tmp;
  for (unsigned int i = 0; i < str->length(); ++i) {
    if (str->at(i) == '\\') {
      tmp.push_back('\\');
    }
    tmp.push_back(str->at(i));
  }
  *str = tmp;
  return true;
}

int Ehttp::parse_cookie(string &cookie_string) {
  TLOCK(mutex_ehttp);
  int ret = __parse_cookie(cookie_string);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__parse_cookie(string &cookie_string) {
  vector<string> split_result;
  split(split_result, cookie_string, is_any_of(";"), token_compress_on);
  BOOST_FOREACH(string &pair, split_result) {
    vector<string> pair_split_result;
    split(pair_split_result, pair, is_any_of("="), token_compress_on);
    if ((int) pair_split_result.size() == 2) {
      trim(pair_split_result[0]);
      trim(pair_split_result[1]);
      __unescape(&pair_split_result[0]);
      __unescape(&pair_split_result[1]);
      ptheCookie[pair_split_result[0]] = pair_split_result[1];
    }
  }
  return EHTTP_ERR_OK;
}

int Ehttp::parse_message() {
  TLOCK(mutex_ehttp);
  int ret = __parse_message();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__parse_message() {
  if( !contentlength ) return EHTTP_ERR_OK;

  log(0) << "Parsed content length:" << contentlength << endl;
  log(0) << "Actual message length read in:" << message.length() << endl;

  // We got more than reported,so now what, truncate?
  // We can normally get +2 more than the reported lenght because of CRLF
  // So we'll hit this case often
  // ...
  // there is a possible strange bug here, need to really validate before truncate

  if(contentlength > message.length()) {
    log(0) << "READ MORE MESSAGE..." << endl;

    Byte buffer[INPUT_BUFFER_SIZE];
    while(contentlength > message.length()) {
      int r = __recv(buffer, INPUT_BUFFER_SIZE - 1);
      if(r <= 0 || fdState == 1) {
        return EHTTP_ERR_GENERIC;
      }
      log(0) << "r=" << r << ", remain=" << (contentlength - message.length())
             << ", buffersize=" << strlen(buffer) << endl;

      buffer[r]='\0';
      log(0) << "-->" << buffer << "<--" << endl;
      message.append(buffer);
    }
  }

  // Just in case we read too much...
  if( contentlength < message.length() ) {
    message = message.substr(0, contentlength);
  }

  // Got here, good, we got the entire reported msg length
  log(0) << "Entire message is <" << message << ">" << endl;
  __parse_out_pairs(message, post_parms);

  return EHTTP_ERR_OK;
}

// int Ehttp::parse_request() {
//   TLOCK(mutex_ehttp);
//   int ret = __parse_request();
//   log(0) << "######################################################" << endl;
//   TUNLOCK(mutex_ehttp);
//   return ret;
// }

int Ehttp::parse_request() {
  int (*pHandler)(EhttpPtr obj)=NULL;

  log(0) << "parse_request..." << endl;
  /* Things in the object which must be reset for each request */
  filename="";
  filetype=EHTTP_TEXT_FILE;
  url_parms.clear();
  post_parms.clear();
  global_parms.clear();
  request_header.clear();
  replace_token.clear();
  contentlength=0;

  string header;
  string message;

  if(__read_header(&header) != EHTTP_ERR_OK) {
    // non socket
    // log(2) << "Error parsing request" << endl;
    return EHTTP_ERR_GENERIC;
  }

  if(__parse_header(header) != EHTTP_ERR_OK) {
    log(2) << "Error parsing request" << endl;
    return EHTTP_ERR_GENERIC;
  }

  log(0) << " if(requesttyp ..." << endl;

  log(0) << filename << endl;
  if(filename == "/upload" && global_parms["command"] == "getfile") {
  } else {
    if (__parse_message() != EHTTP_ERR_OK) {
      log(2) << "Error parsing request" << endl;
      return EHTTP_ERR_GENERIC;
    }
  }

  // We are HTTP1.0 and need the content len to be valid
  // Opera Broswer
  // log(0) << "FD:" << fd << " if( contentlength==0 && requesttype==EHTTP_REQUEST_POST ) {" << endl;
  // if( contentlength==0 && requesttype==EHTTP_REQUEST_POST ) {
  //   log(2) << "Content Length is 0 and requesttype is EHTTP_REQUEST_POST" << endl;
  //   return out_commit(EHTTP_LENGTH_REQUIRED);
  // }

  log(0) << " out_buffer_clear" << endl;

  //Call the default handler if we didn't get the filename
  __out_buffer_clear();

  log(0) << " pPreRequestHandler" << endl;
  if( pPreRequestHandler ) pPreRequestHandler( shared_from_this() );
  if( !filename.length() ) {
    log(2) << __LINE__ << " Call default handler no filename" << endl;
    return pDefaultHandler( shared_from_this() );
  }

  log(0) << " pHandler=handler_map[filename]" << endl;

  //Lookup the handler function fo this filename
  pHandler=handler_map[filename];
  //Call the default handler if we didn't get the filename
  if( !pHandler ) {
    log(0) << __LINE__ << " Call default handler" << endl;
    return pDefaultHandler( shared_from_this() );
  }

  log(0) << " pHandler( shared_from_this() " << endl;
  log(0) << __LINE__ << " Call user handler" << endl;
  return pHandler( shared_from_this() );
}

map <string, string> & Ehttp::getResponseHeader( void ) {
  return response_header;
}

int Ehttp::isClose() {
  return fdState;
}

void Ehttp::close() {
  __close();
}

void Ehttp::__close() {
  if (fdState == 1) {
    log(0) << "Connection closed already... "<< endl;
    return;
  }

  log(0) << "Connection close... " << endl;
  ::shutdown(getFD(), SHUT_RDWR);
  ::close(getFD());
  fdState = 1;

  if (isSsl) {
    /*
    switch( SSL_shutdown(ssl) ) {
    case 1:
      // Shutdown complete
      break;
      // According to OpenSSL, we only need to call shutdown
      // again if we are going to keep the TCP Connection
      // open.  We are not, we are going to close, and doing
      // another shutdown causes everyone to stall..

    case 0:
      // 1/2 of bi-dir connection shut down
      // do it again
      sleep(1);
      SSL_shutdown(ssl);
      break;

    case -1:
      printf("Error shutting down connection %d",SSL_get_error(ssl,-1));
      break;

    default:
      break;
    }*/

    if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ssl = NULL;
    }
  }
}

int Ehttp::error(const string &error_message) {
  TLOCK(mutex_ehttp);
  int ret = __error(error_message);
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__error(const string &error_message) {
  if (!isClose()) {
    log(2) << error_message << endl;
    __out_set_file("errormessage.json");
    __out_replace_token("fail", error_message);
    __out_replace();
    __out_commit();
    __close();
  }
  return EHTTP_ERR_GENERIC;
}

int Ehttp::timeout() {
  TLOCK(mutex_ehttp);
  int ret = __timeout();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__timeout() {
  if (!isClose()) {
    log(0) << "timeout " << endl;
    __out_set_file("timeout.json");
    __out_replace();
    __out_commit();
    __close();
  }
  return EHTTP_ERR_GENERIC;
}

int Ehttp::uploadend() {
  TLOCK(mutex_ehttp);
  int ret = __uploadend();
  TUNLOCK(mutex_ehttp);
  return ret;
}

int Ehttp::__uploadend() {
  if (!isClose()) {
    log(0) << "upload end " << endl;
    __out_set_file("request.json");
    __out_replace_token("jsondata", "\"\"");
    __out_replace();
    __out_commit();
    __close();
  }
  return EHTTP_ERR_GENERIC;
}

ostream & Ehttp::log(int debuglevel) {
  if (username == "")
    debuglevel = -1;
  ostream& out = ::log(debuglevel);
  out << "[" << username << "] (" << getFD() << ") ";
  return out;
}
