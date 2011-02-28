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
#include <assert.h>

ssize_t EhttpRecv(void *fd, void *buf, size_t len) {
  int ret = recv((int)fd,buf,len,0);
  if (ret == -1) {
    int nError = errno;
    log(0) << "RECV ERROR!! CODE = " <<  nError << endl;
  }
  return ret;
}

ssize_t EhttpSend(void *fd, const void *buf, size_t len) {
  return send((int)fd,buf,len,0);
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
  return global_parms[key];
}

string Ehttp::getPostParam(char *key) {
  return global_parms[key];
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
  return request_header[key];
}

void Ehttp::out_replace_token(string tok, string val) {
  replace_token[tok]=val;
}

void Ehttp::out_set_file(string fname, int ftype) {
  outfilename=template_path + fname;
  filetype=ftype;
}

void Ehttp::out_buffer_clear(void) {
  outbuffer="";
}

void Ehttp::out_write_str(char *str) {
  outbuffer+=str;
}

void Ehttp::out_write_str(string &str) {
  outbuffer+=str;
}

int Ehttp::out_replace(void) {
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
          int w = pSend((void *)sock, buffer + (total - remain), remain);
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

int Ehttp::out_commit(int header) {
  int w;
  int err = 0;

  if( filetype == EHTTP_BINARY_FILE) {
    return out_commit_binary();
  }

  if( header == EHTTP_HDR_OK) {
    string headr("HTTP/1.0 200 OK\r\n");
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
    w=pSend((void *)sock, outbuffer.c_str() + (total - remain), remain);
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

int Ehttp::init(void) {
  log(0) << "Ehttp init..." << endl;
  pSend = EhttpSend;
  pRecv = EhttpRecv;
  pPreRequestHandler = NULL;
  return EHTTP_ERR_OK;
}

void Ehttp::add_handler(char *filename, int (*pHandler)(EhttpPtr obj)) {
  if( !filename ) {
    pDefaultHandler = pHandler;
  } else {
    handler_map[filename] = pHandler;
  }
}

void Ehttp::set_prerequest_handler(void (*pHandler)(EhttpPtr obj)) {
  pPreRequestHandler = pHandler;
}

int Ehttp::read_header(string *header) {
  *header = "";
  unsigned int offset = 0;
  log(0) << "read_header..." << endl;

  Byte buffer[INPUT_BUFFER_SIZE];

  while((offset = header->find("\r\n\r\n")) == string::npos) {
    int r = pRecv((void*)sock, buffer, INPUT_BUFFER_SIZE - 1);

    log(0) << "read_header("<<sock<<") r:" << r << "/fdState : " << fdState << endl;

    if(r <= 0 || fdState == 1) {
      return EHTTP_ERR_GENERIC;
    }

    buffer[r] = 0;
    header->append(string(buffer, buffer + r));
  }

  offset += 4;  // \r\n\r\n
  message = header->substr(offset);
  *header = header->substr(0, offset);

  log(0) << "Header:-->" << *header << "<--" << endl;

  log(0) << "Remain Data:-->" << message << "<--" << endl;

  return EHTTP_ERR_OK;
}

int Ehttp::parse_out_pairs(string &remainder, map <string, string> &parms) {
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
            unescape(&id);
            unescape(&value);
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
    unescape(&id);
    unescape(&value);
    log(0) << "Added " << id << " to " << value << endl;
    parms[id]=value;
    global_parms[id] = value;
  }

  return EHTTP_ERR_OK;
}


int Ehttp::parse_header(string &header) {
  char *request=NULL;
  char *request_end=NULL;
  const char *pHeader=header.c_str();

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
  log(1) << "URL: " << filename << " [" << request_string_type << "]"
         << " (" << getFD() << ")" << endl;

  // Save the complete URL
  url=filename;

  // See if there are params passed on the request
  int idx=filename.find("?");
  if (idx > -1) {
    // Yank out filename minus parms which follow
    string remainder=filename.substr(idx+1);
    filename=filename.substr(0,idx);
    parse_out_pairs(remainder, url_parms);
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
  int ret = parse_cookie(request_header["COOKIE"]);
  if (ret != EHTTP_ERR_OK) {
    return ret;
  }

  // Find content length
  contentlength = atoi(request_header["CONTENT-LENGTH"].c_str());
  return EHTTP_ERR_OK;
}

int Ehttp::getFD() {
  return sock;
}

int Ehttp::getContentLength() {
  return contentlength;
}

int Ehttp::unescape(string *str) {
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
  vector<string> split_result;
  split(split_result, cookie_string, is_any_of(";"), token_compress_on);
  BOOST_FOREACH(string &pair, split_result) {
    vector<string> pair_split_result;
    split(pair_split_result, pair, is_any_of("="), token_compress_on);
    if ((int) pair_split_result.size() == 2) {
      trim(pair_split_result[0]);
      trim(pair_split_result[1]);
      unescape(&pair_split_result[0]);
      unescape(&pair_split_result[1]);
      ptheCookie[pair_split_result[0]] = pair_split_result[1];
    }
  }
  return EHTTP_ERR_OK;
}

int Ehttp:: parse_message() {
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
      int r = pRecv((void*)sock, buffer, INPUT_BUFFER_SIZE - 1);
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
  parse_out_pairs(message, post_parms);

  return EHTTP_ERR_OK;
}


int Ehttp::parse_request(int fd) {
  int (*pHandler)(EhttpPtr obj)=NULL;

  log(0) << "parse_request..." << endl;
  /* Things in the object which must be reset for each request */
  filename="";
  sock=fd;
  filetype=EHTTP_TEXT_FILE;
  url_parms.clear();
  post_parms.clear();
  global_parms.clear();
  request_header.clear();
  replace_token.clear();
  contentlength=0;

  string header;
  string message;


  log(0) << "FD:" << fd << " read_header" << endl;
  if(read_header(&header) != EHTTP_ERR_OK) {
    log(2) << "Error parsing request" << endl;
    return EHTTP_ERR_GENERIC;
  }

  log(0) << "FD:" << fd << " parse_header" << endl;
  if(parse_header(header) != EHTTP_ERR_OK) {
    log(2) << "Error parsing request" << endl;
    return EHTTP_ERR_GENERIC;
  }

  log(0) << "FD:" << fd << " if(requesttyp ..." << endl;

  if(requesttype != EHTTP_REQUEST_PUT && parse_message() != EHTTP_ERR_OK) {
    log(2) << "Error parsing request" << endl;
    return EHTTP_ERR_GENERIC;
  }

  // We are HTTP1.0 and need the content len to be valid
  // Opera Broswer
  log(0) << "FD:" << fd << " if( contentlength==0 && requesttype==EHTTP_REQUEST_POST ) {" << endl;
  if( contentlength==0 && requesttype==EHTTP_REQUEST_POST ) {
    log(2) << "Content Length is 0 and requesttype is EHTTP_REQUEST_POST" << endl;
    return out_commit(EHTTP_LENGTH_REQUIRED);
  }

  log(0) << "FD:" << fd << " out_buffer_clear" << endl;

  //Call the default handler if we didn't get the filename
  out_buffer_clear();

  log(0) << "FD:" << fd << " pPreRequestHandler" << endl;
  if( pPreRequestHandler ) pPreRequestHandler( shared_from_this() );
  if( !filename.length() ) {
    log(2) << __LINE__ << " Call default handler no filename" << endl;
    return pDefaultHandler( shared_from_this() );
  }

  log(0) << "FD:" << fd << " pHandler=handler_map[filename]" << endl;

  //Lookup the handler function fo this filename
  pHandler=handler_map[filename];
  //Call the default handler if we didn't get the filename
  if( !pHandler ) {
    log(0) << __LINE__ << " Call default handler" << endl;
    return pDefaultHandler( shared_from_this() );
  }

  log(0) << "FD:" << fd << " pHandler( shared_from_this() " << endl;
  log(0) << __LINE__ << " Call user handler" << endl;
  return pHandler( shared_from_this() );
}

void Ehttp::setSendFunc( ssize_t (*pS)(void *fd, const void *buf, size_t len) ) {
  if( pS )
    pSend=pS;
  else
    pSend=EhttpSend;
}

void Ehttp::setRecvFunc( ssize_t (*pR)(void *fd, void *buf, size_t len) ) {
  if( pR )
    pRecv=pR;
  else
    pRecv=EhttpRecv;
}

map <string, string> & Ehttp::getResponseHeader( void ) {
  return response_header;
}

int Ehttp::isClose() {
  return fdState;
}

void Ehttp::close() {
  if (fdState == 1) {
    log(2) << "Connection closed already... (" << sock << ")" << endl;
    return;
  }
  log(1) << "Connection close... (" << sock << ")" << endl;
  // ::close(sock);
  ::shutdown(sock, SHUT_RDWR);
  ::close(sock);
  fdState = 1;
}

int Ehttp::error(const string &error_message) {
  log(2) << error_message << "(" << sock << ")" << endl;
  out_set_file("errormessage.json");
  out_replace_token("fail", error_message);
  out_replace();
  out_commit();
  close();
  return EHTTP_ERR_GENERIC;
}
