#ifndef _VISCA_H_
#define _VISCA_H_

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <poll.h>
#include <stdint.h>
#include <assert.h>


#define MAX_VISCA_PACKET_LEN (16)

typedef struct visca_packet
{
	unsigned char data[MAX_VISCA_PACKET_LEN];
	size_t length;
}visca_packet_t;

#define VISCA_TERMINATOR 0xFF
#define VISCA_SUCCESS (0)
#define VISCA_FAILURE (-1)

enum VISCA_COMMAND_TYPE
{
	VISCA_COMMAND = 0x01,
	VISCA_INQUIRY = 0x09,
	VISCA_SET_ADDRESS = 0x30
};

enum VISCA_RESPONSE_TYPE
{
	VISCA_RESPONSE_ACK =  0x04,
	VISCA_RESPONSE_COMPLETE = 0x50,
	VISCA_RESPONSE_ERROR = 0x60,
	VISCA_RESPONSE_NETWORK_CHANGE = 0x38
};

enum VISCA_ERROR
{
	VISCA_ERROR_MESSAGE_LENGTH = 0x01,
	VISCA_ERROR_SYNTAX,
	VISCA_ERROR_COMMAND_BUFFER,
	VISCA_ERROR_COMMAND_CANCEL,
	VISCA_ERROR_NO_SOCKET,
	VISCA_ERROR_POWER_OFF= 0x40,
	VISCA_ERROR_COMMAND_FAILED,
	VISCA_ERROR_SEARCH,
	VISCA_ERROR_CONDITION,
	VISCA_ERROR_COUNTER_TYPE = 0x46,
	VISCA_ERROR_TUNER,
	VISCA_ERROR_EMERGENCY_STOP,
	VISCA_ERROR_MEDIA_UNMOUNTED,
	VISCA_ERROR_REGISTER,
	VISCA_ERROR_REGISTER_MODE_SETTING
};



enum VISCA_CATEGORY
{
	VISCA_CATEGORY_SYSTEM = 0x00,
	VISCA_CATEGORY_ADDRESS = 0x01,
	VISCA_CATEGORY_MODE = 0x02
};

#define VISCA_POWER (0x00)
#define VISCA_POWER_ON (0x02)
#define VISCA_POWER_OFF (0x03)
#define VISCA_CONTROL (0x01)

enum 
{
	VISCA_CONTROL_STOP = 0x00,
	VISCA_CONTROL_FORWARD = 0x08,
	VISCA_CONTROL_REWIND = 0x10,
	VISCA_CONTROL_EJECT = 0x18,
	VISCA_CONTROL_STILL = 0x20,
	VISCA_CONTROL_SLOW_10 = 0x24,
	VISCA_CONTROL_SLOW_5 = 0x26,
	VISCA_CONTROL_PLAY = 0x28,
	//...
	VISCA_CONTROL_RECORD_PAUSE = 0x40,
	VISCA_CONTROL_RECORD = 0x48,
};

#define MAX_VISCA_BUFFER_LEN (4096)
typedef struct visca_buffer
{
	unsigned char data[MAX_VISCA_BUFFER_LEN];
	size_t length;
	size_t iter;
}visca_buffer_t;


#ifdef __cplusplus
extern "C" {
#endif

static inline void visca_buffer_init(visca_buffer_t * buffer)
{
	assert(NULL != buffer);
	buffer->length = 0;
	buffer->iter = 0;
}

static inline int visca_buffer_append(visca_buffer_t * buffer, const unsigned char * data, size_t data_len)
{
	assert(NULL != buffer);
	int i;
	if((buffer->length + data_len) > MAX_VISCA_BUFFER_LEN) return VISCA_ERROR_COMMAND_BUFFER;
	if(NULL == data || data_len == 0) return VISCA_SUCCESS;
	
	size_t iter = buffer->iter;
	unsigned char * p = buffer->data;
	
	for(i = 0; i < data_len; ++i)
	{
		p[iter++] = data[i];
		if(iter >= MAX_VISCA_BUFFER_LEN) iter -= MAX_VISCA_BUFFER_LEN;		
	}
	buffer->length += data_len;
	
	return VISCA_SUCCESS;
}

static inline int visca_buffer_get_packet(visca_buffer_t * buffer, visca_packet_t * packet)
{
	assert(NULL != buffer && NULL != packet);
	
	size_t iter = buffer->iter;	
	size_t i = 0;
	
	if(buffer->length == 0) return VISCA_FAILURE; // no data
	
	while((i < buffer->length) && (i < MAX_VISCA_PACKET_LEN))
	{		
		packet->data[i++] = buffer->data[iter++];
		if(packet->data[i - 1] == 0xFF) break;
		if(iter >= MAX_VISCA_BUFFER_LEN) iter -= MAX_VISCA_BUFFER_LEN;
	}
	
	if(packet->data[i - 1] != 0xFF) return VISCA_FAILURE;
	
	buffer->iter = iter;
	buffer->length -= i;
	packet->length = i;
	return VISCA_SUCCESS;
}

static inline int visca_packet_construct(
				visca_packet_t * packet,
				int address, // device id
				unsigned char type, // enum VISCA_COMMAND_TYPE
				unsigned char category, // enum VISCA_CATEGORY
				const unsigned char * data,
				size_t data_len
				)
{
	int i;
	if(NULL == packet) return VISCA_ERROR_COMMAND_BUFFER;
	if(data_len > 14) return VISCA_ERROR_MESSAGE_LENGTH;
	if(address < 0 || address > 8) return VISCA_ERROR_SYNTAX;
	
	if(address == 0) address = 8;
	
	packet->data[0] = 0x80; packet->data[0] |= (unsigned char)address;
	packet->data[1] = type;
	
	unsigned char * p = &packet->data[2];
	if(category < 3)
	{
		if(data_len > 13) return VISCA_ERROR_MESSAGE_LENGTH;
		*p++ = category;
	}
	for(i = 0; i < data_len; ++i)
	{
		p[i] = data[i];
		if(p[i] == 0xFF) break;
	}
		
	p += i;	
	*p++ = (unsigned char)0xFF;
	packet->length = p - packet->data;
	
	return VISCA_SUCCESS;
}

static inline void visca_packet_init(visca_packet_t * packet)
{
	packet->data[0] = 0x80;
	packet->length = 0;
}

static inline int visca_packet_add_bytes(visca_packet_t * packet, const unsigned char * data, size_t len)
{
	assert(NULL != data);
	if((packet->length + len) > MAX_VISCA_PACKET_LEN) return VISCA_ERROR_COMMAND_BUFFER;
	
	int i = 0;
	unsigned char * p = &packet->data[packet->length];
	
	while(i < len)
	{
		p[i] = data[i];
		if(p[i++] == 0xFF) break;
	}
		
	packet->length += i;
		
	return VISCA_SUCCESS;
}

static inline int visca_packet_ack(visca_packet_t * packet, int address)
{
	if(address < 0 || address > 7) return VISCA_FAILURE;
	packet->data[0] = 0x80; packet->data[0] |= (unsigned char)(address << 4);
	packet->data[1] = VISCA_RESPONSE_ACK; packet->data[1] |= (unsigned char)(address & 0x07);
	packet->data[2] = 0xFF;
	packet->length = 3;
	return VISCA_SUCCESS;
}
static inline int visca_packet_complete(visca_packet_t * packet, int address, const unsigned char * data, size_t len)
{
	int i = 0;
	if(address < 0 || address > 7) return VISCA_FAILURE;	
	unsigned char * p = &packet->data[0];
	p[0] = 0x80; p[0] |= (unsigned char)(address << 4);
	p[1] = VISCA_RESPONSE_COMPLETE; p[1] |= (unsigned char)(address & 0x07);
	if(NULL != data)
	{
		if((3 + len) > MAX_VISCA_PACKET_LEN) return VISCA_ERROR_MESSAGE_LENGTH;
		for(i = 0; i < len; ++i)
		{
			if(data[i] == 0xFF) break;
			p[2 + i] = data[i];
		}
	}
	p[2 + i] = 0xFF;
	packet->length = 2 + i + 1;
	return VISCA_SUCCESS;
}

static inline int visca_packet_error(visca_packet_t * packet, int address, unsigned char errcode)
{
	if(address < 0 || address > 7) return VISCA_FAILURE;	
	unsigned char * p = &packet->data[0];
	p[0] = 0x80; p[0] |= (unsigned char)(address << 4);
	p[1] = VISCA_RESPONSE_ERROR; p[1] |= (unsigned char)(address & 0x07);
	p[2] = errcode;
	p[3] = 0xFF;
	packet->length = 4;
	return VISCA_SUCCESS;
}


static inline void visca_packet_dump2(int fd, const visca_packet_t * packet)
{
	int i;
	if(packet->length > MAX_VISCA_PACKET_LEN) 
	{
		fprintf(stderr, "invalid packet format\n");
		return;
	}
	FILE * fp = NULL;
	if(fd == STDIN_FILENO) fp = stdin;
	if(fd == STDOUT_FILENO) fp = stdout;
	else if(fd == STDERR_FILENO) fp = stderr;
	else fdopen(fd, "a");
	if(NULL == fp) fp = stdout;
	
	fprintf(fp, "packet:\tlength = %d\n", (int)packet->length);
	fprintf(fp, "\theader = %.2x\n", (unsigned char)packet->data[0]);
	fprintf(fp, "\ttype = %.2x\n", (unsigned char)packet->data[1]);
	fprintf(fp, "\tcategory = %.2x\n", (unsigned char)packet->data[2]);
	
	fprintf(fp, "\tdata: ");
	for(i = 3; i < (int)packet->length; i++)
	{
		if(packet->data[i] == 0xFF) break;
		fprintf(fp, "%.2x ", packet->data[i]);
	}
	fprintf(fp, "\n");
	if(i < (int)packet->length && (packet->data[i] == 0xFF))
	{
		fprintf(fp, "packet terminate with 0x%.2x\n", 0xFF);
	}else
	{
		fprintf(fp, "packet not terminate, last byte is 0x%.2x\n", packet->data[i - 1]);
	}
	if(fp && fd > STDERR_FILENO) fclose(fp);
}

#ifdef __cplusplus
}
#endif

#endif
