/*
 * echoclnt.c
 * 
 * Copyright 2016 Che Hongwei <htc.chehw@gmail.com>
 * 
 * The MIT License (MIT)
 * 
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, 
 * publish, distribute, sublicense, and/or sell copies of the Software, 
 * and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR 
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */


#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT "8031"
#define SERV_NAME "api.chehw.info"

static int client_run();

int main(int argc, char **argv)
{
	client_run();
	return 0;
}

static int client_run()
{
	int fd;
	//~ struct pollfd pfd;
	int rc;
	
	struct addrinfo hints, * serv_info, * p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	
	
	
	while(1)
	{
		rc = getaddrinfo(SERV_NAME, PORT, &hints, &serv_info);
		if(rc)
		{
			fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(rc));
			abort();
		}
		for(p = serv_info; NULL != p; p = p->ai_next)
		{
			fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if(fd < 0) continue;		
			
			rc = connect(fd, p->ai_addr, p->ai_addrlen);
			if(0 == rc) break;
			close(fd);
		}
		
		if(NULL == p)
		{
			perror("socket");
			freeaddrinfo(serv_info);
			exit(1);
		}
		
		char hbuf[NI_MAXHOST] = "";
		char sbuf[NI_MAXSERV] = "";
		
		rc = getnameinfo(p->ai_addr, p->ai_addrlen, 
			hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
		if(0 == rc)
		{
			printf("connected to %s:%s\n", hbuf, sbuf);
		}	
		freeaddrinfo(serv_info);
		
		write(fd, "hello", 5);
		
		close(fd);
		sleep(1);
	}
	
}
