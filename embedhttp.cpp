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

#define	dprintf printf


ssize_t ehttpRecv(void *fd, void *buf, size_t len, void *cookie)
		{
		return recv((int)fd,buf,len,0);
		}

ssize_t ehttpSend(void *fd, const void *buf, size_t len, void *cookie)
		{
		return send((int)fd,buf,len,0);
		}


int ehttp::getRequestType(void)
		{
		return requesttype;
		}

bool ehttp::isGetRequest(void)
		{
		return (requesttype==EHTTP_REQUEST_GET);
		}	

bool ehttp::isPostRequest(void)
		{
		return (requesttype==EHTTP_REQUEST_POST);
		}	
	

string & ehttp::getURL(void)
		{
		return url;
		}



string & ehttp::getFilename( void )
		{
		return filename;
		}

string ehttp::getUrlParam(char *key)
		{
		return url_parms[key];
		}

string ehttp::getPostParam(char *key)
		{
		return post_parms[key];
		}

map <string, string> & ehttp::getPostParams( void )
		{
		return post_parms;
		}

map <string, string> & ehttp::getUrlParams( void )
		{
		return url_parms;
		}

map <string, string> & ehttp::getRequestHeaders( void )
		{
		return request_header;
		}

string ehttp::getRequestHeader(char *key)
		{
		return request_header[key];
		}

void ehttp::out_replace_token( string tok, string val )
		{
		replace_token[tok]=val;
		}

void ehttp::out_set_file( char *fname, int ftype)
		{
		outfilename=fname;
		filetype=ftype;
		}

void ehttp::out_set_file(  string &fname, int ftype)
		{
		outfilename=fname;
		filetype=ftype;
		}	

void ehttp::out_buffer_clear(void)
		{
		outbuffer="";
		}

void ehttp::out_write_str( char *str )
		{
		outbuffer+=str;
		}
		
void ehttp::out_write_str( string &str )
		{
		outbuffer+=str;
		}

int ehttp::out_replace(void)
		{
		int r;
		int state=0;
		int err=0;
		int line=1;

		static char buffer[10240];
		string token;

		char c;

		if( filetype == EHTTP_BINARY_FILE ) return EHTTP_BINARY_FILE;

		FILE *f=fopen(outfilename.c_str(),"r");
		
		if( !f )
			{
			err=-1;
			outbuffer="<BR>Cannot open the outfile <i>"+outfilename+"</i><BR>";
			}
		while( err== 0 && !feof(f) && f )
			{
			r=fread(buffer,1,sizeof(buffer),f);
			if( r < 0 ) return -1;
			// Read in the buffer and find tokens along the way
			for(int i=0;i<r;i++)
				{
				c=buffer[i];
				if( c=='\n' ) line++;

				
				switch( state )
					{
					// non token state 
					case 0:
						if( c == '#' ) state=1;
						else outbuffer+=c;
						break;

					// try to read the token start
					case 1:
						if( c == '#' )
							{ 
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
						if( c == '#' )
							{
							state=4;
							outbuffer+=replace_token[token];
							dprintf("Replacing token [%s] with [%s]\r\n",token.c_str(),(replace_token[token]).c_str());
							state=0;
							}
						else
							{
							dprintf("(%d)Token Parse Error:%s \r\n",line,token.c_str());
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


int ehttp::out_commit_binary(void)
		{
		int err=0;
		FILE *f=fopen(outfilename.c_str(),"rb");
		char buffer[10240];
		if(f)
			{
			while( !feof(f) )
				{
				int r=fread(buffer,1,sizeof(10240),f);
				if( r>0 )
					{
					int remain=r;
					int total=remain;
					while( remain )
						{
						int w=pSend((void*)localFD,buffer+(total-remain),remain,ptheCookie);
						if( w<0 )
							{
							err=w;
							remain=0;
							fseek(f,SEEK_END,0L);
							}
						else
							{
							remain-=w;
							}
						}
					}
				}
			fclose(f);
			}
			return err;
		}

int ehttp::out_commit(int header)
		{
		int w;
		int err=0;

		if( filetype == EHTTP_BINARY_FILE )
			return out_commit_binary();


		if( header == EHTTP_HDR_OK )
			{
			string headr("HTTP/1.0 200 OK\r\n");
			map <string, string>::const_iterator iter;
			iter=response_header.begin();
			//Send out all the headers you want
			while( iter != response_header.end() )
				{
				headr+=iter->first+string(": ")+iter->second+string("\r\n");
				++iter;
				}
			outbuffer=headr+string("\r\n")+outbuffer;
			}
		else if( header == EHTTP_LENGTH_REQUIRED )
			{
			outbuffer=string("HTTP/1.0 411 Length Required\r\n\r\n");
			}
			

		int remain=outbuffer.length();
		int total=remain;
		while( remain )
			{
			w=pSend((void*)localFD,outbuffer.c_str()+(total-remain),remain,ptheCookie);
			if( w<0 )
				{
				err=w;
				remain=0;
				}
			else
				remain-=w;
			}
		return err;
		}

int ehttp::init( void )
	{
	pSend=ehttpSend;
	pRecv=ehttpRecv;
	pPreRequestHandler=NULL;
	return EHTTP_ERR_OK;
	}


void ehttp::add_handler( char *filename, int (*pHandler)(ehttp &obj, void *cookie))
	{
	if( !filename )
		pDefaultHandler=pHandler;
	else
		handler_map[filename]=pHandler;
	}

void ehttp::set_prerequest_handler( void (*pHandler)(ehttp &obj, void *cookie))
	{
		pPreRequestHandler=pHandler;
	}


int ehttp:: read_header( int fd, void *cookie, string &header, string &message )
	{

	header="";
	unsigned int offset=0;
	while((offset=header.find("\r\n\r\n"))==string::npos )
		{
		input_buffer[0]=0;
		int r=pRecv((void*)fd,&input_buffer[0],INPUT_BUFFER_SIZE,cookie);
		if( r < 0 )
			return EHTTP_ERR_GENERIC;
		input_buffer[r]=0;
		header+=input_buffer;
		}
	message=header.substr(offset+4);
	/* Fix the case where only "GET /xxxxx HTTP1.0\r\n\r\n" is sent (no other headers)*/
	if(offset == header.find("\r\n"))
		header=header.substr(0,offset+2);
	else
		header=header.substr(0,offset);
	dprintf("Header:-->%s<--\r\n",header.c_str());
	dprintf("Message:-->%s<--\r\n",message.c_str());
	return EHTTP_ERR_OK;
	}


int ehttp:: parse_out_pairs( void *cookie, string &remainder, map <string, string> &parms )
	{
	string id;
	string value;
	int state=0;

	// run through the string and pick off the parms as we see them
	for(unsigned int i=0; i < remainder.length();)
		{
		switch( state )
			{
			case 0:
				id="";
				value="";
				state=1;
				break;

			case 1:
				switch( remainder[i] )
					{
					case '=':
						state=2;
						break;
					default:
						id+=remainder[i];
						break;
					}
				i++;
				break;

			case 2:
				switch( remainder[i] )
					{
					case '&':
						parms[id]=value;
						dprintf("Added %s to %s\r\n",id.c_str(),value.c_str());
						state=0;
						break;
					default:
						value+=remainder[i];
						break;
					}
				i++;
				break;
	
			}
		}
	// Add non-nil value to parm list
	if( state == 2 )
		parms[id]=value;

	return EHTTP_ERR_OK;
	}


int ehttp:: parse_header( void *cookie, string &header )
{
	char *request=NULL;
	char *request_end=NULL;
	const char *pHeader=header.c_str();

	filename="";							
	contentlength=0;
	requesttype=-1;

	// Find end of request URL
printf("pHeader == [%s]\r\n",pHeader);	
	request_end=strstr(pHeader," HTTP/1.0\r\n");
	if( !request_end )
			request_end=strstr(pHeader," HTTP/1.1\r\n");
	if( !request_end )
		{
		dprintf("ERROR %d:%s\r\n",__LINE__,__FUNCTION__);
		return EHTTP_ERR_GENERIC;
		}

	// Is this a GET request
	if( requesttype == -1 )
		request=strstr(pHeader,"GET ");
	if( request )
		{
		request+=4;
		requesttype=EHTTP_REQUEST_GET;
		}

	// Is this a POST request
	if( requesttype == -1 )
		{
		request=strstr(pHeader,"POST ");
		if( request )
			{
			request+=5;
			requesttype=EHTTP_REQUEST_POST;
			}
		}

	// didn't find a get,post,etc...
	if( requesttype == -1 )
		{
		dprintf("ERROR %d:%s\r\n",__LINE__,__FUNCTION__);
		return EHTTP_ERR_GENERIC;
		}

	// malformed request
	if( request_end <= request )
		{
		dprintf("ERROR %d:%s\r\n",__LINE__,__FUNCTION__);
		return EHTTP_ERR_GENERIC;
		}

	// get the url requested
	while( request != request_end )
		{
		filename+=*request++;
		}

	// move to end of request
	request_end+=11;// length of " HTTP/1.1\r\n"
	
	// Save the complete URL
	url=filename;

	// See if there are params passed on the request
	int idx=filename.find("?");
	if( idx > -1 )
		{
		// Yank out filename minus parms which follow
		string remainder=filename.substr(idx+1);
		filename=filename.substr(0,idx);
		parse_out_pairs(cookie, remainder, url_parms);
		}

	// Find request headers,
	while( *request_end!='\r' && *request_end!='\0' )
		{
		char *keyend=strstr(request_end,": ");

		// get the key
		if( keyend && keyend>request_end )
			{
			string key,value;
			key="";
			value="";
			while( keyend > request_end)
				key+=(*request_end++);

			//get the value of the key
			request_end=keyend+2;
			char *valueend=strstr(request_end,"\r\n");

			//are we at end of header section
			if(!valueend)
				valueend=request_end+strlen(request_end);
			// read in the value
			if( valueend )
				{
				while( request_end < valueend )
					value+=*request_end++;
				//add key value pair to map
				request_header[key]=value;
				dprintf("Header : ...%s... == ...%s...\r\n",key.c_str(),value.c_str());
				if( *request_end )
					request_end+=2;
				}
			}
		else
			{
			//Somthing went wrong
			dprintf("ERROR %d:%s\r\n",__LINE__,__FUNCTION__);
			return EHTTP_ERR_GENERIC;
				}
		}
			
	// Find content length
	contentlength=atoi( request_header["CONTENT-LENGTH"].c_str() );
	return EHTTP_ERR_OK;		
	

}

int ehttp:: parse_message( int fd, void *cookie, string &message )
	{
	if( !contentlength ) return EHTTP_ERR_OK;

	dprintf("Parsed content length:%d\r\n",contentlength);
	dprintf("Actual message length read in:%d\r\n", message.length());

	// We got more than reported,so now what, truncate? 
	// We can normally get +2 more than the reported lenght because of CRLF
	// So we'll hit this case often
	// ...
	// there is a possible strange bug here, need to really validate before truncate
	if( contentlength < message.length() )
		message=message.substr(0,contentlength );
	// Need to read more in
	else if( contentlength > message.length() )
		{
		dprintf("READ MORE MESSAGE...\r\n");
		while( contentlength > message.length() )
			{
			input_buffer[0]=0;
			int r=pRecv((void*)fd,&input_buffer[0],INPUT_BUFFER_SIZE,cookie);
			if( r < 0 )
				return EHTTP_ERR_GENERIC;
			message+=input_buffer;
			}
		// Just in case we read too much...
		if( contentlength < message.length() )
			message=message.substr(0,contentlength );

		}

	// Got here, good, we got the entire reported msg length
	dprintf("Entire message is <%s>\r\n",message.c_str());
	parse_out_pairs(cookie, message, post_parms);
	

	return EHTTP_ERR_OK;
	}


int ehttp:: parse_request( int fd, void *cookie )
	{
	int (*pHandler)(ehttp &obj, void *cookie)=NULL;

	/* Things in the object which must be reset for each request */
	filename="";
	localFD=fd;
	ptheCookie=cookie;
	filetype=EHTTP_TEXT_FILE;
	url_parms.clear();
	post_parms.clear();
	request_header.clear();
	replace_token.clear();
	contentlength=0;

	string header;
	string message;

	if( read_header(fd,cookie, header, message) == EHTTP_ERR_OK )
		if( parse_header(cookie,header)  == EHTTP_ERR_OK )
			if( parse_message(fd,cookie, message)  == EHTTP_ERR_OK )
				{

				// We are HTTP1.0 and need the content len to be valid
				// Opera Broswer
				if( contentlength==0 && requesttype==EHTTP_REQUEST_POST )
					{
					return out_commit(EHTTP_LENGTH_REQUIRED);
					}

				
				//Call the default handler if we didn't get the filename
				out_buffer_clear();

				if( pPreRequestHandler ) pPreRequestHandler( *this, ptheCookie );
				if( !filename.length() )
					{
					dprintf("%d Call default handler no filename \r\n",__LINE__);
					return pDefaultHandler( *this, ptheCookie );
					}
				else
					{
					//Lookup the handler function fo this filename
					pHandler=handler_map[filename];
					//Call the default handler if we didn't get the filename
					if( !pHandler )
						{
						dprintf("%d Call default handler\r\n",__LINE__);
						
						return pDefaultHandler( *this, ptheCookie );
						}
					//Found a handler
					else
						{
						dprintf("%d Call user handler\r\n",__LINE__);
						return pHandler( *this, ptheCookie );
						}
					}
				}
	dprintf("Error parsing request\r\n");
	return EHTTP_ERR_GENERIC;
	}


void ehttp::setSendFunc( ssize_t (*pS)(void *fd, const void *buf, size_t len, void *cookie) )
	{
	if( pS )
		pSend=pS;
	else
		pSend=ehttpSend;
	}

void ehttp::setRecvFunc( ssize_t (*pR)(void *fd, void *buf, size_t len, void *cookie) )
	{
	if( pR )
		pRecv=pR;
	else
		pRecv=ehttpRecv;
	}




map <string, string> & ehttp::getResponseHeader( void )
	{
	return response_header;
	}








