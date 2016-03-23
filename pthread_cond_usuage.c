/*
 * test3.c
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
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define N 6

/* ************************
 * 在使用条件变量时，尽可能地创建一个单独的互斥锁，将其仅用于同步条件变量cond，
 * 假使为了省事，将该mutex用在了其他地方，如果实现的逻辑有出了差错，可能会出现不可预知的结果
 * 为此，下面自定义了一个结构体，将cond和mutex强行捆绑在一起，避免误用
 * */
typedef struct cond_mutex
{
	pthread_cond_t c;
	pthread_mutex_t m;
}cond_mutex_t;

/* ************************
 * 初始化 互斥锁 和 条件变量 
 * */
cond_mutex_t cm = 
{
	PTHREAD_COND_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};
 
 
static void * wait_thread(void * param);   // 等待线程
static void * worker_thread(void * param); // 工作者线程

// 用一个mutex来同步对count的操作
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int count = 0;
static int data[N];

unsigned int seed;
volatile int quit = 0; // notify threads to quit

int main(int argc, char ** argv)
{
	int rc;
	int i;
#define NUM_WORKERS (4)
	pthread_t th[NUM_WORKERS + 1]; // 1个等待线程、NUM_WORKERS个工作者线程
	seed = time(NULL);
	
	void * ret_code = NULL;
	
	// 创建等待线程
	rc = pthread_create(&th[0], NULL, wait_thread, NULL);
	if(0 != rc)
	{
		perror("pthread_create");
		exit(1);
	}
	usleep(5000); // 5 ms
	
	// 创建工作者线程
	for(i = 1; i <= NUM_WORKERS; ++i)
	{
		rc = pthread_create(&th[i], NULL, worker_thread, NULL);
		if(0 != rc)
		{
			perror("pthread_create");
			exit(1);
		}
	}
		
	char c = 0;
	printf("press enter to quit.\n");
	while(1)
	{
		scanf("%c", &c);
		if(c == '\n') break;
	}
	
	quit = 1;
	pthread_cond_broadcast(&cm.c); // 激活在所有线程中正在等待中的条件变量
	
	pthread_join(th[0], &ret_code);
	for(i = 1; i <= NUM_WORKERS; ++i)
	{
		pthread_join(th[i], &ret_code);
	}
	
	
	rc = (int)(long)ret_code;
	
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cm.c);
	pthread_mutex_destroy(&cm.m);
	return rc;	
}

static void * worker_thread(void * param)
{
	while(!quit)
	{	
		pthread_mutex_lock(&mutex); // 先加锁之后，再修改data和count值
		
		if(quit) // 有可能在锁定等待期间被用户强行退出
		{
			pthread_mutex_unlock(&mutex);
			break;
		}
		if(count == N) //某一个【工作者线程】已经触发了条件变量，但【等待线程】还没来得及加锁
		{	
			printf("正在与【等待线程】争抢资源...\n");		
			pthread_mutex_unlock(&mutex);			
			usleep(10000); // sleep 10 ms, 给其他线程一个机会
			continue;
		}
	//~ #ifdef _DEBUG
		if(count > N) // 应该不可能发生，仅供调试时使用
		{
			fprintf(stderr, "同步的逻辑出现问题，请检查源代码。\n");
			printf("请按回车建退出...\n");
			quit = 1;
			pthread_mutex_unlock(&mutex);
			break;
		}
	//~ #endif
	
		data[count++] = rand_r(&seed) % 1000;	
		
		if(count == N)
		{
			pthread_mutex_lock(&cm.m);			
			pthread_cond_signal(&cm.c);
			pthread_mutex_unlock(&cm.m);			
		}
		pthread_mutex_unlock(&mutex); // 解锁
		usleep(100000); // 100 ms; 人为故意地延迟一下，模拟一下真实场景可能需要的工作量。
	}
	pthread_exit((void *)(long)0);
}

static void * wait_thread(void * param)
{
	int i, sum;
	pthread_mutex_lock(&cm.m);	
	while(!quit)
	{
		pthread_cond_wait(&cm.c, &cm.m);
		if(quit) break; //有可能是在等待期间被用户干预，强制退出的（在while中判断时quit可能还是0）
		
		pthread_mutex_lock(&mutex); // 先上锁，避免其他线程修改 data 和 count 值
		sum = 0;
		printf("average = (");
		for(i = 0; i < N; ++i)
		{
			sum += data[i];
			printf(" %3d ", data[i]);
			if(i < (N - 1)) printf("+");
		}
		
		
		printf(") / %d = %.2f\n", N, (double)sum / (double)N);
		count = 0;
		pthread_mutex_unlock(&mutex);
		
	} // end while
	
	pthread_mutex_unlock(&cm.m);
	
	pthread_exit((void *)(long)0);
}
