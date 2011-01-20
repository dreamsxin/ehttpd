/********************************************************************

	Connection Helper Class for Example Programs for:

	EhttpD - EasyHTTP Server/Parser C++ Class

	http://www.littletux.com

	Copyright (c) 2007, Barry Sprajc


	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS".  USE AT YOUR OWN RISK.

********************************************************************/
#include "./connection.h"

Connection::Connection(int debugprint)
	{
	sd=connect_d=-1;
	debug=debugprint;
	}

Connection::~Connection()
	{
	}

void Connection::terminate( void )
	{
	if( sd!=-1)
		{
		close(sd);
		sd=-1;
		}
	}

int Connection::connect( char *ipAddress, unsigned short port )
{
	// Get my hostname.
	if (gethostname(myhostname, BUFFER_SIZE) != 0)
		{
		if(debug )printf("HTTPD:failed on gethostname(...) \r\n");
		return -1;
		}


	// lookup up our network number.
	if ( (hp = gethostbyname(myhostname)) == NULL)
		{
		if(debug )printf("HTTPD:Can't gethostbyname %s\r\n", myhostname);
		return -1;
		}

	// Try to get a network socket.
	if ( (sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
		if(debug )printf("HTTPD:Can't create a socket\r\n");
		return -1;
		}

	struct hostent *pH=gethostbyname(ipAddress);
	if(debug && pH==NULL)
		{
		printf("HTTPD:Can't gethostbyname %s\r\n", myhostname);
		return -1;
		}
	


	// Create address we will bind to.
	bzero(&sin,0);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	bcopy(pH->h_addr, &sin.sin_addr, pH->h_length);
	int er=::connect(sd,(const sockaddr*)&sin,sizeof(sin));
	if( !er )
		return sd;
	return er;
}


int Connection::init( unsigned short port )
	{
	// Get my hostname.
	if (gethostname(myhostname, BUFFER_SIZE) != 0)
		{
		if(debug )printf("HTTPD:failed on gethostname(...) \r\n");
		return -1;
		}


	// lookup up our network number.
	if ( (hp = gethostbyname(myhostname)) == NULL)
		{
		if(debug )printf("HTTPD:Can't gethostbyname %s\r\n", myhostname);
		return -1;
		}

	// Try to get a network socket.
	if ( (sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
		if(debug )printf("HTTPD:Can't create a socket\r\n");
		return -1;
		}


	// Create address we will bind to.
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);

   // Bind to the port
	int bnd=120;
	while( bnd>0 )
		{
		if (bind(sd, (const struct sockaddr *) &sin, sizeof(sin)) < 0)
			{
			if(debug )printf("HTTPD:Can't bind....retrying.\r\n");
			sleep(1);
			bnd--;
			}
		else bnd=0;
		}

	if( bnd<0 )
		{
		if(debug )printf("HTTPD:Bind giving up\r\n");
		return -1;
		}
	
    if(debug )printf("HTTPD:Bind OK\r\n");
 
	
	// Listen for the connection.
	if (listen(sd, 20) < 0)
		{
		if(debug )printf("HTTPD:Can't listen to port\r\n");
		::close(sd);
		return -1;
		}
	return 0;
	}


int Connection::accept( void )
	{

	int sock;
	static int linger[2]={0,0};
	//Accept connection.
	fromlen = sizeof(struct sockaddr);
	if ( (sock = ::accept(sd, (struct sockaddr *) &fsin, (socklen_t*)&fromlen)) < 0)
		{
		if(debug )printf("HTTPD:Can't accept connection....\r\n");
		::close(sd);
		sd=-1;
		return -1;
		}
	setsockopt(sock,SOL_SOCKET,SO_LINGER,&linger,sizeof(linger));
	return sock;
	}

void Connection::close( int sock )
	{
	if(debug )printf("Clsing\r\n");
	::close(sock);
	if(debug )printf("Done Clsing\r\n");
	}



