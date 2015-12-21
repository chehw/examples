/*
 * client.c
 * 
 * Copyright 2015 Che Hongwei <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <string.h>
#include <assert.h>

#include <poll.h>

#ifndef __USE_BSD
#define __USE_BSD
#endif
#include <termios.h>

#include "visca.h"

#define MAX_DEVICES_COUNT (8)


static const char SUPPORT_CMD[6][16] = { // user defined string command
		"1",
		"2",
		"3",
		"4",
		"5",
		"6" // quit
	};

static inline void err_handler(const char * title)
{
	perror(title);
	exit(1);
}

int g_pts[1];



static int parse_command(const char * cmd, size_t length);
static int parse_message(int fds, const unsigned char * data, size_t length);

int main(int argc, char **argv)
{
	int rc;	
	int fds;
	
	const char * pts_name = "/dev/pts/3";
	if(argc > 1) pts_name = argv[1];
	
	fds = open(pts_name, O_RDWR);
	if(fds < 0) 
	{
		perror("open");
		exit(1);
	}
	
	struct termios options;
	
	
	
	tcgetattr(fds, &options);
	cfmakeraw(&options);		
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);
	
	options.c_cc[VMIN] = 1;
	options.c_cc[VTIME] = 0;
	
	tcsetattr(fds, TCSANOW, &options);
	
	// 清空当前未处理的数据
	tcflush(fds, TCIOFLUSH);
	
	g_pts[0] = fds;
	
	struct pollfd pfd[2];
	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	pfd[1].fd = fds;
	pfd[1].events = POLLIN;
	
	unsigned char input[4096];
	
	while(1)
	{
		//~ printf("\nplease input command (1~5, 6 == quit): ");
		rc = poll(pfd, 2, 3000);
		if(rc < 0)
		{
			if(EAGAIN != errno)
			{
				perror("poll");
				exit(1);
			}
			continue;
		}
		
		if(pfd[0].revents & POLLIN) // receive message from stdin
		{
			rc = read(pfd[0].fd, input, sizeof(input));
			if(rc > 0)
			{
				input[--rc] = '\0'; // remove the tailing '\n'				 
			}else if(rc < 0)
			{
				perror("read stdin");
				break;
			}
			if(rc)
			{
				rc = parse_command((char *)input, rc);
				if(1 == rc) 
				{
					printf("quit...\n");
					break;
				}
			}
		}
		
		if(pfd[1].revents & POLLIN)
		{
			rc = read(pfd[1].fd, input, sizeof(input));
			if(rc > 0)
			{
				parse_message(pfd[1].fd, input, rc);
			}else if(rc < 0)
			{
				perror("read stdin");
				break;
			}
		}
		
		if(pfd[1].revents & POLLHUP)
		{
			fprintf(stderr, "controller device hungup.\n");
			break;
			//~ usleep(500000);
		}
		
	}
	return 0;
}

static int parse_command(const char * cmd, size_t length)
{
	int i;
	int rc;
	int max_cmds = (sizeof(SUPPORT_CMD) / sizeof(SUPPORT_CMD[0]));
	visca_packet_t packet;
	visca_packet_init(&packet);
	
	unsigned char data[2];
	size_t data_len;
	
	if(length == 4)
	{
		if((strcasecmp(cmd, "quit") == 0) || (strcasecmp(cmd, "exit") == 0))
		{
			return 1;
		}
	}
	
	for(i = 0; i < max_cmds; ++i)
	{
		if(strcasecmp(cmd, SUPPORT_CMD[i]) == 0) break;
	}
	
	switch(i)
	{
		case 0: // power on
			data[0] = VISCA_POWER;
			data[1] = VISCA_POWER_ON;
			data_len = 2;
			break;
		case 1: // power off
			data[0] = VISCA_POWER;
			data[1] = VISCA_POWER_OFF;
			data_len = 2;
			break;
		case 2: // power query
			data[0] = VISCA_POWER;
			data_len = 1;
			break;
		case 3:	// record
			data[0] = VISCA_CONTROL;
			data[1] = VISCA_CONTROL_RECORD;
			data_len = 2;
			break;
		case 4: // record pause
			data[0] = VISCA_CONTROL;
			data[1] = VISCA_CONTROL_RECORD_PAUSE;
			data_len = 2;
			break;
		case 5:
			return 1;
		default:
			
			fprintf(stderr, "unsupported command (%s)\n", cmd);
			return -1;
	}
	
	visca_packet_construct(&packet, 1, VISCA_INQUIRY, VISCA_CATEGORY_MODE, data, data_len);
	
	// send command to controller
	struct pollfd pfd[1];
	pfd[0].fd = g_pts[0];
	pfd[0].events = POLLOUT;
	
	
	
	
	// test whether or not the controller can write. (timeout = 1000 ms)
	rc = poll(pfd, 1, 1000);
	if(rc < 0)
	{
		if(EAGAIN == errno)
			fprintf(stderr, "timeout\n");
		else
			perror("poll");
		return -1;
	}
	if(pfd[0].revents & POLLOUT)
	{	
		write(pfd[0].fd, packet.data, packet.length);
	}
	else if(pfd[0].revents & POLLHUP)
	{
		fprintf(stderr, "controller hungup.\n");
		return -1;
	}
	return 0;
}

static int parse_message(int fds, const unsigned char * data, size_t length)
{
	printf("receive message length: %d\n", (int)length);
	static visca_buffer_t vbuf = {{0}};	
	visca_packet_t packet;
	
	
	struct pollfd pfd[1];
	pfd[0].fd = g_pts[0];
	pfd[0].events = POLLOUT;
	int rc;
	
	int reply = 0;
	
	if(data && length > 0)
	{
		rc = visca_buffer_append(&vbuf, data, length);
		if(rc != VISCA_SUCCESS)
		{
			fprintf(stderr, "visca_buffer_append failed with errcode = %d\n", rc);
			return 1;
		}
	}
	while(visca_buffer_get_packet(&vbuf, &packet) == VISCA_SUCCESS)
	{
		// parse packet
		visca_packet_dump2(STDOUT_FILENO, &packet);
		
		if(!reply) continue;
		
		
		rc = poll(pfd, 1, 1000);
		if(rc < 0)
		{
			if(EAGAIN == errno) fprintf(stderr, "timeout\n");
			else 
			{
				perror("poll");
				return 1;
			}
		}
		if(pfd[0].revents & POLLOUT)
		{
			write(pfd[0].fd, packet.data, packet.length);
		}
		else if(pfd[0].revents & POLLHUP)
		{
			fprintf(stderr, "peer device hungup.\n");
			usleep(100000);			
		}
	}
	
	return 0;
}
