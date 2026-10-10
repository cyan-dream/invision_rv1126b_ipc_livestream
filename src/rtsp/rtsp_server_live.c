#include "rtsp_server_handle.h"
#include "mthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_IP   "0.0.0.0"
#define SERVER_PORT 554
#define USER        "admin"
#define PASSWORD    "123456"

#define BUFFER (2 * 1024 * 1024)
#define H264_NAL_NIDR           1
#define H264_NAL_PARTITION_A    2
#define H264_NAL_IDR            5
#define H264_NAL_SEI            6
#define H264_NAL_SPS            7
#define H264_NAL_PPS            8
#define H264_NAL_AUD            9
#define MAX_SESSIONS            16

int run_flag = 1;

struct session_arg {
    void *context;
    char file_path[1024];
};
void *contexts[MAX_SESSIONS];
/* ---------- 原有辅助函数保持不变 ---------- */
static int startCode3(uint8_t *buffer, int len){
    if(len < 3) return 0;
    return (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 1);
}
static int startCode4(uint8_t *buffer, int len){
    if(len < 4) return 0;
    return (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 1);
}
static int getStartCode(uint8_t *buffer, int len){
    if(startCode3(buffer, len)) return 3;
    if(startCode4(buffer, len)) return 4;
    return 0;
}
static uint8_t *findNextStartCode(uint8_t *buf, int len){
    int i;
    if (len < 3) return NULL;
    for (i = 0; i < len - 3; ++i) {
        if (getStartCode(buf, len)) return buf;
        ++buf;
    }
    if (startCode3(buf, len)) return buf;
    return NULL;
}
static int getFrameFromH264File(FILE *fp, uint8_t *frame, int size){
    int r_size, frame_size;
    uint8_t *next_start_code;
    if (fp == NULL) return -1;
    r_size = fread(frame, 1, size, fp);
    if(!getStartCode(frame, r_size)) return -1;
    next_start_code = findNextStartCode(frame + 3, r_size - 3);
    if (!next_start_code) {
        fseek(fp, 0, SEEK_SET);
        frame_size = r_size;
    } else {
        frame_size = (next_start_code - frame);
        fseek(fp, frame_size - r_size, SEEK_CUR);
    }
    return frame_size;
}
int h264_new_access_unit(uint8_t *buffer, int len){
    int start_code = getStartCode(buffer, len);
    if(len < (start_code + 2)) return 0;
    int nal_type = buffer[start_code] & 0x1f;
    if(H264_NAL_AUD == nal_type || H264_NAL_SPS == nal_type || H264_NAL_PPS == nal_type || H264_NAL_SEI == nal_type || (14 <= nal_type && nal_type <= 18))
        return 1;
    if(H264_NAL_NIDR == nal_type || H264_NAL_PARTITION_A == nal_type || H264_NAL_IDR == nal_type){
        return (buffer[start_code + 1] & 0x80) ? 1 : 0;
    }
    return 0;
}

/* ---------- 修改后的发送线程，参数包含 context 和文件路径 ---------- */
void *sendDataThd(void *arg){
    struct session_arg *sarg = (struct session_arg *)arg;
    void *context = sarg->context;
    const char *file = sarg->file_path;

    FILE *fp = fopen(file, "rb");
    if(fp == NULL){
        printf("file not exist: %s\n", file);
        free(sarg);
        return NULL;
    }
    uint8_t *frame = (uint8_t*)malloc(BUFFER);
    if(frame == NULL){
        printf("malloc error\n");
        fclose(fp);
        free(sarg);
        return NULL;
    }
    int frame_size = 0;
    int fps = 25;
    int start_code;
    int type;
    int ret;
    while (run_flag == 1) {
        start_code = 0;
        frame_size = getFrameFromH264File(fp, frame, BUFFER);
        if (frame_size < 0) {
            printf("read over for %s\n", file);
            break;
        }
        start_code = getStartCode(frame, frame_size);
        type = frame[start_code] & 0x1f;

        ret = sessionSendVideoData(context, frame + start_code, frame_size - start_code);
        if(ret < 0){
            printf("sessionSendVideoData error for %s\n", file);
        }
        if(type == 5 || type == 1){
            m_sleep(1000 / fps);
        }
    }
    if(frame) free(frame);
    fclose(fp);
    free(sarg);
    return NULL;
}


int h264_extract_nalu(const uint8_t *buffer, int buffer_size, int parsed_len)
{

    // 2. 初始化变量，定位当前处理起始位置
    const uint8_t *current_ptr = buffer + parsed_len;  // 当前处理起始指针
    int remain_len = buffer_size - parsed_len;         // 剩余未处理数据长度
    int nalu_size = 0;                                 // 最终返回的 NALU 大小
    int next_start_code_pos = -1;                      // 下一个起始码的相对位置（相对于 current_ptr）
    int min_start_code_len = 3;                        // 最小起始码长度（3字节）

    // 3. 若剩余数据不足3字节，无法包含有效起始码，直接返回0
    if (remain_len < min_start_code_len)
    {
        return 0;
    }

    // 4. 查找当前 NALU 之后的下一个起始码（确定当前 NALU 的边界）
    // 遍历范围：从当前位置第3字节开始，到剩余长度前3/4字节（避免越界）
    for (int i = min_start_code_len; i < remain_len - min_start_code_len; i++)
    {
        // 优先匹配 4 字节起始码 0x00000001
        if (i + 3 < remain_len &&
            current_ptr[i] == 0x00 &&
            current_ptr[i+1] == 0x00 &&
            current_ptr[i+2] == 0x00 &&
            current_ptr[i+3] == 0x01)
        {
            next_start_code_pos = i;
            break;
        }
        // 匹配 3 字节起始码 0x000001（排除 4 字节起始码的前3字节，避免误判）
        //else if (current_ptr[i] == 0x00 &&
        //         current_ptr[i+1] == 0x00 &&
        //         current_ptr[i+2] == 0x01 &&
        //         (i == min_start_code_len || current_ptr[i-1] != 0x00))
        //{
        //    next_start_code_pos = i;
        //    break;
        //}
    }

    // 5. 计算当前 NALU 的大小
    if (next_start_code_pos != -1)
    {
        // 5.1 找到下一个起始码：当前 NALU 大小 = 下一个起始码的相对位置
        nalu_size = next_start_code_pos;
    }
    else
    {
        // 5.2 未找到下一个起始码：剩余数据全部作为最后一个 NALU
        nalu_size = remain_len;
    }

    // 6. 验证 NALU 有效性（至少包含3字节起始码，避免无效数据）
    if (nalu_size < min_start_code_len)
    {
        return 0;
    }

    return nalu_size;
}
/* ---------- 主函数：支持多个 Session ---------- */
void* rtsp_server_thread(void *arg)
{

    int auth = 0;

    int ret = rtspModuleInit();
    if(ret < 0){
        printf("rtspModuleInit error\n");
        return NULL;
    }

    extern int is_h265;
    mthread_t tids[MAX_SESSIONS];
    int session_count = 0;

    for(int i = 0; i < 1; i++){
        char *session_name = "live";

        void *context = rtspAddSession(session_name);
        if(context == NULL){
            printf("rtspAddSession error for %s\n", session_name);
            continue;
        }
        if (is_h265) ret = sessionAddVideo(context, VIDEO_H265);
        else ret = sessionAddVideo(context, VIDEO_H264);
        if(ret < 0){
            printf("sessionAddVideo error for %s\n", session_name);
            rtspDelSession(context);
            continue;
        }

        printf("Created session: rtsp://%s:%d/%s\n", SERVER_IP, SERVER_PORT, session_name);

        struct session_arg *sarg = (struct session_arg *)malloc(sizeof(struct session_arg));
        sarg->context = context;
        //strncpy(sarg->file_path, file_path, sizeof(sarg->file_path) - 1);

        // ret = mthread_create(&tids[session_count], NULL, sendDataThd, sarg);
        // if(ret < 0){
        //     printf("sendDataThd mthread_create() failed for %s\n", session_name);
        //     free(sarg);
        //     rtspDelSession(context);
        //     continue;
        // }
        // mthread_detach(tids[session_count]);
        contexts[session_count] = context;
        session_count++;
    }

    if(session_count == 0){
        printf("No valid session created.\n");
        rtspModuleDel();
        return NULL;
    }

    printf("RTSP server started with %d sessions\n", session_count);
    ret = rtspStartServer(auth, SERVER_IP, SERVER_PORT, USER, PASSWORD);
    if(ret < 0){
        printf("rtspStartServer error\n");
    }

    run_flag = 0;
    rtspStopServer();
    for(int i = 0; i < session_count; i++){
        rtspDelSession(contexts[i]);
    }
    rtspModuleDel();
    return NULL;
}