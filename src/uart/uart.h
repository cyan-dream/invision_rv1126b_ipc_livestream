#ifndef UART_H
#define UART_H
#include <stdio.h>
#define PIC_FILE_PATH "/userdata/picture"
#define VID_FILE_PATH "/userdata/video"

#define COM	"/dev/ttyS2"
struct ui_t { 
    int sys_status; //off-0,on-1,auto-2
    int wifi_status;
    char essid[32];
	char passwd[32];
    int config_status;
};

typedef struct {
	int code;
	int type;
	long long size;
	unsigned int crc32;
} __attribute__((packed)) update_file_t;

enum {
	UPDATE_IDEL,
	UPDATE_START,
	UPDATE_STOP,
	UPDATE_FINISH,
	UPDATE_SEND_REQ,
	UPDATE_WAIT_REQ_ACK,
	UPDATE_SEND_DATA,
	UPDATE_WAIT_DATA_ACK,
	UPDATE_WAIT_PROGRAM_ACK,
};

#define RM_UPDATE_REQ	0
#define RM_UPDATE_DATA	1
#define RM_UPDATE_PROGRAM	2
#define RM_UPDATE_STOP		3
#define RM_UPDATE_WAIT	4
#define RM_UPDATE_FIN		5

enum update_stat {
	UPDATE_STAT_NORMAL,
	UPDATE_STAT_FILE_ERR,
	UPDATE_STAT_NO_SPACE,
	UPDATE_STAT_FINISH
};

struct update_info {
	int update_ack;
	int update_step;
	update_file_t ft;
	long update_pos;
	int update_num;
	int update_index;
	FILE* update_fin;
	char* update_file;
	int remote_data_num;
	int update_state;
	int remote_ack_timeout;
};
typedef enum {
	NT_NONE,
	NT_CONFIG_REQ,
	NT_CONFIG_UPDATE,
	NT_CONFIG_ACK,
	RUN_NORM,
	NT_POWER_DOWN,
	POWER_DOWN_ACK,
	PIC_CAP_REQ,
	PIC_CAP_ACK,
	PIC_CAP_NAK,
	REC_START_REQ,
	REC_START_ACK,
	REC_START_NAK,
	REC_STOP_REQ,
	REC_STOP_ACK,
	WIFI_NONE,
	WIFI_NO_MODUL,
	WIFI_MODUL_ERR,
	WIFI_START_ERR,
	WIFI_START_SUC,
	WIFI_STOP_ERR,
	WIFI_STOP_SUC,
	WIFI_STA_NUM,
	UPGRADE_REQ,
	UPGRADE_ACK,
	UPGRADE_NAK,
	UPGRADE_STOP,
	UPGRADE_FIN,
	UPGRADE_NOSPACE,
	VIDEO_DEV_ERR,
	PLAYBACK_START_REQ,
	PLAYBACK_START_ACK,
	PLAYBACK_START_NAK,
	PLAYBACK_STOP_REQ,
	PLAYBACK_STOP_ACK,
	NT_END	
} media_event_t;
#ifdef __cplusplus
extern "C" {
#endif

void uart_send_update_data(char *uart, char* buffer, int len);
int send_cmd(const char *uart, unsigned char *buffer);
int uart_notify(const char *uart, media_event_t event);
int create_dir(const char *path);
#ifdef __cplusplus
}
#endif
#endif