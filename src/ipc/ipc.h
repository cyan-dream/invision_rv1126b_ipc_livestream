#ifndef  IPC_H
#define  IPC_H
#include "kv_sqlite3.h"
struct area_coordinate {
    int ltx;
    int lty;
    int rbx;
    int rby;
};

#define MAX_OCCLUSION_NUM 4
#define PANTILT_UART "/dev/ttyS9"
struct area_invasion {
    int state;
    int photo;
    int video;
    int video_duration;
    int sensitivity;
    struct area_coordinate coordinate;
    int mode;
    int remain;
    int remain_time;
};

struct tripwire_invasion {
    int state;
    int photo;
    int video;
    int video_duration;
    int sensitivity;
    struct area_coordinate coordinate;
    int mode;  
};


struct ipc_config {
    kv_t kv;
    int onvif_state;
    int onvif_authentication;
    int onvif_port;
    int timezone;
    int ptz_addr;
    int ptz_buad;
    int ptz_reset;
    int bitrate;
    int width;
    int height;
    int fps;
    int encode;
    int motion_detection;
    int detection_photo;
    int detection_video;
    int video_duration;
    int detection_ltx;
    int detection_lty;
    int detection_rbx;
    int detection_rby;
    int sensitivity;
    int occlusion_num;
    struct area_coordinate occlusion_coordinate[4];
    struct area_invasion area_invasion;
    struct tripwire_invasion tripwire_invasion; 

    int osd_state;
    char osd_text[32];

    char ftp_url[64]; //ftp://192.168.5.19/test/
    char ftp_username[32];
    char ftp_password[32];
    int ftp_upload;//0-bit photo, 1-bit video
};
#ifdef __cplusplus
extern "C" {
#endif

int start_onvif(char *ip);
int modify_ipc_onvif_config_file(int state, int authentication, int port);
int modify_ipc_ptz_config_file(int addr, int buad, int reset);
int get_eth_ip(const char *interface, char *ip);
int get_date(char *date, int format);
int set_timezone(int timezone);
int init_ipc_config(void);
int modify_ipc_encode_config_file(int bitrate, int width, int height, int fps);
int modify_ipc_motion_detection_config_file(int state, int detection_photo, int detection_video, int video_duration);
int modify_ipc_detection_range_config_file(int sensitivity, int detection_ltx, int detection_lty, int detection_rbx, int detection_rby);
int modify_ipc_area_occlusion_config_file(int occlusion_num,
                                            struct area_coordinate *occlusion_coordinate);
int modify_ipc_area_invasion_state(int state, int photo, int video, int video_duration);
int modify_ipc_area_invasion_config(int sensitivity, int mode, int remain, int remain_time, 
                                        struct area_coordinate coordinate);
int modify_ipc_tripwire_invasion_config(int sensitivity, int mode, struct area_coordinate coordinate);
int modify_ipc_tripwire_invasion_state(int state, int photo, int video, int video_duration);

int modify_ipc_osd_config(int state, char *text);
int modify_ipc_ftp_config(char *url, char *username, char *password, int upload);
int start_onvif(char *ip);
#ifdef __cplusplus
}
#endif
#endif // ! IPC_H
