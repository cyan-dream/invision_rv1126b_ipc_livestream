#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
#define ENABLE_AUDIO 0

#define VIDEO_FPS 25
// 流式读取缓冲区大小（可根据需求调整，建议4KB~64KB）
#define MAX_FRAME_BUFFER (1024 * 1024)

static ssize_t get_nal_size(uint8_t *buf, ssize_t size)
{
    ssize_t pos = 3;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
            return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
            return pos;
        pos++;
    }
    return size;
}

static int write_callback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

typedef struct
{
    uint8_t *buffer;
    ssize_t size;
} INPUT_BUFFER;

// static int read_callback(int64_t offset, void *buffer, size_t size, void *token)
// {
//     INPUT_BUFFER *buf = (INPUT_BUFFER*)token;
//     size_t to_copy = MINIMP4_MIN(size, buf->size - offset - size);
//     memcpy(buffer, buf->buffer + offset, to_copy);
//     return to_copy != size;
// }

// int demux(uint8_t *input_buf, ssize_t input_size, FILE *fout, int ntrack)
// {
//     int /*ntrack, */i, spspps_bytes;
//     const void *spspps;
//     INPUT_BUFFER buf = { input_buf, input_size };
//     MP4D_demux_t mp4 = { 0, };
//     MP4D_open(&mp4, read_callback, &buf, input_size);

//     //for (ntrack = 0; ntrack < mp4.track_count; ntrack++)
//     {
//         MP4D_track_t *tr = mp4.track + ntrack;
//         unsigned sum_duration = 0;
//         i = 0;
//         if (tr->handler_type == MP4D_HANDLER_TYPE_VIDE)
//         {   // assume h264
// #define USE_SHORT_SYNC 0
//             char sync[4] = { 0, 0, 0, 1 };
//             while (spspps = MP4D_read_sps(&mp4, ntrack, i, &spspps_bytes))
//             {
//                 fwrite(sync + USE_SHORT_SYNC, 1, 4 - USE_SHORT_SYNC, fout);
//                 fwrite(spspps, 1, spspps_bytes, fout);
//                 i++;
//             }
//             i = 0;
//             while (spspps = MP4D_read_pps(&mp4, ntrack, i, &spspps_bytes))
//             {
//                 fwrite(sync + USE_SHORT_SYNC, 1, 4 - USE_SHORT_SYNC, fout);
//                 fwrite(spspps, 1, spspps_bytes, fout);
//                 i++;
//             }
//             for (i = 0; i < mp4.track[ntrack].sample_count; i++)
//             {
//                 unsigned frame_bytes, timestamp, duration;
//                 MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
//                 uint8_t *mem = input_buf + ofs;
//                 sum_duration += duration;
//                 while (frame_bytes)
//                 {
//                     uint32_t size = ((uint32_t)mem[0] << 24) | ((uint32_t)mem[1] << 16) | ((uint32_t)mem[2] << 8) | mem[3];
//                     size += 4;
//                     mem[0] = 0; mem[1] = 0; mem[2] = 0; mem[3] = 1;
//                     fwrite(mem + USE_SHORT_SYNC, 1, size - USE_SHORT_SYNC, fout);
//                     if (frame_bytes < size)
//                     {
//                         printf("error: demux sample failed\n");
//                         exit(1);
//                     }
//                     frame_bytes -= size;
//                     mem += size;
//                 }
//             }
//         } else if (tr->handler_type == MP4D_HANDLER_TYPE_SOUN)
//         {   // assume aac
//             for (i = 0; i < mp4.track[ntrack].sample_count; i++)
//             {
//                 unsigned frame_bytes, timestamp, duration;
//                 MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
//                 printf("ofs=%d frame_bytes=%d timestamp=%d duration=%d\n", (unsigned)ofs, frame_bytes, timestamp, duration);

//             }
//         }
//     }

//     MP4D_close(&mp4);
//     if (input_buf)
//         free(input_buf);
//     return 0;
// }

static inline int startCode3(char* buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

static inline int startCode4(char* buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}

static char* findNextStartCode(char* buf, int len)
{
    int i;

    if (len < 3)
        return NULL;

    for (i = 0; i < len - 3; ++i)
    {
        if (startCode3(buf) || startCode4(buf))
            return buf;

        ++buf;
    }

    if (startCode3(buf))
        return buf;

    return NULL;
}
static int getFrameFromH264File(FILE* fp, char* frame, int size) {
    int rSize, frameSize;
    char* nextStartCode;

    if (fp == NULL)
        return -1;

    rSize = fread(frame, 1, size, fp);
    if (rSize == 0)
        return -1;

    if (!startCode3(frame) && !startCode4(frame))
        return -1;

    nextStartCode = findNextStartCode(frame + 3, rSize - 3);
    if (!nextStartCode){
        //fseek(fp, 0, SEEK_SET);
        frameSize = rSize;
    } else {
        frameSize = (nextStartCode - frame);
        fseek(fp, frameSize - rSize, SEEK_CUR);

    }

    return frameSize;
}



// 流式读取H264文件并解析NALU
static int stream_h264_to_mp4(FILE *h264_file, FILE *mp4_file, int sequential_mode, int fragmentation_mode, int is_hevc)
{
    int frameSize, startCode;
    unsigned char* frame = (unsigned char*)malloc(MAX_FRAME_BUFFER);

    int ret = 1;                // 函数返回状态（默认失败）

    if (!frame)
    {
        printf("error: malloc stream buffer failed\n");
        return ret;
    }

    // 初始化MP4复用器
    MP4E_mux_t *mux = MP4E_open(sequential_mode, fragmentation_mode, mp4_file, write_callback);
    mp4_h26x_writer_t mp4wr;
    if (MP4E_STATUS_OK != mp4_h26x_write_init(&mp4wr, mux, 720, 576, is_hevc))
    {
        printf("error: mp4_h26x_write_init failed\n");
        goto cleanup;
    }
    while (1) {
        frameSize = getFrameFromH264File(h264_file, (char *)frame, MAX_FRAME_BUFFER);
        if (frameSize < 0)
        {
            printf("读取h264文件结束\n");
            break;   
        }
        //  写入当前NALU到MP4
        if (MP4E_STATUS_OK != mp4_h26x_write_nal(&mp4wr, frame, frameSize, 90000/VIDEO_FPS))
        {
            printf("error: mp4_h26x_write_nal failed\n");
            goto cleanup;
        }
    }


    ret = 0; // 执行成功

cleanup:
    // 释放资源
    free(frame);
    MP4E_close(mux);
    return ret;
}

int h264_to_mp4(char *h264file, char *mp4file)
{
    // check switches
    int sequential_mode = 1;
    //int sequential_mode = 0;
    int fragmentation_mode = 0;
    int do_demux = 0;

    // 打开输入输出文件
    FILE *fin = fopen(h264file, "rb");
    if (!fin)
    {
        printf("error: can't open input file\n");
        exit(1);
    }

    FILE *fout = fopen(mp4file, "wb");
    if (!fout)
    {
        printf("error: can't open output file\n");
        fclose(fin);
        exit(1);
    }

    int ret = 0;

    // 复用逻辑：使用流式读取
    ret = stream_h264_to_mp4(fin, fout, sequential_mode, fragmentation_mode, 0);

    // 关闭文件
    fclose(fin);
    fclose(fout);

    return ret;
}