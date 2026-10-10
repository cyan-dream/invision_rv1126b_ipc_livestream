#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pthread.h>
#include <unistd.h>
#include "ingenic_uart.h"
#define IPC_DB_FILE "/oem/invision/database/ipc.db"
struct ipc_config ipc_config = {0};

// 从 /sys/class/net/eth0/operstate 文件获取网络接口的连接状态
int get_eth_status(const char *interface) {
    char filename[64];
    snprintf(filename, sizeof(filename), "/sys/class/net/%s/operstate", interface);

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    char status[10];
    if (fgets(status, sizeof(status), file) == NULL) {
        perror("fgets");
        fclose(file);
        return -1;
    }
    fclose(file);

    if (strstr(status, "up") != NULL) {
        return 1;
    } else if (strstr(status, "down") != NULL) {
        return 0;
    }

    return -1;
}

// 获取网络接口的 IP 地址
int get_eth_ip(const char *interface, char *ip) 
{
    int sockfd;
    struct ifreq ifr;

    // 创建套接字
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    // 设置 ifreq 结构体
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    ifr.ifr_addr.sa_family = AF_INET;

    // 执行 ioctl 操作
    if (ioctl(sockfd, SIOCGIFADDR, &ifr) == -1) {
        perror("ioctl");
        close(sockfd);
        return -1;
    }

    // 将二进制 IP 地址转换为点分十进制字符串
    inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, ip, INET_ADDRSTRLEN);

    close(sockfd);
    return 0;
}

void parse_kv(kv_t *kv, struct ipc_config *config)
{
    char value[64];
    if (kv_get(kv, "onvif_state", value, sizeof(value)) == KV_OK) {
        printf("onvif_state = %s\n", value);
        config->onvif_state = atoi(value);
    }
    if (kv_get(kv, "onvif_port", value, sizeof(value)) == KV_OK) {
        printf("onvif_port = %s\n", value);
        config->onvif_port = atoi(value);
    }
    if (kv_get(kv, "onvif_authentication", value, sizeof(value)) == KV_OK) {
        printf("onvif_authentication = %s\n", value);
        config->onvif_authentication = atoi(value);
    }
    if (kv_get(kv, "timezone", value, sizeof(value)) == KV_OK) {
        printf("timezone = %s\n", value);
        config->timezone = atoi(value);
    }
    if (kv_get(kv, "ptz_addr", value, sizeof(value)) == KV_OK) {
        printf("ptz_addr = %s\n", value);
        config->ptz_addr = atoi(value);
    }
    if (kv_get(kv, "ptz_buad", value, sizeof(value)) == KV_OK) {
        printf("ptz_buad = %s\n", value);
        config->ptz_buad = atoi(value);
    }
    if (kv_get(kv, "ptz_reset", value, sizeof(value)) == KV_OK) {
        printf("ptz_reset = %s\n", value);
        config->ptz_reset = atoi(value);
    }
    if (kv_get(kv, "bitrate", value, sizeof(value)) == KV_OK) {
        printf("bitrate = %s\n", value);
        config->bitrate = atoi(value);
    }
    if (kv_get(kv, "width", value, sizeof(value)) == KV_OK) {
        printf("width = %s\n", value);
        config->width = atoi(value);
    }
    if (kv_get(kv, "height", value, sizeof(value)) == KV_OK) {
        printf("height = %s\n", value);
        config->height = atoi(value);
    }  
    if (kv_get(kv, "fps", value, sizeof(value)) == KV_OK) {
        printf("fps = %s\n", value);
        config->fps = atoi(value);
    }   
    if (kv_get(kv, "encode", value, sizeof(value)) == KV_OK) {
        printf("encode = %s\n", value);
        config->encode = atoi(value);
    }
    if (kv_get(kv, "motion_detection", value, sizeof(value)) == KV_OK) {
        printf("motion_detection = %s\n", value);
        config->motion_detection = atoi(value);
    }
    if (kv_get(kv, "detection_photo", value, sizeof(value)) == KV_OK) {
        printf("detection_photo = %s\n", value);
        config->detection_photo = atoi(value);
    }
    if (kv_get(kv, "detection_video", value, sizeof(value)) == KV_OK) {
        printf("detection_video = %s\n", value);
        config->detection_video = atoi(value);
    } 
    if (kv_get(kv, "video_duration", value, sizeof(value)) == KV_OK) {
        printf("video_duration = %s\n", value);
        config->video_duration = atoi(value);
    }   
    if (kv_get(kv, "detection_ltx", value, sizeof(value)) == KV_OK) {
        printf("detection_ltx = %s\n", value);
        config->detection_ltx = atoi(value);
    }  
    if (kv_get(kv, "detection_lty", value, sizeof(value)) == KV_OK) {
        printf("detection_lty = %s\n", value);
        config->detection_lty = atoi(value);
    }     
    if (kv_get(kv, "detection_rbx", value, sizeof(value)) == KV_OK) {
        printf("detection_rbx = %s\n", value);
        config->detection_rbx = atoi(value);
    }  
    if (kv_get(kv, "detection_rby", value, sizeof(value)) == KV_OK) {
        printf("detection_rby = %s\n", value);
        config->detection_rby = atoi(value);
    } 
    if (kv_get(kv, "sensitivity", value, sizeof(value)) == KV_OK) {
        printf("sensitivity = %s\n", value);
        config->sensitivity = atoi(value);
    } 
    if (kv_get(kv, "occlusion_num", value, sizeof(value)) == KV_OK) {
        printf("occlusion_num = %s\n", value);
        config->occlusion_num = atoi(value);
    }  
    for (int i=0; i < config->occlusion_num; i++) {
        char key[32];
        sprintf(key,"occlusion%d_ltx", i);
        if (kv_get(kv, key, value, sizeof(value)) == KV_OK) {
            config->occlusion_coordinate[i].ltx = atoi(value);
        }
        sprintf(key,"occlusion%d_lty", i);
        if (kv_get(kv, key, value, sizeof(value)) == KV_OK) {
            config->occlusion_coordinate[i].lty = atoi(value);
        }
        sprintf(key,"occlusion%d_rbx", i);
        if (kv_get(kv, key, value, sizeof(value)) == KV_OK) {
            config->occlusion_coordinate[i].rbx = atoi(value);
        }
        sprintf(key,"occlusion%d_rby", i);
        if (kv_get(kv, key, value, sizeof(value)) == KV_OK) {
            config->occlusion_coordinate[i].rby = atoi(value);
        }       
    } 
    if (kv_get(kv, "osd_state", value, sizeof(value)) == KV_OK) {
        printf("osd_state = %s\n", value);
        config->osd_state = atoi(value);
    } 
    if (kv_get(kv, "osd_text", config->osd_text, sizeof(config->osd_text)) == KV_OK) {
        printf("osd_text = %s\n", value);
    }

    if (kv_get(kv, "ftp_url", config->ftp_url, sizeof(config->ftp_url)) == KV_OK) {
        printf("ftp_url = %s\n", value);
    }

    if (kv_get(kv, "ftp_username", config->ftp_username, sizeof(config->ftp_username)) == KV_OK) {
        printf("ftp_username = %s\n", value);
    }
    if (kv_get(kv, "ftp_password", config->ftp_password, sizeof(config->ftp_password)) == KV_OK) {
        printf("ftp_password = %s\n", value);
    }
    if (kv_get(kv, "ftp_upload", value, sizeof(value)) == KV_OK) {
        printf("ftp_upload = %s\n", value);
        config->ftp_upload = atoi(value);
    } 
    return;             
}


int get_date(char *date, int format)
{
    // 获取当前时间
    time_t current_time;
    struct tm *local_time;

    time(&current_time);
    local_time = localtime(&current_time);

    // 分别保存年、月、日、时、分、秒
    int year = local_time->tm_year + 1900;
    int month = local_time->tm_mon + 1;
    int day = local_time->tm_mday;
    int hour = local_time->tm_hour;
    int minute = local_time->tm_min;
    int second = local_time->tm_sec;
    if (format) {
        sprintf(date,"%04d/%02d/%02d  %02d:%02d:%02d", year, month, day, hour, minute, second);
    } else {
        sprintf(date,"%d", year);
        sprintf(&date[4],"%02d", month);
        sprintf(&date[6],"%02d", day);
        sprintf(&date[8],"%02d", hour);
        sprintf(&date[10],"%02d", minute);
        sprintf(&date[12],"%02d", second);
    }
    return 0;
}

int set_timezone(int timezone)
{
    if (timezone < 0 || timezone > 26) return -1;
    ipc_config.timezone = timezone;
    char file_path[32] = {0};
    sprintf(file_path, "/usr/share/zoneinfo/Etc/GMT%+d", 12-timezone);
    char cmd[64] = {0};
    sprintf(cmd, "ln -sf %s /etc/localtime", file_path);
    system(cmd);
    char value[16];
    snprintf(value, sizeof(value), "%d", ipc_config.timezone);
    kv_set(&ipc_config.kv, "timezone", value);
    return 0;
}

int start_onvif(char *ip)
{
    system("killall wsdd");
    system("killall onvif_srvd");
    if (ipc_config.onvif_state == 0) return 0;
    char cmd[256] = {0};
    sprintf(cmd, "/oem/bin/wsdd --if_name eth0 --type tdn:NetworkVideoTransmitter --xaddr http://%s:%d/onvif/device_service --scope \"onvif://www.onvif.org/name/IPC onvif://www.onvif.org/Profile/Streaming\"", ip, ipc_config.onvif_port);
    system(cmd);
    memset(cmd, 0, sizeof(cmd));
    if (ipc_config.encode == 0) {
        sprintf(cmd, "/oem/bin/onvif_srvd  --ifs eth0 --scope onvif://www.onvif.org/name/IPC --scope onvif://www.onvif.org/Profile/S --name RTSP --width 720 --height 576 --url rtsp://%s/live --type H264 --port %d", ip, ipc_config.onvif_port);
    } else if (ipc_config.encode == 1) {
        sprintf(cmd, "/oem/bin/onvif_srvd  --ifs eth0 --scope onvif://www.onvif.org/name/IPC --scope onvif://www.onvif.org/Profile/S --name RTSP --width 720 --height 576 --url rtsp://%s/live --type H264 --port %d", ip, ipc_config.onvif_port);
    }
   
    //sprintf(cmd, "onvif_srvd  --ifs eth0 --scope onvif://www.onvif.org/name/IPC --scope onvif://www.onvif.org/Profile/S --name RTSP --width 720 --height 576 --url rtsp://%s/majorAV --type H264 --port %d", ip, ipc_config.onvif_port);

    system(cmd);
    printf("start onvif success\n");
    return 0;
}

void* ipc_thread(void* arg)
{
    char ip[INET_ADDRSTRLEN];
	while(1) {
        sleep(3);
		int status = get_eth_status("eth0");
        if (status) {
            if (0 == get_eth_ip("eth0", ip)) {
                start_onvif(ip);
                break;                
            } else {
                system("udhcpc -n");
            }
        }
	}
	return NULL;
}

int init_ipc_config(void)
{
    pthread_t id;
    if (kv_open(&ipc_config.kv, IPC_DB_FILE) != KV_OK) {
        printf("open ipc db failed\n");
        return 1;
    }
    parse_kv(&ipc_config.kv, &ipc_config);
    int ret = open_uart(PANTILT_UART, (enum uart_baud_rate)ipc_config.ptz_buad, 8, 'n', 1);
    if (ret < 0) {
        perror("open PANTILT_UART fail");
        //return -1;
    } 
    pthread_create(&id, NULL, ipc_thread, NULL);
    return 0;
}

