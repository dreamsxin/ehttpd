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

using namespace std;
using namespace boost;


enum{
  EHTTP_ERR_GENERIC=-1,EHTTP_ERR_OK,EHTTP_ERR_CONNECTLOST,EHTTP_ERR_APP,
  EHTTP_ERR_REGEX,
  EHTTP_HDR_NOTHING, EHTTP_HDR_OK,EHTTP_BINARY_FILE,EHTTP_TEXT_FILE,
  EHTTP_REQUEST_GET, EHTTP_REQUEST_POST, EHTTP_REQUEST_PUT, EHTTP_LENGTH_REQUIRED
};

ssize_t ehttpRecv(void *ctx, void *buf, size_t len);
ssize_t ehttpSend(void *ctx, const void *buf, size_t len);


#define INPUT_BUFFER_SIZE 10240

/*
 Class: ehttp
 EasyHTTPD Parser Class
 */

static int ehttp_inst_count = 0;

typedef char Byte;

class Ehttp: public boost::enable_shared_from_this<Ehttp> {
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
  map <string, string> ptheCookie;
  string message;

  time_t timestamp;

  int getFD();
  int getContentLength();
  int unescape(string *str);
  int addslash(string *str);
  int parse_cookie(string &cookie_string);

  ssize_t (*pRecv)(void *ctx, void *buf, size_t len);
  ssize_t (*pSend)(void *ctx, const void *buf, size_t len);

  /****************************************************************
   Constructor: Ehttp
   Does nothing exciting, use init() to initalize the class
   instantiation
   */
  Ehttp(){
    fdState=0;
    ++ehttp_inst_count;
    log(0) << "new Ehttp() : " << ehttp_inst_count <<  endl;
    response_header["Content-Type"] = "text/html; charset=utf-8";
    timestamp = time(NULL);
  };

  /*
   Destructor: Ehttp
   Does nothing exciting.
   */
  ~Ehttp(){
    --ehttp_inst_count;
    log(0) << "~Ehttp() : " << ehttp_inst_count <<  endl;
    if (!isClose())
      error("Critial Error");
  };



  /****************************************************************
   Member Function: int init( void );

   Usage:
   Initalizes the class instantiation

   Parameters:
   none

   Returns:
   EHTTP_ERR_OK    Initalization OK
   EHTTP_ERR_REGEX    Error compiling internal regular
   expression
   */
  int init( void );



  /****************************************************************
   Member Function:
   int getRequestType(void);
   int isGetRequest();
   int isPostRequest();


   Usage:
   Indicates what type of request is being made

   Parameters:
   none

   Returns:
   EHTTP_REQUEST_GET    HTTP Get request
   EHTTP_REQUEST_POST    HTTP Post request


   */
  int getRequestType(void);
  bool isGetRequest(void);
  bool isPostRequest(void);


  /****************************************************************
   Member Function:
   string &getURL(void);

   Usage:
   Get string reference to the complete URL requested.
   For use inside a user defined Ehttp request handler

   Parameters:
   none

   Returns:
   Return reference to the URL requested.
   URL is considered to be the filename requested plus any
   parameters that follow.

   Example:

   http://localhost/doit.html?CMD=DELETE

   string reference contains /doit.html?CMD=DELETE

   Filename is 'localhost', CMD is a parameter, and DELETE is the
   value of CMD

   */
  string &getURL(void);

  /****************************************************************
   Member Function:
   string &getFilename( void );

   Usage:
   Get string reference to the URL filename requested.
   For use inside a user defined Ehttp request handler

   Parameters:
   none

   Returns:
   Return reference to the filename requested.

   Example:

   http://localhost/doit.html?CMD=DELETE

   string reference contains /doit.html

   Filename is 'doit.html', CMD is a parameter, and DELETE is the value of CMD

   */
  string &getFilename( void );


  /****************************************************************
   Member Function:
   string getUrlParam(char *key);

   Usage:
   Return the value of a parameter passed in URL (request)

   For use inside a user defined Ehttp request handler

   Parameters:
   char * string, name of URL parameter to retrieve

   Returns:
   A copy of the parameter value as a STL string object

   Example:

   http://localhost/doit.html?CMD=DELETE&FILE=foo.txt

   string cmd=Ehttp_obj.getUrlParam("CMD");
   string val=Ehttp_obj.getUrlParam("FILE");

   cmd contains "DELETE" and val contains "foo.txt"


   */
  string getUrlParam(char *key);


  /****************************************************************
   Member Function:
   string getPostParam(char *key);

   Usage:
   Return the value of a POST (form) parameter returned by the
   browser as part of a POST request

   For use inside a user defined Ehttp request handler


   Parameters:
   char * string, name of parameter/name of form control to
   retrieve

   Returns:
   A copy of the parameter value as a STL string object

   Example:

   http://localhost/postit.html

   string firstname=Ehttp_obj.getPostParam("full_name");
   string button=Ehttp_obj.getPostParam("submit");

   <input size="40" type="text" name="full_name"</input>

   firstname contains the text of the text control named full_name
   */
  string getPostParam(char *key);



  /****************************************************************
   Member Function:
   map <string, string> getPostParams( void );
   map <string, string> getUrlParams( void );

   Usage:
   Get a map <string, string> of all GET/POST values, where each
   element in the list, first string object is the name of the
   parameter/form control name and the second string object is
   the value of that parameter.

   It's usefullness come in where you for example, have an HTML
   form with a bunch of checkboxes, but only a subset of those
   boxes have been checked by the user.  The browser will only
   post the values of those boxes which were checked.

   You can itterate through the list, to know which boxes
   were checked.

   For use inside a user defined Ehttp request handler


   Parameters:
   Reference to a map <string, string> object.


   Returns:
   nothing

   Example:

   map <string, string> parms;
   map<string,string>::const_iterator iter;

   // Print out all the checkbox names
   getPostParms(parms);
   iter=parms.begin();
   while( iter != parms.end() )
   {
   cout << iter->first->data()<< iter->second->data() << "\r\n";
   ++iter;
   }

   */
  map <string, string> & getPostParams( void );
  map <string, string> & getUrlParams( void );


  /****************************************************************
   Member Function:
   void out_replace_token( string tok, string val );

   Usage:
   Used to replace a named token inside an HTML template value
   with a string.

   Each call to this function adds the token and string to
   a list.  The token replacments occur when out_replace()
   is called

   Used for dynamic content generation.

   For use inside a user defined Ehttp request handler


   Parameters:
   tok -  Name of token to be replaced
   val  -  Content to replace the token with


   Returns:
   nothing

   Example:

   Part of template file:
   <TITLE>This is ##NAME##'s Website</TITLE>

   obj.out_replace_token( "NAME", "Billy Bob" );
   obj.out_replace_token( "OCCUPATION", "Engineer" );
   obj.out_replace();
   obj.out_commit();

   Will render <TITLE>This is Billy Bob's Website</TITLE>
   on out_commit();

   */
  void out_replace_token( string tok, string val );

  /****************************************************************
   Member Function:
   void out_set_file( char *fname, int ftype=EHTTP_TEXT_FILE);
   void out_set_file( string &fname, int ftype=EHTTP_TEXT_FILE);


   Usage:
   Specifies the filename of the local html template.

   Used for dynamic content generation.

   For use inside a user defined Ehttp request handler


   Parameters:
   fname -  Name of the local HTML template file
   ftype-  EHTTP_TEXT_FILE (default)
   EHTTP_BINARY_FILE

   If ftype is EHTTP_BINARY_FILE, then the file is written
   to the browser as is, with no token replacement, on
   out_commit()


   Returns:
   nothing

   Example:

   obj.out_set_file("./status.html");
   obj.out_replace_token( "STATUS", "[S]ituation [N]ormal AFU" );
   obj.out_replace();
   obj.out_commit();

   */
  void out_set_file( char *fname, int ftype=EHTTP_TEXT_FILE);
  void out_set_file( string &fname, int ftype=EHTTP_TEXT_FILE);

  /****************************************************************
   Member Function:
   void out_write_str( char *str );
   void out_write_str( string &str );


   Usage:
   Writes a string directly to the output buffer.

   The output buffer is empty when the handler is entered.
   Calls to these functions append to the output buffer.
   The function out_replace() appends to the output buffer,
   so this function may be called before or after, if at all
   out_replace.

   Parameters:
   str  - String to append to the output buffer


   Returns:
   nothing

   Example:
   obj.out_write_str("HTTP/1.0 400 Bad Request\r\n\r\n");
   obj.out_commit(EHTTP_HDR_NOTHING);

   */
  void out_write_str( char *str );
  void out_write_str( string &str );


  /****************************************************************
   Member Function:
   int out_replace(void);


   Usage:

   In short, read the template file specified with out_set_file,
   and replace tokens found in the file with tokens found in the
   internal list, where the internal list was populated via
   calls to out_replace_token

   Causes the output buffer to be filled or appended to.

   Parameters:
   none


   Returns:
   nothing

   Example:
   obj.out_set_file("./status.html");
   obj.out_replace_token( "STATUS", "[S]ituation [N]ormal AFU" );

   obj.out_replace();    // Replace ##STATUS## in status.html
   // with the SNAFU string

   obj.out_commit();    // Write the content to the client
   */
  int out_replace(void);


  /****************************************************************
   Member Function:
   int out_commit_binary(void);


   Usage:
   Write the file specified by out_set_file() directly to the
   connected socket, with no translation.

   If out_set_file is called with the ftype as EHTTP_BINARY_FILE,
   then this function is called automatically via out_commit();

   Parameters:
   none


   Returns:
   EHTTP_ERR_OK - no error

   or returns the error code from the TCP socket we tried
   to write to

   Example:
   obj.out_set_file("./status.html");
   obj.out_replace_token( "STATUS", "[S]ituation [N]ormal AFU" );

   obj.out_replace();    // Replace ##STATUS## in status.html
   // with the SNAFU string

   obj.out_commit();    // Write the content to the client
   */
  int out_commit_binary(void);


  /****************************************************************
   Member Function:
   int out_commit(int header=EHTTP_HDR_OK);


   Usage:
   Writes the output buffer to the connected socket.

   The is the last member function to be called when sending the
   response from the handler function.

   Parameters:
   header -
   EHTTP_HDR_OK
   Prepends the response with HTTP 200 OK header

   Other values are for class internal use.


   Returns:
   EHTTP_ERR_OK - no error

   or returns the error code from the TCP socket we tried
   to write to

   Example:
   obj.out_set_file("./status.html");
   obj.out_replace_token( "STATUS", "[S]ituation [N]ormal AFU" );

   obj.out_replace();    // Replace ##STATUS## in status.html
   // with the SNAFU string

   obj.out_commit();    // Write the content to the client
   */
  int out_commit(int header=EHTTP_HDR_OK);


  /****************************************************************
   Member Function:
   int parse_request( int fd, void *cookie );


   Usage:
   Read in an HTTP request from a connected socket and ultimatly
   write out the response to the connected socket, usually through
   one of the handler function

   Parameters:
   fd -   TCP socket from which we accepted a connection

   cookie -This value is passed directly to the handler functions,
   and its intended use is to provide a link/pointer between
   the request handler and the application.


   Returns:
   EHTTP_ERR_OK -
   no error, usually returned from the request handler

   EHTTP_LENGTH_REQUIRED -
   Parser got a post request, but there was no Content-Lengh in
   the header send by the browser.

   EHTTP_ERR_GENERIC -
   Could not parse the request.

   The request handler could return anything it wants, but it should
   return the value from out_commit();


   Example:
   Ehttp  httpd;
   MyApp  app;
   app.runInThread();
   httpd.init();
   while( 1 )
   {
   fromlen = sizeof(struct sockaddr);
   if ( (connect_d = accept(sd, (struct sockaddr *) &fsin, (socklen_t*)&fromlen)) < 0)
   {
   fprintf(stderr, "HTTPD:Can't accept....exiting\n");
   close(sd);
   exit(1);
   }

   httpd.parse_request(connect_d, (void)&app);
   close(connect_d);
   }
   */
  int parse_request(int fd);


  /****************************************************************
   Member Function:
   void add_handler( char *filename, int
   (*pHandler)(Ehttp &obj));


   Usage:
   Add a request handler function to the Ehttp parser instance


   Parameters:
   filename -   Name of URL file to handle (index.html for example)
   pHandler -  Pointer to handler funciton, with reference to the
   Ehttp instance, and a void pointer (pointer to app)


   Returns:
   nothing


   Example:

   // Handler: diagreport.html
   int handleDiagReport( Ehttp &obj)
   {
   AppToGUI &app=CookieToApp(cookie);

   // Load the template, replace the tokens
   obj.out_set_file(obj."diagreport.html");
   obj.out_replace_token("CONTENT",app.getDiagCounterReport());

   // write the steam and finish connectoin
   obj.out_replace();
   return obj.out_commit();
   }


   Ehttp  httpd;
   MyApp  app;
   ...
   obj.add_handler("/", handleIndex );
   obj.add_handler("/index.html", handleIndex );
   obj.add_handler("/diagreport.html", handleDiagReport);


   */
  void add_handler( char *filename, int (*pHandler)(EhttpPtr obj));


  /****************************************************************
   Member Function:
   void set_prerequest_handler( void (*pHandler)(Ehttp &obj, void *cookie));

   Usage:
   Sets a handler function to be called for all specified requests handlers
   before the specific request handler get called.

   It's purpose is to allow action to be taken globally which effects all
   requests handlers.

   As an example, you might want to update a path to HTML files depending
   on the type of browser used.  This way you would not need to check the
   browser type in every request handler function

   */
  void set_prerequest_handler( void (*pHandler)(EhttpPtr obj));

  /****************************************************************
   Member Function:
   map <string, string> & getRequestHeaders( void );

   Usage:
   Returns reference to a  map of the parsed out headers sent by the client.

   This is used to get the value a header.

   Example:
   string cookie=getRequestHeaders()["User-Agent"];

   */
  map <string, string> & getRequestHeaders( void );


  /****************************************************************
   Member Function:
   string getRequestHeader(char *key);

   Usage:
   Returns the value of a specific header as a string


   Example:
   int timeout=atol( getReqeustHeader("Keep-Alive").c_str() );
   */
  string getRequestHeader(char *key);

  /****************************************************************
   Member Function:
   map <string, string> & getResponseHeader( void );

   Usage:
   Returns reference to a map of header to send back to the client.

   Used to send a header to the client.

   Exampe:
   getResponseHeader()['Set-Cookie']="USERID=42; path=/index.html;"


   */
  map <string, string> & getResponseHeader( void );

  /****************************************************************
   Member Function:
   void setSendFunc( ssize_t (*pS)(void *fd, const void *buf, size_t len, void *cookie) );
   void setRecvFunc( ssize_t (*pR)(void *fd, void *buf, size_t len, void *cookie) );


   Usage:
   Used to specify alternate I/O functions, where the defaults are
   send() and recv().

   The intention is to allow the use of SSL, which has its own stream functions.

   See 'hellowworldssl.cpp' for a detailed example of usage.

   Paramters:
   fd -   fd(socket), just like send() and recv()
   buf-  pointer to input/output buffer
   len-  size of the buffer
   cookie-  cookie passed in the parse_request function. For your own use.
   */
  void setSendFunc( ssize_t (*pS)(void *fd, const void *buf, size_t len) );
  void setRecvFunc( ssize_t (*pR)(void *fd, void *buf, size_t len) );

  void close();
  int isClose();

  int error(const string &error_message);
};

typedef shared_ptr<Ehttp> EhttpPtr;
