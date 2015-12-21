/*
 * visca_controller.c
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
 * along with this program; if not, write toS the Free Software
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

//最多支持 1（控制端） + 7（虚拟相机）= 8 个设备
#define MAX_DEVICES_COUNT (8)

int g_ptm[MAX_DEVICES_COUNT]; // pty master
int g_pts[MAX_DEVICES_COUNT]; // pty slave

// 在controller端可以通过stdin输入的控制命令列表，debug测试时使用
static const char SUPPORT_CMD[5][16] = { // user defined string command
		"1",
		"2",
		"3",
		"4",
		"5"
	};


static inline void err_handler(const char * title)
{
	perror(title);
	exit(1);
}

static int init_controller(int camera_count);
static int run();

void test();

int main(int argc, char **argv)
{
	int rc;
	int camera_count = MAX_DEVICES_COUNT - 1;
	if(argc > 1) camera_count = atol(argv[1]);
	
	//~ test();
	//~ return 0;
	
	// 初始化控制器
	int fdm = init_controller(camera_count);
	if(fdm <= 0) return 1;
	
	// 侦听客户端命令
	rc = run();
	
	return rc;
}


static int init_controller(int camera_count)
{
	int rc;
	int i;
	int fdm;
	char * pts_name;
	struct termios options;
	
	camera_count &= 0x07; // camera_count should equal or less then 7 
	
	// 初始化控制器端的8个虚拟串口master端，模拟主控制器
	for(i = 0; i < MAX_DEVICES_COUNT; i++)
	{
		fdm = posix_openpt(O_RDWR);
		if(fdm < 0) err_handler("posix_openpt");
		
		rc = grantpt(fdm);
		if(rc) err_handler("grantpt");
		
		rc = unlockpt(fdm);
		if(rc) err_handler("unlockpt");
		
		g_ptm[i] = fdm;
		
		//~ if(i)
		{
			// 设置串口数据模式为raw, 速率为9600
			tcgetattr(fdm, &options);
			cfmakeraw(&options);		
			cfsetispeed(&options, B9600);
			cfsetospeed(&options, B9600);
			
			//~ options.c_cc[VMIN] = 0;
			//~ options.c_cc[VTIME] = 1;
			
			tcsetattr(fdm, TCSANOW, &options);
		}
		
		//~ 
		// 打开7个虚拟串口slave端，模拟相机设备
		// 不打开g_ptm[0]对应的从端，留给客户端来控制		
		if(i && (i < (camera_count + 1)))
		{
			pts_name = ptsname(fdm);
			g_pts[i] = open(pts_name, O_RDWR | O_NDELAY | O_NOCTTY);
			if(g_pts[i] < 0) err_handler("open pts");
		}
	}
	
	pts_name = ptsname(g_ptm[0]);
	g_pts[0] = STDOUT_FILENO;
	
	// 清空当前未处理的数据
	tcflush(g_ptm[0], TCIOFLUSH);
	
	
	// 显示当前控制器所在的端口，
	// 客户端需要使用这个端口才能连接上控制器。
	printf("visca controller is available on '%s'\n", pts_name);
	return g_ptm[0];
}


static void * controller_thread(void * param);
static void * camera_thread(void * param);
static int controller_proc(int id, const unsigned char * packet, size_t len);
static int camera_proc(int address, const unsigned char *packet, size_t len);
static int stdin_proc(const char * cmd, size_t len);


static int run()
{
	pthread_t th;
	long quit = 0;
	long rc;
	void * exit_code = NULL;
	
	// 创建1个控制器线程
	// 在此示例中，不创建这一线程也可以，该线程的目的只是为了模拟出一个虚拟设备
	rc = pthread_create(&th, NULL, controller_thread, &quit);  // controller 	
	if(rc) err_handler("pthread_create");
	
	// do sth.
	// 但是在此示例中，没有事可做（所以此示例创建控制器线程是多余的）
	
	
	// 等待控制器线程结束
	pthread_join(th, &exit_code);
	rc = (long)exit_code;
	
	printf("exit with code %ld\n", rc);
	
	return rc;
}

static void * controller_thread(void * param)
{
	long * quit = (long *)param;
	assert(NULL != quit);
	int rc;
	int i;
	
	pthread_t tcam[MAX_DEVICES_COUNT - 1];
	void * exit_code = NULL;
	
	// 创建7个线程来模拟7个相机设备
	for(i = 0; i < (MAX_DEVICES_COUNT - 1); ++i)
	{
		pthread_create(&tcam[i], NULL, camera_thread, (void *)(long)(i + 1));
		// 如果涉及到争抢资源，就usleep
		//~ usleep(100000);
	}
	
	
	// 用poll来侦听设备的读取
	struct pollfd pfd[MAX_DEVICES_COUNT + 1]; // include stdin
	
	pfd[MAX_DEVICES_COUNT].fd = STDIN_FILENO; // 侦听stdin
	pfd[MAX_DEVICES_COUNT].events = POLLIN;
	
	unsigned char input[4096];
	
	// debug时，自己打开主控制器对应的从端
	//~ g_pts[0] = (open(ptsname(g_ptm[0]), O_RDWR));
	
	for(i = 0; i < MAX_DEVICES_COUNT; ++i)
	{
		pfd[i].fd = g_ptm[i]; // 侦听来自对应虚拟相机设备的消息
		pfd[i].events = POLLIN;
	}
	
	
	while(!(*quit))
	{
		// 判断一下虚拟相机设备是否已经关闭
		for(i = 0; i < MAX_DEVICES_COUNT; ++i)
		{			
			if(g_pts[i] > 0)
				pfd[i].fd = g_ptm[i];
			else
				pfd[i].fd = -1;
		}
		
		// 设置超时时间为1000ms
		rc = poll(pfd, MAX_DEVICES_COUNT + 1, 1000);
		if(rc < 0) err_handler("poll");
		if(0 == rc)
		{
			// timeout
			continue;
		}
		
		for(i = 0; i < MAX_DEVICES_COUNT + 1; i++)
		{
			if(pfd[i].fd < 0)
			{
				printf("unavailable device %d\n", i);
				continue;
			}
			
			if(pfd[i].revents & POLLIN)
			{
				printf("message ready on [%d]\n", i);
				rc = read(pfd[i].fd, input, sizeof(input) - 1);
				if(rc > 0)
				{
					//~ input[rc] = 0;
					if(i == MAX_DEVICES_COUNT) // stdin
					{
						// erase the tailing '\n'
						input[rc] = '\0';
						if(input[--rc] == '\n') 
						{
							input[rc] = '\0';
						}
						
						if(rc)
						{
							// 在控制端输入"quit"，可退出程序
							if((rc == 4) && strcmp((char *)input, "quit") == 0) 
							{
								printf("quit\n");
								*quit = 1;
								break;
							}
							
							// 处理来自stdin的命令
							stdin_proc((char *)input, rc);							
							//~ write(g_pts[0], input, rc);
							usleep(1);
						}
						
					}else if(i == 0) // 发送到主控制器的命令
					{
						printf("message reached to master[%d]: length = %d\n", i, rc);
						controller_proc(0, input, rc);						
					}else
					{								
						printf("notify from device [%d]: length = %d\n", i, rc);						
						controller_proc(i, input, rc);		
					}
				}else if(rc < 0)
				{
					fprintf(stderr, "read [%d] error.\n", i);
					exit(1);
				}
			}
			if(pfd[i].revents & POLLHUP)
			{
				// 如果client断开连接，对应的主控制端会一直出于POLLHUP状态
				// 没发现特别好的方法来处理这种情况，
				// 备选的方法可以考虑先从pollfd中移除该fd，
				// 然后另开一个线程，用inotify来监控对应的设备是否重新连接上，如果连接上，则重新侦听该fd
				// 此示例为了简化处理，只简单地使用usleep来忽略这一消息
				//~ fprintf(stderr, "[%d] hangup.\n", i);
				
				usleep(500000);
			}
			
		}
		
	}
	
	
	printf("wait camera shutdown...\n");
	memset(g_pts, -1, sizeof(g_pts));
	
	// 关闭所有主控设备
	for(i = 0; i < (MAX_DEVICES_COUNT); ++i)
	{
		close(g_ptm[i]);
		g_ptm[i] = -1;
	}
	
	// 等待虚拟相机线程安全退出
	for(i = 0; i < (MAX_DEVICES_COUNT - 1); ++i)
	{		
		pthread_join(tcam[i], &exit_code);
		printf("camera [%d] shutdown with code %ld.\n", i + 1, (long)exit_code);
	}
	
	printf("exit thread.\n");
	pthread_exit((void *)exit_code);
}

static void * camera_thread(void * param)
{
	long id = (long)param; // camera address
	
	// 相机设备号必须在【1，7】之间
	if(id < 1 || id > 7) pthread_exit((void *)(long)-1);
	int rc;
	volatile int fds;
	
	// 如果初始化主控制器时没有选择打开对应的相机端口，
	// 那么在相机线程中打开
	//~ char * pts_name = ptsname(g_ptm[id]);
	//~ fds = g_pts[id] = open(pts_name, O_RDWR|O_SYNC|O_NOCTTY);
	//~ printf("camera [%d] name: %s\n", (int)id, pts_name);
	
	fds = g_pts[id];	
	
	// 设置相机的虚拟串口参数
	struct termios options;
	
	tcgetattr(fds, &options);
	cfmakeraw(&options);		
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);
	
	// raw模式下，可以选择性设置下面两个参数：
	options.c_cc[VMIN] = 1;		// 只要收到1字节，就可以开始读取
	options.c_cc[VTIME] = 0;	// 可以立即读取，无须等待
	
	tcsetattr(fds, TCSANOW, &options);
	
	
	unsigned char input[4096];
	
	
	struct pollfd pfd[1];	
	
	while((fds = g_pts[id]) > 0)
	{
		pfd[0].fd = fds;
		pfd[0].events = POLLIN | POLLHUP;
		
		if(fds < 0) break;
		rc = poll(pfd, 1, 1000);
		if(rc <= 0) 
		{
			if(rc == 0) // timeout 
			{
				usleep(500000);
				continue;
			}
			err_handler("select");
		}
		
		if(pfd[0].revents & POLLIN)
		{
			rc = read(fds, input, sizeof(input));
			if(rc > 0)
			{
				camera_proc((long)id, input, rc);
			
			}else if(rc < 0)
			{
				// hangup
				fprintf(stderr, "slave [%d] hangup...\n", (int)id);
				pthread_exit((void *)id);
			}
		}else if(pfd[0].revents & POLLHUP)
		{
			close(fds);
			break;
		}
		
	}
	printf("camera [%d] shutdown successfully.\n", (int)id);
	pthread_exit((void *)0);
}

static int controller_proc(int id, const unsigned char * data, size_t length)
{	
	visca_packet_t packet;
	
	struct pollfd pfd[1];
	
	pfd[0].events = POLLOUT;
	int rc;
	int dst_device = 0;
	if(id < 0 || id >= MAX_DEVICES_COUNT) 
	{
		fprintf(stderr, "invalid device id\n");
		return 1;
	}
	
	if(id == 0) // came from control client
	{	
		static visca_buffer_t vbuf = {{0}};	
		
		// 缓冲区中可能包含了多个命令，先写入到visca_buffer_t中，
		// (visca_buffer_t是一个循环数组，最多存储4096字节的命令)
		// 用visca_buffer_get_packet来逐个读取，并做相应的处理
		// visca_buffer_get_packet函数在"visca.h"中定义
		
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
			// parse command and notify dst camera device
			
			// 此示例中，主控制器不解析任何命令，直接将命令转给#1号相机
			dst_device = 1;
			
			pfd[0].fd = g_ptm[dst_device];
			// 检测#1号相机端口是否可以写入，超时时间设为1000ms
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
				rc = write(pfd[0].fd, packet.data, packet.length);
				
				//~ read(pfd[0].fd, packet.data, rc);
			}
			else if(pfd[0].revents & POLLHUP)
			{
				fprintf(stderr, "peer device hungup.\n");
				usleep(100000);			
			}
		}
	}else // came from camera device
	{
		static visca_buffer_t vbuf = {{0}};	
		if(data && length > 0)
		{
			rc = visca_buffer_append(&vbuf, data, length);
			if(rc != VISCA_SUCCESS)
			{
				fprintf(stderr, "visca_buffer_append failed with errcode = %d\n", rc);
				return 1;
			}
		}
		
		printf("reveive msg from device: %d\n", id);
		
		while(visca_buffer_get_packet(&vbuf, &packet) == VISCA_SUCCESS)
		{
			// parse command and notify dst camera device
			
			// 受到对应设备返回的消息后，不做任何处理，直接写回主控制器0
			dst_device = 0;
			
			visca_packet_dump2(STDOUT_FILENO, &packet);
			
			pfd[0].fd = g_ptm[dst_device];
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
				printf("receive camera %d response: %d bytes.\n", id, (int)packet.length);
				
				write(pfd[0].fd, packet.data, packet.length);
				//~ write(pfd[0].fd, packet.data, packet.length);
			}
			else if(pfd[0].revents & POLLHUP)
			{
				fprintf(stderr, "peer device hungup.\n");
				usleep(100000);	
			}
		}
		
	}
	
	return 0;
}

static int camera_proc(int address, const unsigned char * data, size_t length)
{
	static visca_buffer_t vbuf = {{0}};	
	visca_packet_t packet;
	
	int id = address;
	if(address < 0 || address >= MAX_DEVICES_COUNT) return -1;
	
	struct pollfd pfd[1];
	pfd[0].fd = g_pts[id];	// 对应的串口设备从端
	pfd[0].events = POLLOUT;
	int rc;
	
	// 此示例中不对任何命令进行解析，
	// 如果收到消息，只是简单地将数据原封不动地传回主控制端。
	
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

// 测试 "visca.h"中的功能
//~ void test()
//~ {
	//~ visca_packet_t packet;
	//~ visca_packet_init(&packet);
	//~ unsigned char data[16] = {0x88, 0x01, 0x00, 0x01, 0xFF};
	//~ size_t length;
	//~ int rc;
	//~ 
	//~ rc = visca_packet_add_bytes(&packet, data, 16);
	//~ visca_packet_dump2(STDOUT_FILENO, &packet);
	//~ 
	//~ data[0] = 0;
	//~ data[1] = VISCA_POWER_ON;
	//~ length = 2;
	//~ 
	//~ rc = visca_packet_construct(&packet, 1, VISCA_COMMAND, VISCA_CATEGORY_MODE, data, length);
	//~ if(rc == VISCA_SUCCESS)
	//~ {
		//~ visca_packet_dump2(STDOUT_FILENO, &packet);
	//~ }
	//~ 
	//~ 
//~ }

// 处理来自标准输入(stdin)中的命令
static int stdin_proc(const char * cmd, size_t len)
{
	int i;
	int max_cmds = (sizeof(SUPPORT_CMD) / sizeof(SUPPORT_CMD[0]));
	visca_packet_t packet;
	visca_packet_init(&packet);
	
	unsigned char data[2];
	size_t data_len;
	
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
		default:
			fprintf(stderr, "unsupported command (%s)\n", cmd);
			return -1;
	}
	
	visca_packet_construct(&packet, 1, VISCA_INQUIRY, VISCA_CATEGORY_MODE, data, data_len);
	
	if(g_pts[0] == STDOUT_FILENO)
	{
		// 如果不是debug模式（或没有在主程序中打开g_pts[0]端口），则直接处理
		controller_proc(0, packet.data, packet.length);
		return 0;
	}
	
	// send command to controller
	struct pollfd pfd[1];
	pfd[0].fd = g_ptm[0]; // 将数据转发给主控制器
	pfd[0].events = POLLOUT;
	
	int rc;
	
	
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
	else if(pfd->revents & POLLHUP)
	{
		fprintf(stderr, "controller hungup.\n");
		return -1;
	}
	return 0;
}
