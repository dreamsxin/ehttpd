/********************************************************************
	Sample Application for EhttpD 

	EhttpD - EasyHTTP Server/Parser C++ Class

	http://www.littletux.com

	Copyright (c) 2007, Barry Sprajc


	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS".  USE AT YOUR OWN RISK.

********************************************************************/

#include <time.h>
#include <stdio.h>
#include "./connection.h"
#include "./embedhttp.h"

/*
This is the default request handler.
You must allways have a default request handler, to 
handle those requests which are not supported.


The default handler filename is NULL when you add_handler()
*/
int handleDefault( ehttp &obj, void *cookie )
{
	obj.out_write_str("Unable to handle the request for:");
	obj.out_write_str(obj.getURL());
	return obj.out_commit();
}

/*
This handler handles the following URLS:
	/
	/index.html

as specified by the add_handler() function in main()

The template file is "index_template.html" and contains the
following 'tags' in the file:
	##MESSAGE##
	##CURRENTTIME##
	##URL##
	##FILENAME##
	##CMD##
	##ACTION##

	and

	##RESULTS##

Each of these tags gets replaced with call to out_replace_token():

Example:
	obj.out_replace_token("MESSAGE","Hello World!");

	This replaced the string ##MESSAGE## with "Hello World".

The replacment take place when a call to out_replace() is called,
which should be called once right before out_commit()

This handler also checkes if the request is a post request, and if
so, generates some extra content, using the data supplied form.

	

*/
int handleIndex( ehttp &obj, void *cookie )
{
	//AppToGUI &app=CookieToApp(cookie);
	
	// Select the template file to use
	obj.out_set_file( "index_template.html");

	// Replace tags in the template with interesting stuff
	obj.out_replace_token("MESSAGE","Hello World!");
	time_t t;
	time(&t);
	obj.out_replace_token("CURRENTTIME", ctime(&t));
	obj.out_replace_token("URL",obj.getURL());
	obj.out_replace_token("FILENAME",obj.getFilename());
	obj.out_replace_token("CMD",obj.getUrlParam("CMD"));
	obj.out_replace_token("ACTION",obj.getUrlParam("ACTION"));


	// If this is a HTTP POST request, get the data from the
	// form controls, and generate some more intersesting 
	// text from them.

	// If this is not a post request, then ##RESULTS## gets replace
	// with an empty string.
	if( obj.isPostRequest() )
		{
		if( obj.getPostParam("mybutton")=="Echo")
			{
			obj.out_replace_token("RESULTS",string("You Pressed Echo: ")+
				obj.getPostParam("NAME"));
			}
		else if( obj.getPostParam("mybutton")=="Print+Length")
			{
			char buffer[50];
			sprintf(buffer,"String length is:<b>%d</b>",obj.getPostParam("NAME").length());
			obj.out_replace_token("RESULTS",string("You Pressed Print Length: ")+string(buffer));
			}

		obj.out_replace_token("FCOLOR",obj.getPostParam("Dropdown"));
		
		}
	
	// Cause the actual token replacement with the template file
	obj.out_replace();

	// Write out the text(HTML) to the socket, along with appropriate
	// HTTP response header
	obj.getResponseHeader()["Cache-Control"]="private";
	obj.getResponseHeader()["Content-Type"]="text/html; charset=UTF-8";
	obj.getResponseHeader()["Set-Cookie"]="BARRY=SPRAJC23232; path=/index.html; expires=Sun, 17-Jan-2038 19:14:07 GMT";
	
	return obj.out_commit();
}

int main( int argc, char *argv[] )
{

	ehttp	http;

	// Initalize EhttpD
	if( http.init() != EHTTP_ERR_OK )
		{
		printf("Can't initalize ehttpd\r\n");
		exit(1);
		}

	// Add requests handers to handle the following URL requests:
	//	/
	//	/index.html
	http.add_handler("/", handleIndex );
	http.add_handler("/index.html", handleIndex );

	
	// Allways add a default handler, to deal with the requests we
	// don't explictily handle.
	//
	// NULL means default handler
	http.add_handler(NULL,handleDefault);


	// Listen on port 80 for requests
	Connection c(1);
	if( c.init(80) )
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
			// Read and parse the HTTP request and
			// write the result to the browser/socket
			//
			http.parse_request(s, NULL);
			printf("close...\r\n");
			c.close(s);
			}
		}

	// Close down our connection class
	c.terminate();

	return 0;
}


