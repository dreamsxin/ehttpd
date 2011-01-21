/********************************************************************
	Hello World Application for EhttpD 

	Simple example of how to use EasyHTTPD
	Step by Step


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


int handleDefault( ehttp &obj, void *cookie );

int main( int argc, char *argv[] )
{
	//---------------------------------------------------------------
	//  #1a 
	//	Instantiate an ehhtpd object, and call the  objects init() 
	//	method.
	//---------------------------------------------------------------
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


	// Listen on port 80 for requests
	Connection c(1);
	if( c.init(12345) )
		exit(1);


	// Process incomming requests
	int s=0;
	printf("Wait for request\r\n");
	while(s>-1)
		{
		printf("accept...\r\n");
		int s=c.accept();
		if( s >-1 )
			{
			printf("parse...\r\n");

	//---------------------------------------------------------------
	//	#3a
	//	For each socket accepted, call the objects parse_request() 
	//	method.  The second param	is a void* which is passed to the 
	//	request handler, and provides a link between the  request 
	//	handler and  your application.
	//---------------------------------------------------------------
			
			http.parse_request(s, NULL);
			printf("close...\r\n");
			c.close(s);
			}
		}

	// Close down our connection class
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
	obj.out_replace_token("MESSAGE","Hello World");

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
