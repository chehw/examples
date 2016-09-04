/*
 * echoserv.c
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
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>

#define PORT "8081"
#define MAX_EVENTS 64

static int serv_run();
static int on_recv(int fd);

static int chutil_make_non_blocking(int fd)
{
	int rc;
	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	if(-1 == flags)
	{
		perror("fcntl");
		return -1;
	}
	
	flags |= O_NONBLOCK;
	rc = fcntl(fd, F_SETFL, flags);
	if(-1 == rc)
	{
		perror("fcntl");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	serv_run();
	return 0;
}

static int serv_run()
{
	int rc;
	int efd;
	int sfd = -1;
	struct addrinfo hints, * serv_info, * p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	rc = getaddrinfo("127.0.0.1", PORT, &hints, &serv_info);
	if(rc)
	{
		fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(rc));
		exit(1);
	}
	for(p = serv_info; NULL != p; p = p->ai_next)
	{
		sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(-1 ==  sfd) continue;
		
		rc = bind(sfd, p->ai_addr, p->ai_addrlen);
		if(rc)
		{
			perror("bind");
			close(sfd);
			continue;
		}
		break;
	}
	
	if(NULL == p)
	{
		perror("getaddrinfo");
		freeaddrinfo(serv_info);
		close(sfd);
		exit(1);
	}
	
	char hbuf[NI_MAXHOST] = "", sbuf[NI_MAXSERV] = "";
	rc = getnameinfo(p->ai_addr, p->ai_addrlen, 
		hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
		NI_NUMERICHOST | NI_NUMERICSERV);
	if(0 == rc)
	{	
		printf("listening on %s:%s\n", hbuf, sbuf);
	}
	freeaddrinfo(serv_info);
	chutil_make_non_blocking(sfd);	
	rc = listen(sfd, SOMAXCONN);
	if(-1 == rc)
	{
		perror("listen");
		abort();
	}
	
	struct epoll_event events[1 + MAX_EVENTS];
	efd = epoll_create1(0);
	
	memset(events, 0, sizeof(events));
	events[MAX_EVENTS].data.fd = sfd;
	events[MAX_EVENTS].events = EPOLLIN | EPOLLET;
	
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &events[MAX_EVENTS]);
	do
	{
		int n, i;
		int fd;
		n = epoll_wait(efd, &events[0], MAX_EVENTS, -1);
		if(n <= 0)
		{
			perror("epoll_wait");
			break;
		}
		
		for(i = 0; i < n; ++i)
		{
			if(events[i].events & EPOLLERR ||
				events[i].events & EPOLLHUP ||
				events[i].events & EPOLLRDHUP ||
				!(events[i].events & EPOLLIN)
				)
			{
				perror("epoll_wait");
				close(events[i].data.fd);
				continue;
			}
			
			if(events[i].data.fd == sfd) // incomming connections
			{
				struct sockaddr_storage ss;
				socklen_t slen = 0;
				fd = accept(sfd, (struct sockaddr *)&ss, &slen); 
				if(-1 == fd)
				{
					if(errno == EAGAIN || errno == EWOULDBLOCK)
					{
						break;
					}else 
					{
						perror("accept");
						break;
					}
				}
				rc = getnameinfo((struct sockaddr *)&ss, slen, 
					hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
					NI_NUMERICHOST | NI_NUMERICSERV);
				if(0 == rc)
				{
					printf("connected from %s:%s\n", hbuf, sbuf);
				}
				chutil_make_non_blocking(fd);
				events[MAX_EVENTS].data.fd = fd;
				events[MAX_EVENTS].events = EPOLLIN | EPOLLET;
				rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &events[MAX_EVENTS]);
				if(rc)
				{
					perror("epoll_ctl");
					abort();
				}
				continue;
			}else
			{
				rc = on_recv(fd);
				continue;
			}
		}
		
	}while(1);
	
	
	close(sfd);
	return 0;
}


static int on_recv(int fd)
{
	int done = 0;
	ssize_t cb;
	char buf[4096];
	while(1)
	{
		cb = read(fd, buf, sizeof(buf));
		if(-1 == cb)
		{
			if(EAGAIN != errno)
			{
				perror("read");
				done = 1;
			}
			break;
		}else if(0 == cb) // remote close the connection
		{
			done = 1;
			break;
		}else
		{
			fprintf(stdout, "cb = %d\n", (int)cb);			
		}
	}
	if(done)
	{
		printf("close connection on [%d]\n", fd);
		close(fd);
	}
	return done;
}
