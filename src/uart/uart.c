#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "ingenic_uart.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "circular_buffer.h"
#include "uart.h"
#include "update.h"
#include <sys/stat.h>
#include "cmd.h"
#include "gpio.h"
#include "ipc.h"
#define BUFFER_LEN 512
#define TIMEOUT 1000


typedef struct{
	char code;
	char status;
	char reserved;
	char func;
	char cnt_h;
	char cnt_l;
	char crc_h;
	char crc_l;
} uart_cmd_t;
extern int running;
/**
 * @brief  发送, 接收数据测试
 * @param  uart_name: 输入参数, 串口名
 * @return 成功: 0
 *         失败: -1
 */
int send_data(const char *uart_name)
{
    int ret = -1;
    uint8_t uart_send[] = {0x6e, 0x00, 0x00, 0x7c, 0x00, 0x02, 0x52, 0x90, 0x00, 0x17, 0x62, 0xd6};
    uint32_t uart_send_size = sizeof(uart_send);

    printf("uart %s write data[len = %d]: ", uart_name, uart_send_size);
    for (uint8_t i = 0; i < uart_send_size; i++)
        printf("0x%02X ", uart_send[i]);
    printf("\n");

    ret = uart_write_data(uart_name, uart_send, uart_send_size);
    if (ret < 0) {
        printf("uart %s send data fail\n", uart_name);
        close_uart(uart_name);
        return -1;
    }

    //uart_clear_data(uart_name);

    return 0;
}

int recv_data(const char *uart_name)
{
    int ret = -1;
    uint8_t uart_recv[32] = {0};
    // 串口接收数据
    ret = uart_read_data(uart_name, uart_recv, BUFFER_LEN, TIMEOUT);
    if (ret > 0) {
        printf("uart %s read data[len = %d]: ", uart_name, ret);
        for (uint8_t i = 0; i < ret; i++)
            printf("0x%02X ", uart_recv[i]);
        printf("\n");
    } else {
        printf("uart %s recv data fail\n", uart_name);
        close_uart(uart_name);
        return -1;
    }
}

int uart_notify(const char *uart, media_event_t event)
{
	switch(event) {
		case NT_CONFIG_ACK:
			printf("notify event NT_CONFIG_ACK\n");
			break;
		case RUN_NORM:
			printf("notify event RUN_NORM\n");
			break;
		case NT_POWER_DOWN:
			printf("notify event NT_POWER_DOWN\n");
			break;
		case PIC_CAP_ACK:
			printf("notify event PIC_CAP_ACK\n");
			break;
		case REC_START_ACK:
			printf("notify event REC_START_ACK\n");
			break;
		case REC_STOP_ACK:
			printf("notify event REC_STOP_ACK\n");
			break;
		case WIFI_MODUL_ERR:
			printf("notify event WIFI_MODUL_ERR\n");
			break;
		case WIFI_START_ERR:
			printf("notify event WIFI_START_ERR\n");
			break;
		case WIFI_STOP_ERR:
			printf("notify event WIFI_STOP_ERR\n");
			break;
		case WIFI_START_SUC:
			printf("notify event WIFI_START_SUC\n");
			break;
		case WIFI_STOP_SUC:
			printf("notify event WIFI_STOP_SUC\n");
			break;
		case UPGRADE_NOSPACE:
			printf("notify event UPGRADE_NOSPACE\n");
			break;
		case UPGRADE_FIN:
			printf("notify event UPGRADE_FIN\n");
			break;
		case UPGRADE_NAK:
			printf("notify event UPGRADE_NAK\n");
			break;
		case VIDEO_DEV_ERR:
			printf("notify event VIDEO_DEV_ERR\n");
			break;
		default:
			break;
	}

	unsigned char buffer[32];

	buffer[0] = 0x6e;
	buffer[1] = 0x00;
	buffer[2] = 0x00;
	buffer[3] = 0x7c;
    buffer[4] = 0x00;
	buffer[5] = 0x02;
	unsigned short crc = crc16_calc(buffer, 6);
	buffer[6] = (crc>>8)&0xff;
	buffer[7] = crc&0xff;

	buffer[8] = 0x00; 
	buffer[9] = event;
	
	crc = crc16_calc(&buffer[8], 2);

	buffer[10]	= (crc>>8)&0xff;// buffer crc
	buffer[11] = crc&0xff;

    uart_write_data(uart, buffer, 12);
    //uart_clear_data(uart);
	return 0;
}

void uart_send_remote_update_cmd(char *uart, int xcmd)
{
	unsigned char buffer[128];

	uart_cmd_t cmd;
	memset(&cmd, 0, sizeof(cmd));

	cmd.code = 0x6e;
	cmd.cnt_h = 0x00;
	cmd.cnt_l = 2;
	cmd.func = 0x58;
	short crc = crc16_calc((unsigned char*)&cmd, sizeof(cmd)-2);
	cmd.crc_h = ((crc>>8)&0xff);
	cmd.crc_l = (crc&0xff);

	buffer[0] = 0x00; 
	buffer[1] = (xcmd&0xff); // update req

	crc = crc16_calc(buffer, cmd.cnt_l);

	buffer[cmd.cnt_l]	= ((crc>>8)&0xff);// buffer crc
	buffer[cmd.cnt_l+1] = (crc&0xff);

    uart_write_data(uart, (unsigned char*)&cmd, sizeof(cmd));
    uart_write_data(uart, buffer, cmd.cnt_l+2);
    //uart_clear_data(uart);
	return;
}

void uart_send_update_data(char *uart, char* buffer, int len)
{

	uart_cmd_t cmd;
	memset(&cmd, 0, sizeof(cmd));

	cmd.code = 0x6e;
	cmd.cnt_h = ((len>>8)&0xff);
	cmd.cnt_l = (len&0xff);
	cmd.func = 0x58;
	short crc = crc16_calc((unsigned char*)&cmd, sizeof(cmd)-2);
	cmd.crc_h = ((crc>>8)&0xff);
	cmd.crc_l = (crc&0xff);


	crc = crc16_calc((unsigned char*)buffer, len);

	buffer[len]	= ((crc>>8)&0xff);// buffer crc
	buffer[len+1] = (crc&0xff);
    int ret = uart_write_data(uart, (unsigned char*)&cmd, sizeof(cmd));
    if (ret < 0) {
        printf("uart %s send cmd fail\n", uart);
        return;
    }
    ret = uart_write_data(uart, buffer, len+2);
    if (ret < 0) {
        printf("uart %s send data fail\n", uart);
        return;
    }
    //uart_clear_data(uart);
	return;
}

int send_cmd(const char *uart, unsigned char *buffer)
{
	uart_cmd_t cmd;
	memset(&cmd, 0, sizeof(cmd));

	cmd.code = buffer[0];
	cmd.cnt_h = buffer[4];
	cmd.cnt_l = buffer[5];
	cmd.func = buffer[3];
	//cmd.crc_h = buffer[6];
	//cmd.crc_l = buffer[7];
	short crc = crc16_calc((unsigned char*)&cmd, sizeof(cmd)-2);
	cmd.crc_h = ((crc>>8)&0xff);
	cmd.crc_l = (crc&0xff);
	int i;

	crc = crc16_calc(&buffer[8], cmd.cnt_l);

	buffer[8+cmd.cnt_l]	= ((crc>>8)&0xff);// buffer crc
	buffer[8+cmd.cnt_l+1] = (crc&0xff);
	//buffer[cmd.cnt_l]	= buffer[8+cmd.cnt_l];// buffer crc 有bug
	//buffer[cmd.cnt_l+1] = buffer[8+cmd.cnt_l+1];

	
	unsigned char* ptr = (unsigned char*)&cmd;
	printf("send cmd:");
	for (i = 0; i < sizeof(cmd); i++) {
		printf("%x ", ptr[i]);
	}

	for (i = 8; i < 8+cmd.cnt_l+2; i++) {
		printf( "%x ", buffer[i]);
	}
	printf("\n");
    uart_write_data(uart, (unsigned char*)&cmd, sizeof(cmd));
    uart_write_data(uart, &buffer[8], cmd.cnt_l+2);
    //uart_clear_data(uart);
	return 0;
}
void update_notify(char *uart, media_event_t event, int index, int num, long pos, long fsize)
{

	unsigned char buffer[128];

	uart_cmd_t cmd;
	memset(&cmd, 0, sizeof(cmd));

	cmd.code = 0x6e;
	cmd.cnt_h = 0x00;
	cmd.func = 0x7c;

	buffer[0] = 0x00; 
	buffer[1] = event;

	buffer[2] = ((index>>8)&0xff);
	buffer[3] = (index&0xff);

	buffer[4] = ((num>>8)&0xff);
	buffer[5] = (num&0xff);

	unsigned char* ptr = &buffer[6];
	int i = 64-8;;
	for( ;i >= 0; i -= 8) {
		*ptr++ = ((pos>>i)&0xff);
	}

	i = 64-8;
	for( ;i >= 0; i -= 8) {
		*ptr++ = ((fsize>>i)&0xff);
	}

	cmd.cnt_l = (char)(ptr-buffer);
	short crc = crc16_calc((unsigned char*)&cmd, sizeof(cmd)-2);
	cmd.crc_h = ((crc>>8)&0xff);
	cmd.crc_l = (crc&0xff);
	crc = crc16_calc(buffer, cmd.cnt_l);

	buffer[cmd.cnt_l]	= ((crc>>8)&0xff);// buffer crc
	buffer[cmd.cnt_l+1] = (crc&0xff);


    uart_write_data(uart, (unsigned char*)&cmd, sizeof(cmd));
    uart_write_data(uart, buffer, cmd.cnt_l+2);
    //uart_clear_data(uart);
	printf( "write update ack  index = %d, num = %d, pos = %d, fsize = %d\n", 
				index, num, pos, fsize); 

	return;

}

static struct CircularBuffer *cb = NULL;
static pthread_mutex_t mutex;      // 保护缓冲区访问
static pthread_cond_t not_empty;   // 缓冲区非空条件
static pthread_cond_t not_full;    // 缓冲区非满条件
void* uart_read_thread(void* arg)
{
    int num, ret;
    unsigned char uart_recv[BUFFER_LEN];
    struct timespec timeout;
    while (running) {
        num = uart_read_data(COM, uart_recv, BUFFER_LEN, TIMEOUT);
        if (num > 0) {
            // 等待缓冲区有空间
            pthread_mutex_lock(&mutex);
            while ((cb->size == cb->capacity) && running) {
                // 计算超时时间
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 1;
                ret = pthread_cond_timedwait(&not_full, &mutex, &timeout);
                if (ret == ETIMEDOUT) {
                    printf("生产者等待超时\n");
                } else if (ret != 0) {
                    printf("错误码：%d\n", ret);
                }
            }

            if (0 > enqueue(cb, uart_recv, num))
                printf("CircularBuffer full");
            pthread_cond_signal(&not_empty);
            pthread_mutex_unlock(&mutex);                
        }
    }
    printf("uart_recv_thread exit\n");
    return NULL;
}

static int is_power_down = 0;
int is_record = 0;
int is_snap = 0;
struct update_info update_info = {0}; 

int create_dir(const char *path) 
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);  // 是目录返回1，不是目录返回0
       
    } else { // 不存在或无法访问
        mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    return 0;  
}

void uart_pelco_d_progress(unsigned char *cmd)
{ 
    unsigned short check_sum = 0;
	int i;
	for(i = 1; i < 6; i++) {
		check_sum += cmd[i];
	}

	if ((check_sum&0xff) != cmd[6]) {
        printf("check sum error\n");
        return;
	}

    uart_write_data(PANTILT_UART, cmd, 7);

    return;
}

extern int g_uart_client0, g_uart_client1, g_uart_client0_flag, g_uart_client1_flag;
void uart_cmd_process(unsigned char *data)
{
    unsigned char func = data[3];
    int len = data[4]*256+data[5];
    unsigned short subfunc = data[8]*256+data[9];
    switch (func) {
        case 0x7c:
            switch (subfunc) {
                case NT_CONFIG_UPDATE:
					{
						uart_notify(COM, NT_CONFIG_ACK);
                        uart_notify(COM, RUN_NORM);
					}
					break;
					case POWER_DOWN_ACK:
                        is_power_down = 1;
					break;
					case PIC_CAP_REQ:
						//check_dsc_dir();
						//capture_pic();
                        create_dir(PIC_FILE_PATH);
                        is_snap = 1;
						uart_notify(COM, PIC_CAP_ACK);
					break;
					case REC_START_REQ:
						//check_video_dir();
						//start_rec();
                        create_dir(VID_FILE_PATH);
                        is_record = RECORD_START;
						uart_notify(COM, REC_START_ACK);
					break;
					case REC_STOP_REQ:
						//stop_rec();
                        is_record = RECORD_FINISHING;
						uart_notify(COM, REC_STOP_ACK);
					break;
					case UPGRADE_REQ:
						//update_req();
                        if (g_uart_client0_flag) {
                            send(g_uart_client0, data, len+10, 0);
                        } else {
                            send(g_uart_client1, data, len+10, 0);
                        }
                        update_info.update_step = UPDATE_START;
					break;
					case UPGRADE_STOP:
						//update_stop();
                        if (update_info.update_step > UPDATE_IDEL)
                            update_info.update_step = UPDATE_STOP;
					break;
					case UPGRADE_FIN:
						update_info.update_state = UPDATE_STAT_FINISH;
					break;
					case PLAYBACK_START_REQ:
						uart_notify(COM, PLAYBACK_START_ACK);
					break;
					case PLAYBACK_STOP_REQ:
						uart_notify(COM, PLAYBACK_STOP_ACK);
					break;
            }
            break;
        case 0x58:
            update_info.update_ack = subfunc;
            break;

        default: 
            if (g_uart_client0_flag) {
                send(g_uart_client0, data, len+10, 0);
            } else {
                send(g_uart_client1, data, len+10, 0);
            }
            //printf("uart_cmd_process: default\n");            

            break;
    }
}
int send_config_event(char* uart, int event)
{

	unsigned char buffer[32];

	buffer[0] = 0x6e;
	buffer[1] = 0x00;
	buffer[2] = 0x00;
	buffer[3] = 0x7c;
    buffer[4] = 0x00;
	buffer[5] = 0x02;
	unsigned short crc = crc16_calc(buffer, 6);
	buffer[6] = (crc>>8)&0xff;
	buffer[7] = crc&0xff;

	buffer[8] = 0x00; 
	buffer[9] = event;

	crc = crc16_calc(&buffer[8], 2);

	buffer[10]	= (crc>>8)&0xff;// buffer crc
	buffer[11] = crc&0xff;

    uart_write_data(uart, buffer, 12);
    //uart_clear_data(uart);
	return 0;
}
/*
 * GPIO_PA(n)  (0*32 + n)
 * GPIO_PB(n)  (1*32 + n)
 * GPIO_PC(n)  (2*32 + n)
 * GPIO_PD(n)  (3*32 + n)
 * GPIO_PE(n)  (4*32 + n)
 * GPIO_PG(n)  (5*32 + n)
*/
#define RS485_SEL2      32  //PB00
#define RS485_SEL1      33  //PB01
void init_gpio(void)
{
   	if(gpio_simple_init(RS485_SEL1, GPIO_OUT) < 0)
		printf("RS485_SEL0 init failed\n");
	gpio_set_value(RS485_SEL1, GPIO_LOW);

	if(gpio_simple_init(RS485_SEL2, GPIO_OUT) < 0)
		printf("RS485_SEL1 init failed\n");
	gpio_set_value(RS485_SEL2, GPIO_HIGH); 
}
void* ui_thread(void* arg)
{
    int size, ret, i;
    struct timespec timeout;
    unsigned char uart_recv[BUFFER_LEN];
    make_crc32_table();
    crc16_init_table();
    init_gpio();
    //send_config_req(COM, PIC_CAP_REQ);
    update_info.update_state = UPDATE_STAT_FINISH;

    while (running) {
        pthread_mutex_lock(&mutex);
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;
        ret = pthread_cond_timedwait(&not_empty, &mutex, &timeout);
        if (ret == ETIMEDOUT) {
            //printf("消费者等待超时\n");
        } else if (ret != 0) {
            printf("错误码：%d\n", ret);
        }
        while (cb->size >= 7) {
            if ((cb->buffer[cb->head] != 0x6e) && (cb->buffer[cb->head] != 0xFF)) {
                clearBuffer(cb);
                break;
            }
            if (cb->buffer[cb->head] == 0x6e) {
                size = 10+cb->buffer[(cb->head+4)%cb->capacity]*256+cb->buffer[(cb->head+5)%cb->capacity];
                if (cb->size >= size) {
                    dequeue(cb, uart_recv, size);
                    printf("uart_recv:");
                    for (i = 0; i < size; i++)
                    printf("0x%02X ", uart_recv[i]);
                    printf("\n");
                    uart_cmd_process(uart_recv);
                } else {
                    break;
                }
            } else {
                dequeue(cb, uart_recv, 7);
                printf("uart_recv:");
                for (i = 0; i < 7; i++)
                printf("0x%02X ", uart_recv[i]);
                printf("\n");
                uart_pelco_d_progress(uart_recv);
            }

        }
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&mutex);

        if (is_power_down) {
            usleep(100000);
            system("echo mem > /sys/power/state");
            is_power_down = 0;
			uart_notify(COM, RUN_NORM);
        }
        do {
            switch (update_info.update_step) {
                case UPDATE_IDEL:
                break;
                case UPDATE_START:
                    system("sync");
                    update_info.update_fin = NULL;
                    update_info.update_pos = 0;
                    update_info.update_num = 0;
                    update_info.update_index = 0;
                    update_info.update_state = UPDATE_STAT_NORMAL;
                    memset(&(update_info.ft), 0, sizeof(update_info.ft));
                    update_info.update_file = NULL;
                    printf("update_start\n");
                    if (update_file_check()) {
                        update_notify(COM, UPGRADE_ACK, update_info.update_index+1, 
                                    update_info.update_num, update_info.update_pos, update_info.ft.size);
                        if (update_info.ft.type == 0 || update_info.ft.type == 2) { // local file
                            int uret = update_local_file();
                            if (uret < 0) {
                                uart_notify(COM, UPGRADE_NOSPACE);
                                update_info.update_step = UPDATE_STOP;
                                update_info.update_state = UPDATE_STAT_NO_SPACE;
                                //uart_notify(COM, RUN_NORM);
                            } else {
                                update_info.update_step = UPDATE_FINISH;
                            }
                        } else {
                            update_info.update_step = UPDATE_SEND_REQ;
                        }
                    } else {
                        uart_notify(COM, UPGRADE_NAK);
                        update_info.update_state = UPDATE_STAT_FILE_ERR;
                        update_info.update_step = UPDATE_IDEL;
                        uart_notify(COM, RUN_NORM);
                    }
                break;
                case UPDATE_SEND_REQ:
                    uart_send_remote_update_cmd(COM, RM_UPDATE_REQ);
                    update_info.update_step = UPDATE_WAIT_REQ_ACK; 
                    update_info.update_ack = -1;
                break;
                case UPDATE_WAIT_REQ_ACK:
                    if (update_info.update_ack == RM_UPDATE_REQ) {
                        update_info.update_ack = -1;
                        update_info.update_step = UPDATE_SEND_DATA;
                    } else {
                        printf("wait req ack timeout\n");
                        update_info.update_step = UPDATE_SEND_REQ;
                    }
                break;
                case UPDATE_SEND_DATA:
                    if (update_remote_file() <= 0) { // file == NULL or file formate error
                        update_info.update_step = UPDATE_STOP;
                        update_info.update_state = UPDATE_STAT_FILE_ERR;
                    } else {
                        update_info.update_step = UPDATE_WAIT_DATA_ACK;
                    }
                break;
                case UPDATE_WAIT_DATA_ACK:
                    if (update_info.update_ack == RM_UPDATE_DATA) {
                        update_info.update_ack = -1;
                        update_notify(COM, UPGRADE_ACK, update_info.update_index+1, 
                                        update_info.update_num, update_info.update_pos, update_info.ft.size);
                        if (update_info.update_pos == update_info.ft.size) { // last data pkt ack
                            uart_send_remote_update_cmd(COM, RM_UPDATE_PROGRAM);
                            update_info.update_step = UPDATE_WAIT_PROGRAM_ACK;
                            fclose(update_info.update_fin);
                            update_info.update_fin = NULL;
                        } else {
                            update_info.update_step = UPDATE_SEND_DATA;
                        }

                    } else {
                        update_info.update_step = UPDATE_SEND_DATA;
                        update_info.remote_ack_timeout = 1;
                    }
                break;
                case UPDATE_WAIT_PROGRAM_ACK:
                    if(update_info.update_ack == RM_UPDATE_PROGRAM) {
                        update_info.update_ack  = -1;
                        char path[128];
                        sprintf(path, "rm -f %s/%s", UPDATE_PATH, update_info.update_file);
                        system(path);
                        system("sync");
                        free(update_info.update_file);
                        update_info.update_file = NULL;
                        update_info.update_step = UPDATE_FINISH;
                    } else if (update_info.update_ack > RM_UPDATE_FIN) {
                        uart_notify(COM, UPGRADE_NAK);
                        update_info.update_state = UPDATE_STAT_FILE_ERR;
                        update_info.update_step = UPDATE_IDEL;
                        uart_notify(COM, RUN_NORM);
                    }
                break;
                case UPDATE_FINISH:
                    uart_notify(COM, UPGRADE_FIN);
                    update_info.update_step = UPDATE_IDEL;
                    update_info.update_state = UPDATE_STAT_FINISH;
                    uart_notify(COM, RUN_NORM);
                break;
                case UPDATE_STOP:
                    if (update_info.update_fin != NULL) {
                        fclose(update_info.update_fin);
                        update_info.update_fin = NULL;
                    }
                    if (update_info.update_file != NULL) {
                        free(update_info.update_file);
                        update_info.update_file = NULL;
                    }
                    uart_notify(COM, UPGRADE_STOP);
                    uart_send_remote_update_cmd(COM, RM_UPDATE_STOP);
                    update_info.update_step = UPDATE_IDEL;
                    uart_notify(COM, RUN_NORM);
                break;
            }
        } while (update_info.update_step == UPDATE_SEND_DATA);

    }
    printf("ui_thread exit\n");
    return NULL;
}


void* uart_init_pthread(void *arg)
{
    int ret;
    //const char *uart0 = "/dev/ttyS0";
    //const char *uart1 = "/dev/ttyS1";
    pthread_t id1, id2, id3;
    cb = createBuffer(512);
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&not_empty, NULL);
    pthread_cond_init(&not_full, NULL);
    ret = open_uart(COM, BAUD_115200, 8, 'n', 1);
    //if (ret < 0) {
    //    perror("open uart fail");
    //    return NULL;
    //}

    //pthread_create(&id1, NULL, uart_read_thread, NULL);
    //pthread_create(&id2, NULL, ui_thread, NULL);
    pthread_create(&id3, NULL, cmd_thread, NULL);

    //pthread_join(id1, NULL);
    //pthread_join(id2, NULL);
    pthread_join(id3, NULL);

    destroyBuffer(cb);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&not_full);
    close_uart(COM);
    printf("close uart %s\n", COM);

    return 0;

}
