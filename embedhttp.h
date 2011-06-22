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

#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <crypt.h>
#include <errno.h>
#include <string.h>
#include <list>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "./log.h"
#include "ssl.h"

using namespace std;
using namespace boost;


enum{
  EHTTP_ERR_GENERIC=-1,EHTTP_ERR_OK,EHTTP_ERR_CONNECTLOST,EHTTP_ERR_APP,
  EHTTP_ERR_REGEX,
  EHTTP_HDR_NOTHING, EHTTP_HDR_OK,EHTTP_BINARY_FILE,EHTTP_TEXT_FILE,
  EHTTP_REQUEST_GET, EHTTP_REQUEST_POST, EHTTP_REQUEST_PUT, EHTTP_LENGTH_REQUIRED
};


#define INPUT_BUFFER_SIZE 10240

/*
 Class: ehttp
 EasyHTTPD Parser Class
 */

static int ehttp_inst_count = 0;

typedef char Byte;

class Ehttp: public boost::enable_shared_from_this<Ehttp> {
  static string template_path;
  static string save_path;

  int fdState;
  int sock;
  int filetype;
  int requesttype;
  unsigned int contentlength;

  string filename;            //filename of the url
  string url;                //the url
  string outfilename;            //template filename
  string outbuffer;            //response output buffer

  map <string, string> url_parms;
  map <string, string> post_parms;
  map <string, string> global_parms;
  map <string, string> request_header;
  map <string, string> replace_token;
  map <string, string> response_header;

  typedef shared_ptr<Ehttp> EhttpPtr;
  map <string, int (*)(EhttpPtr obj)> handler_map;
  int (*pDefaultHandler)(EhttpPtr obj);
  void (*pPreRequestHandler)(EhttpPtr obj);

  int read_header(string *header);
  int parse_header(string &header);
  int parse_out_pairs(string &remainder, map <string, string> &parms );
  int parse_message();
  void out_buffer_clear(void);

public:
  static void set_template_path(string p) {
    Ehttp::template_path = p;
  };
  static void set_save_path(string p) {
    Ehttp::save_path = p;
  };
  static string &get_save_path() {
    return Ehttp::save_path;
  };

  map <string, string> ptheCookie;
  string message;

  time_t timestamp;

  int getFD();
  int getContentLength();
  int unescape(string *str);
  int addslash(string *str);
  int parse_cookie(string &cookie_string);

  Ehttp(){
    ssl = NULL;
    isSsl = false;
    fdState=0;
    ++ehttp_inst_count;
    log(0) << "new Ehttp() : " << ehttp_inst_count <<  endl;
    response_header["Content-Type"] = "text/html; charset=utf-8";
    timestamp = time(NULL);
  };

  ~Ehttp(){
    --ehttp_inst_count;
    log(0) << "~Ehttp() : " << ehttp_inst_count <<  endl;
    if (!isClose())
      error("Critial Error");
  };

  int init( void );

  int getRequestType(void);
  bool isGetRequest(void);
  bool isPostRequest(void);

  string &getURL(void);

  string &getFilename( void );

  string getUrlParam(char *key);

  string getPostParam(char *key);

  map <string, string> & getPostParams( void );
  map <string, string> & getUrlParams( void );

  void out_replace_token( string tok, string val );

  void out_set_file( string fname, int ftype=EHTTP_TEXT_FILE);

  void out_write_str( char *str );
  void out_write_str( string &str );


  int out_replace(void);

  int out_commit_binary(void);

  int out_commit(int header=EHTTP_HDR_OK);

  int parse_request();

  void add_handler( char *filename, int (*pHandler)(EhttpPtr obj));

  void set_prerequest_handler( void (*pHandler)(EhttpPtr obj));

  map <string, string> & getRequestHeaders( void );

  string getRequestHeader(char *key);

  map <string, string> & getResponseHeader( void );

  void close();
  int isClose();

  int error(const string &error_message);
  int timeout();
  int uploadend();

  bool isSsl;
  int socket;
  SSL *ssl;
  ssize_t send(const void *buf, size_t len);
  ssize_t recv(void *buf, size_t len);
};

typedef shared_ptr<Ehttp> EhttpPtr;

