/********************************************************************
	Hello World Application for EhttpD over SSL

	Simple example of how to use EasyHTTPD over SSL
	Step by Step

	PLEASE - This is not production code.  You've been warned.

	EhttpD - EasyHTTP Server/Parser C++ Class

	http://www.littletux.com

	Copyright (c) 2007, Barry Sprajc


	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS".  USE AT YOUR OWN RISK.

********************************************************************/
#include <time.h>
#include <stdio.h>	

#include "./embedhttp.h"		//The header file
#include "./connection.h"		//A helper class to listen and accept
								//incomming connections. You don't
								//have to use this class, you can use
								//your own method to get a socket

#include "ssl.h"				// -I/usr/include/openssl -I/usr/kerberos/include


ssize_t sslsend( void *ssl, const void *buf, size_t len, void *cookie );
ssize_t sslrecv( void *ssl, void *buf, size_t len, void *cookie );
int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata);
int handleDefault( ehttp &obj, void *cookie );

int main( int argc, char *argv[] )
{
	//---------------------------------------------------------------
	//  #1a 
	//	Instantiate an ehhtpd object, and call the  objects init() 
	//	method.
	//------i---------------------------------------------------------
	int err;
	ehttp	http;
	if( http.init() != EHTTP_ERR_OK )
		{
		printf("Can't initalize ehttpd\r\n");
		exit(1);
		}

	//---------------------------------------------------------------
	//	#2a
	//	Add at least one request handler to the object.
	//	Allways add a default handler.  The default 
	//	handler has the filename parameter == NULL.
	//---------------------------------------------------------------
	http.add_handler(NULL,handleDefault);


	//---------------------------------------------------------------
	// Initalize the SSL library and set the parser to use our
	// socket wrapper functions that call the SSL I/O functions
	//---------------------------------------------------------------
	http.setRecvFunc( sslrecv );
	http.setSendFunc( sslsend );
	SSL_library_init();
	SSL_load_error_strings();
	SSL_CTX *ctx=SSL_CTX_new(SSLv23_server_method());
	SSL_CTX_set_default_passwd_cb(ctx, &pem_passwd_cb);

	// Generate your own key and cert files...
	// 	http://sial.org/howto/openssl/csr/
	if ( SSL_CTX_use_certificate_file(ctx, "./domain.cert",SSL_FILETYPE_PEM)<0 )
		{
		printf("Can’t read cert file");
		}

	if(!(SSL_CTX_use_PrivateKey_file(ctx, "./domain.key",SSL_FILETYPE_PEM)))
		{
		printf("Can’t read key file");
		
		}
	SSL_CTX_set_verify_depth(ctx,1);



	// Listen on port 80 for requests
	Connection c(1);
	if( c.init(443) )
		{
		printf("port in use?\r\n");
		exit(1);
		}



	// Process incomming requests
	int s=0;
	printf("Wait for request\r\n");
	while(s>-1)
		{
		printf("accept...\r\n");
		int s=c.accept();
		if( s >-1 )
			{

	//---------------------------------------------------------------
	// Create new SSL from context
	//---------------------------------------------------------------
			SSL *ssl=SSL_new(ctx);

	//---------------------------------------------------------------
	// Set our accepted socket to be used
	// for SSL communication
	//---------------------------------------------------------------
			if( !SSL_set_fd(ssl,s) )
				{
				printf("set fd failed\r\n");
				}

	//---------------------------------------------------------------
	// SSL handshaking
	//---------------------------------------------------------------
			if( (err=SSL_accept(ssl)) < 0 )
				{
				printf("SSL Accept Error %d\r\n",SSL_get_error(ssl,err));
				if( (err=SSL_accept(ssl)) < 0 )
					{
					printf("SSL Accept Error %d\r\n",SSL_get_error(ssl,err));
					}
				else
					{
					printf("Got it this time\r\n");
					}
				}



	//---------------------------------------------------------------
	//	#3a
	//
	//	PARSE THE REQUEST
	//
	//	For each socket accepted, call the objects parse_request() 
	//	method.  The second param	is a void* which is passed to the 
	//	request handler, and provides a link between the  request 
	//	handler and  your application.
	//---------------------------------------------------------------
			http.parse_request((int)ssl, NULL);
			printf("Parse request complete\r\n");
	//---------------------------------------------------------------
	// SSL Shutdown
	//---------------------------------------------------------------
			switch( SSL_shutdown(ssl) )
				{
				case 1:
					// Shutdown complete
					printf("Shutdown complete\r\n");
					break;
#if 0					
				// According to OpenSSL, we only need to call shutdown
				// again if we are going to keep the TCP Connection
				// open.  We are not, we are going to close, and doing
				// another shutdown causes everyone to stall..
			
				case 0:
					// 1/2 of bi-dir connection shut down
					// do it again
					printf("Shutdown 1/2 complete\r\n");
					SSL_shutdown(ssl);
					break;
#endif					
				case -1:
					printf("Error shutting down connection %d",SSL_get_error(ssl,-1));
					break;

				default:
					printf("Unknown API result\r\n");
					break;
				}
	//---------------------------------------------------------------
	// Close the TCP socket
	//---------------------------------------------------------------
			c.close(s);
			}
		}

	// Close down our connection class
	// and SSL 
	SSL_CTX_free(ctx);
	c.terminate();

	return 0;
}

/*
	This is the default request handler.
	You must allways have a default request handler, to 
	handle those requests which are not supported.

	The default handler filename is NULL when you add_handler()
*/
int handleDefault( ehttp &obj, void *cookie )
{
	//---------------------------------------------------------------
	//	#1b
	//	Specify the name of the HTML template file to be used.
	//	The template file is just a HTML file, with ##TAGS## embedded
	//	and these tags get replace with something when out_replace
	//	is called
	//---------------------------------------------------------------
	obj.out_set_file("helloworld_template.html");

	//---------------------------------------------------------------
	// 	#2b
	//	This is where most of the work happens in the request 
	//	handler.
	//
	//	Create your dynamic content.  In this case, we simply replace
	//	the ##MESSAGE## tag in the template file with the "Hello 
	//	World" string.
	//
	//	At this point, you might check for get or post requests 
	//	and	pass commands to your app via the cookie, or get content
	//	(application status as one example) via the cookie.
	//---------------------------------------------------------------
	obj.out_replace_token("MESSAGE","Hello World from SSL Land");

	//---------------------------------------------------------------
	//	#3b
	//	Replace all the tags in the template file with tokens
	//---------------------------------------------------------------
	obj.out_replace();

	//---------------------------------------------------------------
	//	#4b
	//	Write the content to the socket
	//---------------------------------------------------------------
	return obj.out_commit();
}

//---------------------------------------------------------------
// Send HTTP data over an OpenSSL connection
//
// Our socket wrapper function so the class calls OpenSSL 
// I/O functions
//---------------------------------------------------------------
ssize_t sslsend( void *ssl, const void *buf, size_t len, void *cookie )
{
	// The OpenSSL IO Function
	ssize_t i =  SSL_write((SSL*)ssl,buf,len);
printf("Wrote %d of %d bytes\r\n",i,len);
	// Handle OpenSSL quirks
	if( i== 0)
		{
		switch( SSL_get_error((SSL*)ssl,i) )
			{
			case SSL_ERROR_ZERO_RETURN:
				// A 'real' nice end of data close connection
				break;
			case SSL_ERROR_WANT_WRITE:
				//Don't do this for production code
				return sslsend(ssl,buf,len,cookie);
			default:
				//The parser doesn't care what error, just that there is one
				//so -1 is OK
				return -1;
			}
		}
	return i;
}


//---------------------------------------------------------------
// Read HTTP data from an OpenSSL connection
//
// Our socket wrapper function so the class calls OpenSSL 
// I/O functions
//---------------------------------------------------------------
ssize_t sslrecv( void *ssl, void *buf, size_t len, void *cookie )
{
	// The OpenSSL IO Function
	ssize_t i=  SSL_read((SSL*)ssl,buf,len);

	// Handle Open SSL quirks
	if( i== 0)
		{
		switch( SSL_get_error((SSL*)ssl,i) )
			{
			case SSL_ERROR_ZERO_RETURN:
				// A 'real' nice end of data close connection
				break;
			case SSL_ERROR_WANT_READ:
				//Don't do this for production code
				return sslrecv(ssl,buf,len,cookie);
			default:
				//The parser doesn't care what error, just that there is one
				//so -1 is OK
				return -1;
			}
		}
	
	return i;
}


//---------------------------------------------------------------
// Return the password when requested from SSL
//
// This is the passphrase you entered when you generated your 
// certificate.
//---------------------------------------------------------------
int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	strcpy(buf,"barry");
	return 5;
}

