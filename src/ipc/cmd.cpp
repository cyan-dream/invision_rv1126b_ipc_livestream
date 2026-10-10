#include "cmd.h"
#include "uart.h"
#include <stdio.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "circular_buffer.h"
#include "ipc.h"   
#include <math.h>
#include "ingenic_uart.h"
#include "uart.h"
#include <dirent.h>
#include <sys/stat.h>

extern int running;
int g_uart_client0 = 0;
int g_uart_client1 = 0;
int g_uart_client0_flag = 0;
int g_uart_client1_flag = 0;
extern int is_record;
extern int is_snap;
extern struct ipc_config ipc_config;
extern struct update_info update_info;
void get_ip_address(const char *interface_name, char *ip) {
    int sockfd;
    struct ifreq ifr;

    // 创建一个套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return;
    }

    // 设置接口名称
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    // 获取 IP 地址
    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl");
        close(sockfd);
        return;
    }

    // 将 IP 地址转换为点分十进制格式
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    strcpy(ip, inet_ntoa(sin->sin_addr));

    // 关闭套接字
    close(sockfd);
}

// 定义函数来获取子网掩码
void get_netmask(const char *interface_name, char *netmask) {
    int sockfd;
    struct ifreq ifr;

    // 创建一个套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return;
    }

    // 设置接口名称
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    // 获取子网掩码
    if (ioctl(sockfd, SIOCGIFNETMASK, &ifr) < 0) {
        perror("ioctl");
        close(sockfd);
        return;
    }

    // 将子网掩码转换为点分十进制格式
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_netmask;
    strcpy(netmask, inet_ntoa(sin->sin_addr));

    // 关闭套接字
    close(sockfd);
}

// 定义函数来获取网关
void get_gateway(const char *interface_name, char *gateway) {
    FILE *fp;
    char line[256];
    char iface[IFNAMSIZ];
    char gw[INET_ADDRSTRLEN];

    // 打开路由表文件
    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        perror("fopen");
        return;
    }

    // 跳过标题行
    fgets(line, sizeof(line), fp);

    // 逐行读取路由表
    while (fgets(line, sizeof(line), fp) != NULL) {
        sscanf(line, "%s %*s %s", iface, gw);
        if (strcmp(iface, interface_name) == 0 && strcmp(gw, "00000000") != 0) {
            // 将网关地址转换为点分十进制格式
            unsigned int gw_addr = strtoul(gw, NULL, 16);
            // 手动进行字节序转换
            unsigned char *bytes = (unsigned char *)&gw_addr;
            unsigned int correct_addr = (bytes[3] << 24) | (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];

            struct in_addr addr;
            addr.s_addr = correct_addr;
            strcpy(gateway, inet_ntoa(addr));
            break;
        }
    }

    // 关闭文件
    fclose(fp);
}

int check_eth0_dhcp(void) 
{
    FILE *file = fopen("/etc/network/interfaces", "r");
    if (file == NULL) {
        perror("无法打开配置文件");
        return -1;
    }

    char line[128];
    int found_interface = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (!found_interface && strstr(line, "eth0") != NULL) {
            found_interface = 1;
        } else if (found_interface && strstr(line, "dhcp") != NULL) {
            fclose(file);
            return 1;
        } else if (found_interface && strstr(line, "iface") != NULL) {
            // 遇到新的 iface 配置，说明当前 eth0 配置段结束
            break;
        }
    }

    fclose(file);
    return 0;
}

int set_device_network(char *ip, char *netmask, char *gateway, int dhcp)
{
    char cmd[128] = {0};
    if (dhcp == 0) {
        sprintf(cmd, "ifconfig eth0 %s netmask %s", ip, netmask);
        system(cmd); 

        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "route add default gw %s eth0", gateway);
        system(cmd);
        
        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "sed -i '/^iface eth0 inet /c\\iface eth0 inet static' /etc/network/interfaces");
        system(cmd);

        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "sed -i '/^[[:space:]]*address /c\\    address %s' /etc/network/interfaces", ip);
        system(cmd);

        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "sed -i '/^[[:space:]]*netmask /c\\    netmask %s' /etc/network/interfaces", netmask);
        system(cmd);

        memset(cmd, 0, sizeof(cmd));
        sprintf(cmd, "sed -i '/^[[:space:]]*gateway /c\\    gateway %s' /etc/network/interfaces", gateway);
        system(cmd);
        start_onvif(ip);
    } else {
        sprintf(cmd, "sed -i '/^iface eth0 inet /c\\iface eth0 inet dhcp' /etc/network/interfaces");
        system(cmd);
    }

#if 0
    FILE* interfaces = fopen("/etc/network/interfaces", "w");
    if (interfaces == NULL) {
        printf("open file error\n");
        return -1;
    }
    char buffer[32];
    sprintf(buffer, "auto lo\n"); fwrite(buffer, 1, strlen(buffer), interfaces);
    sprintf(buffer, "iface lo inet loopback\n"); fwrite(buffer, 1, strlen(buffer), interfaces);
	sprintf(buffer, "auto eth0\n"); fwrite(buffer, 1, strlen(buffer), interfaces);
    if (dhcp) {
        sprintf(buffer, "iface eth0 inet dhcp\n"); fwrite(buffer, 1, strlen(buffer), interfaces);
    } else {
        sprintf(buffer, "iface eth0 inet static\n"); fwrite(buffer, 1, strlen(buffer), interfaces);
        sprintf(buffer, "    address %s\n", ip); fwrite(buffer, 1, strlen(buffer), interfaces);
        sprintf(buffer, "    netmask %s\n", netmask); fwrite(buffer, 1, strlen(buffer), interfaces);
        sprintf(buffer, "    gateway %s\n", gateway); fwrite(buffer, 1, strlen(buffer), interfaces); 
    }
    fflush(interfaces);
	fclose(interfaces);
#endif
    printf("ifconfig ip\n");
    return 0;
}

// 过滤函数，去除以 `.` 开头的文件
static int filter(const struct dirent *entry) {
    return entry->d_name[0] != '.';
}

// 比较函数，按文件名升序排序
static int ascend_compare(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}
// 比较函数，按文件名降序排序
static int decline_compare(const struct dirent **a, const struct dirent **b) {
    return strcmp((*b)->d_name, (*a)->d_name);
}
static int send_file_info(int subfunc, int order, int page, int num, int Client, unsigned char *recvbuffer) 
{
	char directory_path[64];
    unsigned char* jpg_buffer = NULL;
    char* pic_name = NULL;
	if (subfunc == 1)
        sprintf(directory_path, "%s", VID_FILE_PATH);
	else if (subfunc == 2) {
        sprintf(directory_path, "%s", PIC_FILE_PATH);
    } else return -1;
	// 文件计数器
    //int file_count = 0;
	int itemLen = 0;
	unsigned char buffer[512] = {0};
	buffer[0] = 0x6E;
	buffer[3] = 0xF0;

    // 打开目录
    //DIR* directory = opendir(directory_path);
    char file_path[128] = {0};
    strcpy(file_path, directory_path);
    strcat(file_path, "/");
    int pathLen = strlen(file_path);
    char fileSize[8] = {0};
    int size_strlen, time_strlen;

    struct dirent **namelist;
    int n;
    if (order)
        n = scandir(directory_path, &namelist, filter, decline_compare);
    else
        n = scandir(directory_path, &namelist, filter, ascend_compare);
    int sum = 0;
    for (int i = 0; i < n; i++) {
            //printf("%s\n", namelist[i]->d_name);
        if (subfunc == 3 && strncmp(namelist[i]->d_name, pic_name, 11) != 0) {
            free(namelist[i]);
            continue;
        }
        if ((i < (page-1)*num) || (i > (page*num - 1))) {
            continue;
        }
        memset(fileSize, 0, sizeof(fileSize));

        itemLen = strlen(namelist[i]->d_name);
        strncpy(file_path+pathLen, namelist[i]->d_name, itemLen);
        file_path[pathLen+itemLen] = 0;
        struct stat st;
        if (stat(file_path, &st) == 0) {
            sprintf(fileSize, "%ld", st.st_size);
        }
        size_strlen = strlen(fileSize);

        memcpy(&buffer[12 + sum], namelist[i]->d_name, itemLen);
        buffer[12+sum+itemLen] = ',';
        memcpy(&buffer[13+sum+itemLen], fileSize, size_strlen);
        buffer[13+sum+itemLen+size_strlen] = ';';
				
        free(namelist[i]);
        sum += (itemLen + 1 + size_strlen + 1);
    }
    buffer[4] = ((4 + sum)>>8)&0xff;
    buffer[5] = (4 + sum)&0xff;
	buffer[6] = 0;
	buffer[7] = 0;
    buffer[9] = subfunc;
    buffer[10] = (n >> 8)&0xff;
    buffer[11] = n&0xff; 
    send(Client, buffer, 10 + sum + 4, 0);
    //printf("send file list ok, len = %d\n", sum+14);
    free(namelist);

    return 0;
}

int rm_file(unsigned char *buffer)
{
    int subfunc = buffer[9];
    int count = buffer[5];
    char directory_path[64];
    if (subfunc == 1) {
        sprintf(directory_path, "%s", VID_FILE_PATH);
    } else {
        sprintf(directory_path, "%s", PIC_FILE_PATH);
    }
    DIR *dir = opendir(directory_path);
    if (dir == NULL) return -1;
    if (count > 2) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, (char *)&buffer[10], strlen(entry->d_name)) == 0) {
                // 删除文件
                char file_path[64] = {0};
                sprintf(file_path, "%s/%s", directory_path, entry->d_name);
                if (remove(file_path) != 0) {
                    return -1;
                }
                if (subfunc == 1) {
                    //rtspServer->deleteServerMediaSession(entry->d_name);
                }
                printf("File %s deleted successfully.\n", file_path);
                break; // 仅删除一个文件即可，退出循环
            }
        }
            // 关闭文件夹
        closedir(dir);
    } else if (count == 2) {
        char cmd[64] = {0};
        sprintf(cmd, "rm %s/*", directory_path);
        system(cmd);
        // 关闭文件夹
        closedir(dir);
    }
    return 0;
    
}


void* socketThread(void* arg)
{
	int iRecvLen = 0,sendlen = 0, iRet;
    struct timeval tout, tv;  
    fd_set readfds;
    unsigned char ucRecvBuf[128];
    //int pSocketClient = (int)arg;
    pthread_t socketThreadID;
    tout.tv_sec = 2;
	tout.tv_usec = 0;
    int socket_fd = (int)(intptr_t)arg;
    //memset(ucRecvBuf, 0, 128);
    struct CircularBuffer *cb = createBuffer(128);
    //int recv_len = 0;
    while (running)
    {
        /* 接收客户端发来的数据 */ 
        
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);  
        FD_SET(socket_fd, &readfds);
        iRet = select(socket_fd+1, &readfds, NULL, NULL, &tv);
        if (iRet <= 0) {
            //printf("wait socket timeout\n");
            continue; //超时
        } 
        if (FD_ISSET(socket_fd, &readfds)) {
            iRecvLen = recv(socket_fd, ucRecvBuf, 128, 0);
        } else {
            continue;
        }
        if (iRecvLen <= 0) {
            printf("link fd%d disconnect\n", socket_fd);
            close(socket_fd);
            if (g_uart_client0 == socket_fd) {
                g_uart_client0 = 0;
            } else if (g_uart_client1 == socket_fd){
                g_uart_client1 = 0;
            }
            destroyBuffer(cb);
                //close(iSocketServer);
            return (void*)0;
        } 
        
        int i = 0;
        printf("net_recv:");
        while (i < iRecvLen) {
            printf("0x%x ", ucRecvBuf[i]);
            i++;
        }
        printf("\n");
  
        // 将接收到的数据放入环形缓冲区  
        if (!enqueue(cb, ucRecvBuf, iRecvLen)) {  
            printf("Buffer overflow, discarding data.\n");  
        }
        if (cb->buffer[cb->head] != 0x6e) {
            printf("clear buffer\n");
            clearBuffer(cb);
            continue;
        }

        while ((cb->buffer[cb->head] == 0x6e) && (cb->size > 6) && (cb->size >= (10+cb->buffer[(cb->head+5) % cb->capacity]))) {
            if (socket_fd == g_uart_client0) {
                g_uart_client0_flag = 1;
                g_uart_client1_flag = 0;
            } else if (socket_fd == g_uart_client1) {
                g_uart_client0_flag = 0;
                g_uart_client1_flag = 1;
            }
            memset(ucRecvBuf, 0, 128);
            dequeue(cb, ucRecvBuf, 10+cb->buffer[(cb->head+5) % cb->capacity]);
            if (ucRecvBuf[3] == 0x02) {
                send_cmd(COM, ucRecvBuf);
                system("reboot");
            } else if (ucRecvBuf[3] == 0x7c && ucRecvBuf[9] == 0x07) {
                //check_dsc_dir();
				//capture_pic();
                create_dir(PIC_FILE_PATH);
                is_snap = 1;
				uart_notify(COM, PIC_CAP_ACK);
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0x7c && ucRecvBuf[9] == 0x0a) {
                //check_video_dir();
				//start_rec();
                create_dir(VID_FILE_PATH);
                is_record = 1;
				uart_notify(COM, REC_START_ACK);
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0x7c && ucRecvBuf[9] == 0x0d) {
                //stop_rec();
                is_record = 0;
				uart_notify(COM, REC_STOP_ACK);
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] < 0xE0) {
                send_cmd(COM, ucRecvBuf);
            } else if (ucRecvBuf[3] == 0xF0) {				
                send_file_info(ucRecvBuf[9], ucRecvBuf[10], ucRecvBuf[12], ucRecvBuf[14], socket_fd, ucRecvBuf);
            } else if (ucRecvBuf[3] == 0xF1) {
                if (0 != rm_file(ucRecvBuf)) {
                    ucRecvBuf[1] = 0x05;
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                } 
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                
            } else if (ucRecvBuf[3] == 0xF2) {

            } else if (ucRecvBuf[3] == 0xF3) { //network
                if (ucRecvBuf[5] == 0) { //读取ip
                    const char *interface_name = "eth0";
                    char ip[16] = {0}, netmask[16] = {0}, gateway[16] = {0};
                    get_ip_address(interface_name, ip);
                    get_netmask(interface_name, netmask);
                    get_gateway(interface_name, gateway);
                    //get_device_netinfo(ip, netmask, gateway);
                    int num, len;
                    ucRecvBuf[8] = check_eth0_dhcp();
                    unsigned char *tmp = &ucRecvBuf[9];
                    num = sprintf((char *)tmp, "%s", ip);
                    tmp += num;
                    *tmp++ = 0;
                    num = sprintf((char *)tmp, "%s", netmask);
                    tmp += num; 
                    *tmp++ = 0;
                    num = sprintf((char *)tmp, "%s", gateway);
                    tmp += num; 
                    len = tmp - &ucRecvBuf[8];
                    ucRecvBuf[5] = len & 0xff;
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    *tmp++ = 0;
                    *tmp = 0;
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                    
                } else { //设置ip
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                    
                    if (ucRecvBuf[5]  == 1) {
                        set_device_network(NULL, NULL, NULL, 1);
                    } else {
                        sleep(1);
                        char ip[16] = {0}, netmask[16] = {0}, gateway[16] = {0};
                        int num;
                        unsigned char *tmp = &ucRecvBuf[9];
                        num = sprintf(ip, "%s", tmp);
                        tmp += (num+1);
                        num = sprintf(netmask, "%s", tmp);
                        tmp += (num+1);
                        sprintf(gateway, "%s", tmp);

                        set_device_network(ip, netmask, gateway, 0);
                    }
                }
            } else if (ucRecvBuf[3] == 0xF4) { //onvif
                if (ucRecvBuf[5] == 0) { //read
                    ucRecvBuf[5] = 4;
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = ipc_config.onvif_state;
                    ucRecvBuf[9] = ipc_config.onvif_authentication;
                    ucRecvBuf[10] = (ipc_config.onvif_port>>8)&0xff;
                    ucRecvBuf[11] = ipc_config.onvif_port&0xff;
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                } else {
                    ipc_config.onvif_state = ucRecvBuf[8];
                    ipc_config.onvif_authentication = ucRecvBuf[9];
                    ipc_config.onvif_port = ((ucRecvBuf[10])<<8) | ucRecvBuf[11];
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.onvif_state);
                    kv_set(&ipc_config.kv, "onvif_state", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.onvif_authentication);
                    kv_set(&ipc_config.kv, "onvif_authentication", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.onvif_port);
                    kv_set(&ipc_config.kv, "onvif_port", value); 
                    char ip[16] = {0};
                    if (0 == get_eth_ip("eth0", ip)) start_onvif(ip);
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                }
                
            } else if (ucRecvBuf[3] == 0xF5) { //timezone
                if (ucRecvBuf[5] == 0) { //read
                    ucRecvBuf[5] = 0xf;
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = ipc_config.timezone;
                    get_date((char*)&ucRecvBuf[9], 0);
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                } else {
                    ucRecvBuf[5] = 0xf;
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    set_timezone(ucRecvBuf[8]);
                    get_date((char*)&ucRecvBuf[9], 0);
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                }
            } else if (ucRecvBuf[3] == 0xF6) { //motion detection
                if ((ucRecvBuf[5] == 1) && (ucRecvBuf[8] == 0) ) { //get state
                    ucRecvBuf[5] = 6;
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = 0;
                    ucRecvBuf[9] = ipc_config.motion_detection;
                    ucRecvBuf[10] = ipc_config.detection_photo;
                    ucRecvBuf[11] = ipc_config.detection_video; 
                    ucRecvBuf[12] = (ipc_config.video_duration>>8)&0xff;
                    ucRecvBuf[13] = ipc_config.video_duration&0xff;                  
                } else if ((ucRecvBuf[5] == 1) && (ucRecvBuf[8] == 1) ) { //get range
                    ucRecvBuf[5] = 10;
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0; 
                    ucRecvBuf[8] = 1;
                    ucRecvBuf[9] = ipc_config.sensitivity;
                    int scale = round(ipc_config.detection_ltx/719.0*10000);
                    ucRecvBuf[10] = (scale>>8)&0xff;
                    ucRecvBuf[11] = scale&0xff;
                    scale = round(ipc_config.detection_lty/575.0*10000);
                    ucRecvBuf[12] = (scale>>8)&0xff;
                    ucRecvBuf[13] = scale&0xff;
                    scale = round(ipc_config.detection_rbx/719.0*10000);
                    ucRecvBuf[14] = (scale>>8)&0xff;
                    ucRecvBuf[15] = scale&0xff;
                    scale = round(ipc_config.detection_rby/575.0*10000);
                    ucRecvBuf[16] = (scale>>8)&0xff;
                    ucRecvBuf[17] = scale&0xff;                    
                } else if (ucRecvBuf[5] == 6) { //set state
                    ipc_config.motion_detection = ucRecvBuf[9];
                    ipc_config.detection_photo = ucRecvBuf[10];
                    ipc_config.detection_video = ucRecvBuf[11];
                    ipc_config.video_duration = ucRecvBuf[12]*256+ucRecvBuf[13];
                    //if (!ipc_config.motion_detection) clearBackground();
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.motion_detection);
                    kv_set(&ipc_config.kv, "motion_detection", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_photo);
                    kv_set(&ipc_config.kv, "detection_photo", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_video);
                    kv_set(&ipc_config.kv, "detection_video", value); 
                    snprintf(value, sizeof(value), "%d", ipc_config.video_duration);
                    kv_set(&ipc_config.kv, "video_duration", value);
                } else if (ucRecvBuf[5] == 10) { //set range
                    ipc_config.sensitivity = ucRecvBuf[9];
                    int tmp = round((ucRecvBuf[10]*256+ucRecvBuf[11])/10000.0*719);
                    ipc_config.detection_ltx = tmp;
                    tmp = round((ucRecvBuf[12]*256+ucRecvBuf[13])/10000.0*575);
                    ipc_config.detection_lty = tmp;
                    tmp = round((ucRecvBuf[14]*256+ucRecvBuf[15])/10000.0*719);
                    ipc_config.detection_rbx = tmp;
                    tmp = round((ucRecvBuf[16]*256+ucRecvBuf[17])/10000.0*575);
                    ipc_config.detection_rby = tmp;
                    //clearBackground();
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.sensitivity);
                    kv_set(&ipc_config.kv, "sensitivity", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_ltx);
                    kv_set(&ipc_config.kv, "detection_ltx", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_lty);
                    kv_set(&ipc_config.kv, "detection_lty", value); 
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_rbx);
                    kv_set(&ipc_config.kv, "detection_rbx", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.detection_rby);
                    kv_set(&ipc_config.kv, "detection_rby", value); 
                }

                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xF7) { //编码器配置
                if (ucRecvBuf[5] == 0) { //get
                    ucRecvBuf[5] = 10;
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = (ipc_config.bitrate>>24)&0xff;
                    ucRecvBuf[9] = (ipc_config.bitrate>>16)&0xff;
                    ucRecvBuf[10] = (ipc_config.bitrate>>8)&0xff;
                    ucRecvBuf[11] = ipc_config.bitrate&0xff;
                    ucRecvBuf[12] = (ipc_config.width>>8)&0xff;
                    ucRecvBuf[13] = ipc_config.width&0xff;
                    ucRecvBuf[14] = (ipc_config.height>>8)&0xff;
                    ucRecvBuf[15] = ipc_config.height&0xff;
                    ucRecvBuf[16] = ipc_config.fps;
                    ucRecvBuf[17] = ipc_config.encode;
                    ucRecvBuf[18] = 0;
                    ucRecvBuf[19] = 0;
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                } else { //set
                    ipc_config.bitrate = ucRecvBuf[8]*256*256*256+ucRecvBuf[9]*256*256+ucRecvBuf[10]*256+ucRecvBuf[11];
                    ipc_config.width = ucRecvBuf[12]*256+ucRecvBuf[13];
                    ipc_config.height = ucRecvBuf[14]*256+ucRecvBuf[15];
                    ipc_config.fps = ucRecvBuf[16];
                    ipc_config.encode = ucRecvBuf[17];

                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.bitrate);
                    kv_set(&ipc_config.kv, "bitrate", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.width);
                    kv_set(&ipc_config.kv, "width", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.height);
                    kv_set(&ipc_config.kv, "height", value); 
                    snprintf(value, sizeof(value), "%d", ipc_config.fps);
                    kv_set(&ipc_config.kv, "fps", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.encode);
                    kv_set(&ipc_config.kv, "encode", value);                                       

                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                }
            } else if (ucRecvBuf[3] == 0xF8) { //升级状态查询
                if (ucRecvBuf[5] == 0) { //get
                    ucRecvBuf[5] = 2;
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = update_info.update_state;
                    if (update_info.update_state == UPDATE_STAT_NORMAL) {
                        ucRecvBuf[9] = (unsigned char)(1.0*update_info.update_pos/update_info.ft.size*100);
                    } else {
                        ucRecvBuf[9] = 0;
                    }
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                }
            } else if (ucRecvBuf[3] == 0xF9) { //area_occlusion
                if (ucRecvBuf[5] == 0) { //get
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = ipc_config.occlusion_num;
                    for(int i=0; i < ipc_config.occlusion_num; i++) {
                        int scale = round(ipc_config.occlusion_coordinate[i].ltx/720.0*10000);
                        ucRecvBuf[9+8*i] = (scale>>8)&0xff;
                        ucRecvBuf[10+8*i] = scale&0xff;
                        scale = round(ipc_config.occlusion_coordinate[i].lty/576.0*10000);
                        ucRecvBuf[11+8*i] = (scale>>8)&0xff;
                        ucRecvBuf[12+8*i] = scale&0xff;
                        scale = round(ipc_config.occlusion_coordinate[i].rbx/720.0*10000);
                        ucRecvBuf[13+8*i] = (scale>>8)&0xff;
                        ucRecvBuf[14+8*i] = scale&0xff;
                        scale = round(ipc_config.occlusion_coordinate[i].rby/576.0*10000);
                        ucRecvBuf[15+8*i] = (scale>>8)&0xff;
                        ucRecvBuf[16+8*i] = scale&0xff;                                                
                    }
                    ucRecvBuf[5] = 1 + 8*ipc_config.occlusion_num;
                } else {
                    ipc_config.occlusion_num = ucRecvBuf[8];
                    char value[16], key[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.occlusion_num);
                    kv_set(&ipc_config.kv, "occlusion_num", value);
                    for (int i=0; i<ipc_config.occlusion_num; i++) {
                        ipc_config.occlusion_coordinate[i].ltx = round((ucRecvBuf[9+8*i]*256+ucRecvBuf[10+8*i])/10000.0*720);
                        ipc_config.occlusion_coordinate[i].lty = round((ucRecvBuf[11+8*i]*256+ucRecvBuf[12+8*i])/10000.0*576);
                        ipc_config.occlusion_coordinate[i].rbx = round((ucRecvBuf[13+8*i]*256+ucRecvBuf[14+8*i])/10000.0*720);
                        ipc_config.occlusion_coordinate[i].rby = round((ucRecvBuf[15+8*i]*256+ucRecvBuf[16+8*i])/10000.0*576);
                        snprintf(key, sizeof(key), "occlusion%d_ltx", i);
                        snprintf(value, sizeof(value), "%d", ipc_config.occlusion_coordinate[i].ltx);
                        kv_set(&ipc_config.kv, key, value);
                        snprintf(key, sizeof(key), "occlusion%d_lty", i);
                        snprintf(value, sizeof(value), "%d", ipc_config.occlusion_coordinate[i].lty);
                        kv_set(&ipc_config.kv, key, value);
                        snprintf(key, sizeof(key), "occlusion%d_rbx", i);
                        snprintf(value, sizeof(value), "%d", ipc_config.occlusion_coordinate[i].rbx);
                        kv_set(&ipc_config.kv, key, value);
                        snprintf(key, sizeof(key), "occlusion%d_rby", i);
                        snprintf(value, sizeof(value), "%d", ipc_config.occlusion_coordinate[i].rby);
                        kv_set(&ipc_config.kv, key, value);                        
                    }
                }
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xFA) { //area_invasion
                if (ucRecvBuf[5] == 2) {
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    if (ucRecvBuf[9] == 0) {
                        ucRecvBuf[5] = 7;
                        ucRecvBuf[10] = ipc_config.area_invasion.state;
                        ucRecvBuf[11] = ipc_config.area_invasion.photo;
                        ucRecvBuf[12] = ipc_config.area_invasion.video;
                        ucRecvBuf[13] = (ipc_config.area_invasion.video_duration>>8)&0xff;
                        ucRecvBuf[14] = ipc_config.area_invasion.video_duration&0xff;
                    } else if (ucRecvBuf[9] == 1) { 
                        ucRecvBuf[5] = 0xe;
                        ucRecvBuf[10] = ipc_config.area_invasion.sensitivity;
                        ucRecvBuf[11] = ipc_config.area_invasion.mode;
                        ucRecvBuf[12] = ipc_config.area_invasion.remain;
                        ucRecvBuf[13] = ipc_config.area_invasion.remain_time;
                        int scale = round(ipc_config.area_invasion.coordinate.ltx/720.0*10000);
                        ucRecvBuf[14] = (scale>>8)&0xff;
                        ucRecvBuf[15] = scale&0xff;
                        scale = round(ipc_config.area_invasion.coordinate.lty/576.0*10000);
                        ucRecvBuf[16] = (scale>>8)&0xff;
                        ucRecvBuf[17] = scale&0xff;
                        scale = round(ipc_config.area_invasion.coordinate.rbx/720.0*10000);
                        ucRecvBuf[18] = (scale>>8)&0xff;
                        ucRecvBuf[19] = scale&0xff;
                        scale = round(ipc_config.area_invasion.coordinate.rby/576.0*10000);
                        ucRecvBuf[20] = (scale>>8)&0xff;
                        ucRecvBuf[21] = scale&0xff;
                    }
                } else {
                    if (ucRecvBuf[9] == 0) {
                        ipc_config.area_invasion.state = ucRecvBuf[10];
                        ipc_config.area_invasion.photo = ucRecvBuf[11];
                        ipc_config.area_invasion.video = ucRecvBuf[12];
                        ipc_config.area_invasion.video_duration = (ucRecvBuf[13]<<8) + ucRecvBuf[14];
                    } else if (ucRecvBuf[9] == 1) { 
                        ipc_config.area_invasion.sensitivity = ucRecvBuf[10];
                        ipc_config.area_invasion.mode = ucRecvBuf[11];
                        ipc_config.area_invasion.remain = ucRecvBuf[12];
                        ipc_config.area_invasion.remain_time = ucRecvBuf[13];
                        ipc_config.area_invasion.coordinate.ltx = round((ucRecvBuf[14]*256+ucRecvBuf[15])/10000.0*720);
                        ipc_config.area_invasion.coordinate.lty = round((ucRecvBuf[16]*256+ucRecvBuf[17])/10000.0*576);
                        ipc_config.area_invasion.coordinate.rbx = round((ucRecvBuf[18]*256+ucRecvBuf[19])/10000.0*720);
                        ipc_config.area_invasion.coordinate.rby = round((ucRecvBuf[20]*256+ucRecvBuf[21])/10000.0*576);
                    }
                }
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xFB) { //tripwire_invasion
                if (ucRecvBuf[5] == 2) {
                    ucRecvBuf[6] = 0;
                    ucRecvBuf[7] = 0;
                    if (ucRecvBuf[9] == 0) {
                        ucRecvBuf[5] = 7;
                        ucRecvBuf[10] = ipc_config.tripwire_invasion.state;
                        ucRecvBuf[11] = ipc_config.tripwire_invasion.photo;
                        ucRecvBuf[12] = ipc_config.tripwire_invasion.video;
                        ucRecvBuf[13] = (ipc_config.tripwire_invasion.video_duration>>8)&0xff;
                        ucRecvBuf[14] = ipc_config.tripwire_invasion.video_duration&0xff;
                    } else if (ucRecvBuf[9] == 1) { 
                        ucRecvBuf[5] = 0xc;
                        ucRecvBuf[10] = ipc_config.tripwire_invasion.sensitivity;
                        ucRecvBuf[11] = ipc_config.tripwire_invasion.mode;
                        int scale = round(ipc_config.tripwire_invasion.coordinate.ltx/720.0*10000);
                        ucRecvBuf[12] = (scale>>8)&0xff;
                        ucRecvBuf[13] = scale&0xff;
                        scale = round(ipc_config.tripwire_invasion.coordinate.lty/576.0*10000);
                        ucRecvBuf[14] = (scale>>8)&0xff;
                        ucRecvBuf[15] = scale&0xff;
                        scale = round(ipc_config.tripwire_invasion.coordinate.rbx/720.0*10000);
                        ucRecvBuf[16] = (scale>>8)&0xff;
                        ucRecvBuf[17] = scale&0xff;
                        scale = round(ipc_config.tripwire_invasion.coordinate.rby/576.0*10000);
                        ucRecvBuf[18] = (scale>>8)&0xff;
                        ucRecvBuf[19] = scale&0xff;
                    }
                    
                } else {
                    if (ucRecvBuf[9] == 0) {
                        ipc_config.tripwire_invasion.state = ucRecvBuf[10];
                        ipc_config.tripwire_invasion.photo = ucRecvBuf[11];
                        ipc_config.tripwire_invasion.video = ucRecvBuf[12];
                        ipc_config.tripwire_invasion.video_duration = (ucRecvBuf[13]<<8) + ucRecvBuf[14];

                    } else if (ucRecvBuf[9] == 1) {
                        ipc_config.tripwire_invasion.sensitivity = ucRecvBuf[10];
                        ipc_config.tripwire_invasion.mode = ucRecvBuf[11];
                        ipc_config.tripwire_invasion.coordinate.ltx = round((ucRecvBuf[12]*256+ucRecvBuf[13])/10000.0*720);
                        ipc_config.tripwire_invasion.coordinate.lty = round((ucRecvBuf[14]*256+ucRecvBuf[15])/10000.0*576);
                        ipc_config.tripwire_invasion.coordinate.rbx = round((ucRecvBuf[16]*256+ucRecvBuf[17])/10000.0*720);
                        ipc_config.tripwire_invasion.coordinate.rby = round((ucRecvBuf[18]*256+ucRecvBuf[19])/10000.0*576);
                    }
                }
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xFC) { //OSD
                if (ucRecvBuf[5] == 0) { 
                    ucRecvBuf[5] = 1 + strlen(ipc_config.osd_text);
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    ucRecvBuf[8] = ipc_config.osd_state;
                    if (strlen(ipc_config.osd_text) > 0) {
                        memcpy(ucRecvBuf+9, ipc_config.osd_text, strlen(ipc_config.osd_text));
                    }
                } else {
                    ipc_config.osd_state = ucRecvBuf[8];
                    memcpy(ipc_config.osd_text, ucRecvBuf+9, ucRecvBuf[5]-1);
                    ipc_config.osd_text[ucRecvBuf[5]-1] = 0;
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.osd_state);
                    kv_set(&ipc_config.kv, "osd_state", value);
                    kv_set(&ipc_config.kv, "osd_text", ipc_config.osd_text);
                }
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xFD) { //ftp
                if (ucRecvBuf[5] == 0) {
                    ucRecvBuf[5] = strlen(ipc_config.ftp_url) + 1 + strlen(ipc_config.ftp_username) + 1 + strlen(ipc_config.ftp_password)+1+2;
                    ucRecvBuf[6] = ucRecvBuf[7] = 0;
                    memcpy(ucRecvBuf+8, ipc_config.ftp_url, strlen(ipc_config.ftp_url));
                    memcpy(ucRecvBuf+8+strlen(ipc_config.ftp_url)+1, ipc_config.ftp_username, strlen(ipc_config.ftp_username));
                    memcpy(ucRecvBuf+8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1, ipc_config.ftp_password, strlen(ipc_config.ftp_password));
                    ucRecvBuf[8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1+strlen(ipc_config.ftp_password)+1] = ipc_config.ftp_upload&0x01;
                    ucRecvBuf[8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1+strlen(ipc_config.ftp_password)+2] = (ipc_config.ftp_upload>>1)&0x01;
                } else {
                    memset(ipc_config.ftp_url, 0, sizeof(ipc_config.ftp_url));
                    memcpy(ipc_config.ftp_url, ucRecvBuf+8, strlen((char*)ucRecvBuf+8));
                    memset(ipc_config.ftp_username, 0, sizeof(ipc_config.ftp_username));
                    memcpy(ipc_config.ftp_username, ucRecvBuf+8+strlen(ipc_config.ftp_url)+1, strlen((char*)ucRecvBuf+8+strlen(ipc_config.ftp_url)+1));
                    memset(ipc_config.ftp_password, 0, sizeof(ipc_config.ftp_password));
                    memcpy(ipc_config.ftp_password, ucRecvBuf+8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1, strlen((char*)ucRecvBuf+8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1));
                    ipc_config.ftp_upload = ucRecvBuf[8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1+strlen(ipc_config.ftp_password)+1] +
                            (ucRecvBuf[8+strlen(ipc_config.ftp_url)+1+strlen(ipc_config.ftp_username)+1+strlen(ipc_config.ftp_password)+1+1]<<1);
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.ftp_upload);
                    kv_set(&ipc_config.kv, "ftp_upload", value);
                    kv_set(&ipc_config.kv, "ftp_url", ipc_config.ftp_url);
                    kv_set(&ipc_config.kv, "ftp_username", ipc_config.ftp_username);
                    kv_set(&ipc_config.kv, "ftp_password", ipc_config.ftp_password);
                }
                send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
            } else if (ucRecvBuf[3] == 0xFF) { //PTZ
                if (ucRecvBuf[5] == 0) {
                    ucRecvBuf[5] = 6;
                    ucRecvBuf[6] = ucRecvBuf[7] = ucRecvBuf[8] = 0;
                    ucRecvBuf[9] = ipc_config.ptz_addr;
                    ucRecvBuf[10] = (ipc_config.ptz_buad>>16)&0xff;
                    ucRecvBuf[11] = (ipc_config.ptz_buad>>8)&0xff;
                    ucRecvBuf[12] = (ipc_config.ptz_buad)&0xff;
                    ucRecvBuf[13] = ipc_config.ptz_reset;
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                } else if (ucRecvBuf[5] == 4) {
                    unsigned char cmd[7];
                    cmd[0] = 0xff;
                    cmd[1] = ipc_config.ptz_addr;
                    cmd[2] = ucRecvBuf[8];
                    cmd[3] = ucRecvBuf[9];
                    cmd[4] = ucRecvBuf[10];
                    cmd[5] = ucRecvBuf[11];
                    cmd[6] = cmd[1]+cmd[2]+cmd[3]+cmd[4]+cmd[5];
                    unsigned short xcmd = (cmd[2]&0xff);
		            xcmd <<= 8;
		            xcmd |= (cmd[3]&0xff);
                    if ((xcmd == 0x0080) || (xcmd == 0x0100) ||
                        (xcmd == 0x0020) || (xcmd == 0x0040) ||
                        (xcmd == 0x0200) || (xcmd == 0x0400) ||
                        (xcmd == 0x0025) || (xcmd == 0x0027)) {
                        uart_write_data(COM, cmd, 7);
                    } else {
                        if (xcmd == 0x0000) uart_write_data(COM, cmd, 7);
                        int ret = uart_write_data(PANTILT_UART, cmd, 7);
                    }
                } else if (ucRecvBuf[5] == 6) {
                    ipc_config.ptz_addr = ucRecvBuf[9];
                    ipc_config.ptz_buad = ucRecvBuf[10]*256*256+ucRecvBuf[11]*256+ucRecvBuf[12];
                    ipc_config.ptz_reset = ucRecvBuf[13];
                    char value[16];
                    snprintf(value, sizeof(value), "%d", ipc_config.ptz_addr);
                    kv_set(&ipc_config.kv, "ptz_addr", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.ptz_buad);
                    kv_set(&ipc_config.kv, "ptz_buad", value);
                    snprintf(value, sizeof(value), "%d", ipc_config.ptz_reset);
                    kv_set(&ipc_config.kv, "ptz_reset", value); 
                    send(socket_fd, ucRecvBuf, 10 + ucRecvBuf[5], 0);
                }
            }
        }
        
    }
    destroyBuffer(cb);
    return NULL;
}

void* cmd_thread(void* arg)
{
	struct sockaddr_in tSocketServerAddr;
	struct sockaddr_in tSocketClientAddr;
    pthread_t socketThreadID;
    fd_set readfds;
	int iRet;
	int iAddrLen;
	struct timeval tv;
	int iSocketServer = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (-1 == iSocketServer)
	{
		printf("socket error!\n");
		return NULL;
	}

    // 设置 SO_REUSEADDR 选项
    int opt = 1;
    if (setsockopt(iSocketServer, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(iSocketServer);
        return NULL;
    }

	tSocketServerAddr.sin_family      = AF_INET;
	tSocketServerAddr.sin_port        = htons(5002);  /* host to net, short */
 	tSocketServerAddr.sin_addr.s_addr = INADDR_ANY;
	memset(tSocketServerAddr.sin_zero, 0, 8);


	iRet = bind(iSocketServer, (const struct sockaddr *)&tSocketServerAddr, sizeof(struct sockaddr));
	if (-1 == iRet)
	{
		printf("bind error!\n");
        close(iSocketServer);
		return NULL;
	}

	iRet = listen(iSocketServer, 10);
	if (-1 == iRet)
	{
		printf("listen error!\n");
        close(iSocketServer);
		return NULL;
	}

    iAddrLen = sizeof(struct sockaddr);
                
    printf("start accepting connection\n");
    while (running) {
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);  
        FD_SET(iSocketServer, &readfds);
        iRet = select(iSocketServer+1, &readfds, NULL, NULL, &tv);
        if (iRet <= 0) {
            //printf("wait timeout\n");
            continue; //超时
        } 
        if (FD_ISSET(iSocketServer, &readfds)) {
            if (g_uart_client0 == 0) {
                g_uart_client0 = accept(iSocketServer, (struct sockaddr *)&tSocketClientAddr, (socklen_t*)&iAddrLen);
                if (g_uart_client0 ) {
                    printf("Get connect from client0: %s\n", inet_ntoa(tSocketClientAddr.sin_addr));
                } else {
                    printf("connect error!\n");
                    continue;
                }
                pthread_create(&socketThreadID, NULL, socketThread, (void*)(intptr_t)g_uart_client0);
            } else {
                g_uart_client1 = accept(iSocketServer, (struct sockaddr *)&tSocketClientAddr, (socklen_t*)&iAddrLen);
                if (g_uart_client1) {
                    printf("Get connect from client1: %s\n", inet_ntoa(tSocketClientAddr.sin_addr));
                } else {
                    printf("connect error!\n");
                    continue;
                }
                pthread_create(&socketThreadID, NULL, socketThread, (void*)(intptr_t)g_uart_client1);
            }
        } else {
            continue;
        }
	}		
    if (g_uart_client0) close(g_uart_client0);  
    if (g_uart_client1) close(g_uart_client1);   
	close(iSocketServer);
    printf("close(iSocketServer)\n");
	return NULL;

}