#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENABLE_AUDIO 0
#define VIDEO_FPS 25
#define VIDEO_TIMESCALE 90000
#define MAX_FRAME_BUFFER (1024 * 1024)
#define MAX_AU_BUF (1024 * 1024) // AU合并缓存1MB

static int write_callback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return (int)(fwrite(buffer, 1, size, f) != size);
}

static inline int startCode3(const char* buf)
{
    return (buf[0] == 0 && buf[1] == 0 && buf[2] == 1) ? 1 : 0;
}

static inline int startCode4(const char* buf)
{
    return (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1) ? 1 : 0;
}

static char* findNextStartCode(char* buf, int len)
{
    if (len < 3)
        return NULL;
    int i;
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

// 读取单个NAL（带起始码），返回NAL总长度
static int get_single_nal(FILE* fp, char* frame, int size)
{
    int rSize = (int)fread(frame, 1, size, fp);
    if (rSize == 0)
        return -1;
    if (!startCode3(frame) && !startCode4(frame))
        return -1;

    char* nextStartCode = findNextStartCode(frame + 3, rSize - 3);
    int nalSize;
    if (!nextStartCode)
    {
        nalSize = rSize;
    }
    else
    {
        nalSize = (int)(nextStartCode - frame);
        fseek(fp, nalSize - rSize, SEEK_CUR);
    }
    return nalSize;
}

// 剥离起始码，返回NAL数据指针，输出起始码长度
static uint8_t* strip_startcode(uint8_t* buf, int buflen, int* sc_len)
{
    if (buflen >= 4 && buf[0]==0 && buf[1]==0 && buf[2]==0 && buf[3]==1)
    {
        *sc_len = 4;
        return buf + 4;
    }
    else if (buflen >= 3 && buf[0]==0 && buf[1]==0 && buf[2]==1)
    {
        *sc_len = 3;
        return buf + 3;
    }
    *sc_len = 0;
    return buf;
}

static int is_sps(uint8_t hdr) { return (hdr & 0x1F) == 7; }
static int is_pps(uint8_t hdr) { return (hdr & 0x1F) == 8; }
static int is_idr(uint8_t hdr) { return (hdr & 0x1F) == 5; }
static int is_aud(uint8_t hdr) { return (hdr & 0x1F) == 9; }
// 判断是否VCL切片NAL（帧起始标记）
static int is_vcl_slice(uint8_t hdr)
{
    uint8_t t = hdr & 0x1F;
    // type1/2/3/4 普通slice; type5 IDR slice
    return t ==1 || t==2 || t==3 || t==4 || t==5;
}

// 把单个NAL（无起始码）追加到AU缓存，格式：4字节长度 + NAL数据
static int append_nal_to_au(uint8_t *au_buf, int *au_len, const uint8_t *nal, int nal_len)
{
    if ((*au_len) + 4 + nal_len > MAX_AU_BUF)
        return 0;
    uint32_t be_len = (uint32_t)nal_len;
    au_buf[*au_len + 0] = (be_len >> 24) & 0xFF;
    au_buf[*au_len + 1] = (be_len >> 16) & 0xFF;
    au_buf[*au_len + 2] = (be_len >> 8) & 0xFF;
    au_buf[*au_len + 3] = be_len & 0xFF;
    memcpy(au_buf + 4 + (*au_len), nal, nal_len);
    *au_len += 4 + nal_len;
    return 1;
}

// 输出当前AU为MP4 sample
static void flush_au(MP4E_mux_t *mux, int track_id, uint8_t *au_buf, int *au_len, uint32_t frame_dur, uint64_t *pts)
{
    if (*au_len <=0) return;
    int is_key = 0;
    int off = 0;
    while (off < *au_len)
    {
        uint32_t l = (au_buf[off] <<24)|(au_buf[off+1]<<16)|(au_buf[off+2]<<8)|au_buf[off+3];
        uint8_t ntype = au_buf[off+4] & 0x1F;
        if (ntype ==5) { is_key=1; break; }
        off += 4 + l;
    }
    int kind = is_key ? MP4E_SAMPLE_RANDOM_ACCESS : MP4E_SAMPLE_DEFAULT;
    MP4E_put_sample(mux, track_id, au_buf, *au_len, frame_dur, kind);
    *pts += frame_dur;
    *au_len = 0;
}

static int stream_h264_to_mp4(FILE *h264_file, FILE *mp4_file,
                              int sequential_mode, int fragmentation_mode,
                              int w, int h)
{
    unsigned char* nal_buf = (unsigned char*)malloc(MAX_FRAME_BUFFER);
    unsigned char* au_buf = (unsigned char*)malloc(MAX_AU_BUF);
    if (!nal_buf || !au_buf)
    {
        printf("malloc buffer failed\n");
        free(nal_buf);
        free(au_buf);
        return -1;
    }

    MP4E_mux_t *mux = MP4E_open(sequential_mode, fragmentation_mode, mp4_file, write_callback);
    if (!mux)
    {
        printf("MP4E_open fail\n");
        goto cleanup;
    }

    MP4E_track_t track_info = {0};
    track_info.track_media_kind = e_video;
    track_info.object_type_indication = MP4_OBJECT_TYPE_AVC;
    track_info.time_scale = VIDEO_TIMESCALE;
    track_info.default_duration = VIDEO_TIMESCALE / VIDEO_FPS;
    track_info.u.v.width = w;
    track_info.u.v.height = h;
    track_info.language[0] = 'u';
    track_info.language[1] = 'n';
    track_info.language[2] = 'd';

    int track_id = MP4E_add_track(mux, &track_info);
    if (track_id < 0)
    {
        printf("MP4E_add_track fail\n");
        goto cleanup;
    }

    uint8_t sps_cache[2048] = {0};
    uint8_t pps_cache[2048] = {0};
    int sps_len = 0, pps_len = 0;

    uint64_t pts = 0;
    const uint32_t frame_dur = VIDEO_TIMESCALE / VIDEO_FPS;
    int au_len = 0;    // 当前AU缓存有效长度
    int ret = -1;

    while (1)
    {
        // 读取一个完整NAL（带起始码）
        int nal_raw_size = get_single_nal(h264_file, (char*)nal_buf, MAX_FRAME_BUFFER);
        if (nal_raw_size < 0)
        {
            // 文件读完，输出剩余AU
            //flush_au(mux, track_id, au_buf, &au_len, frame_dur, &pts);
            printf("H264读取完成\n");
            break;
        }

        // 剥离起始码，得到纯NAL数据
        int sc_len = 0;
        uint8_t *nal_data = strip_startcode(nal_buf, nal_raw_size, &sc_len);
        int nal_len = nal_raw_size - sc_len;
        if (nal_len <= 0) continue;
        uint8_t nalu_hdr = nal_data[0];

        // 缓存SPS/PPS用于avcC
        if (is_sps(nalu_hdr))
        {
            if (nal_len < (int)sizeof(sps_cache))
            {
                memcpy(sps_cache, nal_data, nal_len);
                sps_len = nal_len;
            }
        }
        if (is_pps(nalu_hdr))
        {
            if (nal_len < (int)sizeof(pps_cache))
            {
                memcpy(pps_cache, nal_data, nal_len);
                pps_len = nal_len;
            }
        }

        // 兼容AUD（有则刷新AU）
        if (is_aud(nalu_hdr))
        {
            flush_au(mux, track_id, au_buf, &au_len, frame_dur, &pts);
            continue;
        }

        // 核心：VCL切片=新帧起点，先输出旧AU
        if (is_vcl_slice(nalu_hdr))
        {
            append_nal_to_au(au_buf, &au_len, nal_data, nal_len);
            flush_au(mux, track_id, au_buf, &au_len, frame_dur, &pts);
            continue;
        }

        // SPS/PPS/SEI等非VCL，追加到当前帧AU
        append_nal_to_au(au_buf, &au_len, nal_data, nal_len);
    }

    // 写入SPS/PPS到avcC盒子
    if (sps_len > 0 && pps_len > 0)
    {
        MP4E_set_sps(mux, track_id, sps_cache, sps_len);
        MP4E_set_pps(mux, track_id, pps_cache, pps_len);
    }
    ret = 0;

cleanup:
    free(nal_buf);
    free(au_buf);
    MP4E_close(mux);
    return ret;
}

int h264_to_mp4(char *h264file, char *mp4file)
{
    int sequential_mode = 1;
    int fragmentation_mode = 0;

    FILE *fin = fopen(h264file, "rb");
    if (!fin)
    {
        printf("无法打开输入H264文件\n");
        return -1;
    }

    FILE *fout = fopen(mp4file, "wb");
    if (!fout)
    {
        printf("无法打开输出MP4文件\n");
        fclose(fin);
        return -1;
    }

    // 替换为视频实际宽高
    int ret = stream_h264_to_mp4(fin, fout, sequential_mode, fragmentation_mode, 720, 576);

    fclose(fin);
    fclose(fout);
    return ret;
}


/* ===================== H.265 / HEVC 支持 ===================== */

/* H.265 NAL type: 取第 1 个字节的高 6 位 */
static int h265_nal_type(uint8_t hdr0)        { return (hdr0 >> 1) & 0x3F; }
static int h265_is_vps(uint8_t hdr0)          { return h265_nal_type(hdr0) == 32; }
static int h265_is_sps(uint8_t hdr0)          { return h265_nal_type(hdr0) == 33; }
static int h265_is_pps(uint8_t hdr0)          { return h265_nal_type(hdr0) == 34; }
static int h265_is_aud(uint8_t hdr0)          { return h265_nal_type(hdr0) == 35; }

/* VCL: 0-9 (TRAIL/TSA/STSA/RADL/RASL) 与 16-21 (BLA/IDR/CRA) */
static int h265_is_vcl_slice(uint8_t hdr0)
{
    int t = h265_nal_type(hdr0);
    return (t <= 9) || (t >= 16 && t <= 21);
}

/* 关键帧: IDR_W_RADL(19)、IDR_N_LP(20)、CRA_NUT(21) */
static int h265_is_key_frame(uint8_t hdr0)
{
    int t = h265_nal_type(hdr0);
    return (t == 19 || t == 20 || t == 21);
}

/* 输出当前 AU 为 MP4 sample（H.265 版） */
static void flush_au_hevc(MP4E_mux_t *mux, int track_id,
                          uint8_t *au_buf, int *au_len,
                          uint32_t frame_dur, uint64_t *pts)
{
    if (*au_len <= 0) return;

    int is_key = 0;
    int off = 0;
    while (off + 4 <= *au_len)
    {
        uint32_t l = (au_buf[off] << 24) | (au_buf[off + 1] << 16) |
                     (au_buf[off + 2] << 8) | au_buf[off + 3];
        if ((int)(off + 4 + l) > *au_len) break;
        if (l >= 2 && h265_is_key_frame(au_buf[off + 4])) { is_key = 1; break; }
        off += 4 + (int)l;
    }

    int kind = is_key ? MP4E_SAMPLE_RANDOM_ACCESS : MP4E_SAMPLE_DEFAULT;
    MP4E_put_sample(mux, track_id, au_buf, *au_len, frame_dur, kind);
    *pts += frame_dur;
    *au_len = 0;
}

static int stream_h265_to_mp4(FILE *h265_file, FILE *mp4_file,
                              int sequential_mode, int fragmentation_mode,
                              int w, int h)
{
    unsigned char *nal_buf = (unsigned char *)malloc(MAX_FRAME_BUFFER);
    unsigned char *au_buf  = (unsigned char *)malloc(MAX_AU_BUF);
    if (!nal_buf || !au_buf)
    {
        printf("malloc buffer failed\n");
        free(nal_buf);
        free(au_buf);
        return -1;
    }

    MP4E_mux_t *mux = MP4E_open(sequential_mode, fragmentation_mode,
                                mp4_file, write_callback);
    if (!mux)
    {
        printf("MP4E_open fail\n");
        goto cleanup;
    }

    MP4E_track_t track_info = {0};
    track_info.track_media_kind      = e_video;
    track_info.object_type_indication = MP4_OBJECT_TYPE_HEVC; /* H.265 */
    track_info.time_scale            = VIDEO_TIMESCALE;
    track_info.default_duration      = VIDEO_TIMESCALE / VIDEO_FPS;
    track_info.u.v.width             = w;
    track_info.u.v.height            = h;
    track_info.language[0] = 'u';
    track_info.language[1] = 'n';
    track_info.language[2] = 'd';

    int track_id = MP4E_add_track(mux, &track_info);
    if (track_id < 0)
    {
        printf("MP4E_add_track fail\n");
        goto cleanup;
    }

    uint8_t vps_cache[2048] = {0};
    uint8_t sps_cache[2048] = {0};
    uint8_t pps_cache[2048] = {0};
    int vps_len = 0, sps_len = 0, pps_len = 0;

    uint64_t pts = 0;
    const uint32_t frame_dur = VIDEO_TIMESCALE / VIDEO_FPS;
    int au_len = 0;
    int ret = -1;

    while (1)
    {
        /* 读取一个带起始码的 NAL */
        int nal_raw_size = get_single_nal(h265_file, (char *)nal_buf, MAX_FRAME_BUFFER);
        if (nal_raw_size < 0)
        {
            printf("H265读取完成\n");
            break;
        }

        int sc_len = 0;
        uint8_t *nal_data = strip_startcode(nal_buf, nal_raw_size, &sc_len);
        int nal_len = nal_raw_size - sc_len;
        if (nal_len < 2) continue;  /* H.265 NAL 头至少 2 字节 */

        uint8_t nalu_hdr0 = nal_data[0];

        /* 缓存 VPS/SPS/PPS 用于 hvcC */
        if (h265_is_vps(nalu_hdr0) && nal_len < (int)sizeof(vps_cache))
        {
            memcpy(vps_cache, nal_data, nal_len);
            vps_len = nal_len;
        }
        if (h265_is_sps(nalu_hdr0) && nal_len < (int)sizeof(sps_cache))
        {
            memcpy(sps_cache, nal_data, nal_len);
            sps_len = nal_len;
        }
        if (h265_is_pps(nalu_hdr0) && nal_len < (int)sizeof(pps_cache))
        {
            memcpy(pps_cache, nal_data, nal_len);
            pps_len = nal_len;
        }

        /* AUD：表示一个 AU 结束，刷新 */
        if (h265_is_aud(nalu_hdr0))
        {
            flush_au_hevc(mux, track_id, au_buf, &au_len, frame_dur, &pts);
            continue;
        }

        /* VCL 切片：遇到即表示当前 AU 完成，写入后开新 AU */
        if (h265_is_vcl_slice(nalu_hdr0))
        {
            append_nal_to_au(au_buf, &au_len, nal_data, nal_len);
            flush_au_hevc(mux, track_id, au_buf, &au_len, frame_dur, &pts);
            continue;
        }

        /* 其余（SEI/参数集等）追加到当前 AU */
        append_nal_to_au(au_buf, &au_len, nal_data, nal_len);
    }

    /* 写 VPS/SPS/PPS 到 hvcC */
    if (vps_len > 0 && sps_len > 0 && pps_len > 0)
    {
        MP4E_set_vps(mux, track_id, vps_cache, vps_len);
        MP4E_set_sps(mux, track_id, sps_cache, sps_len);
        MP4E_set_pps(mux, track_id, pps_cache, pps_len);
    }
    ret = 0;

cleanup:
    free(nal_buf);
    free(au_buf);
    MP4E_close(mux);
    return ret;
}

int h265_to_mp4(char *h265file, char *mp4file)
{
    int sequential_mode    = 1;
    int fragmentation_mode = 0;

    FILE *fin = fopen(h265file, "rb");
    if (!fin)
    {
        printf("无法打开输入H265文件\n");
        return -1;
    }

    FILE *fout = fopen(mp4file, "wb");
    if (!fout)
    {
        printf("无法打开输出MP4文件\n");
        fclose(fin);
        return -1;
    }

    /* 替换为实际视频宽高 */
    int ret = stream_h265_to_mp4(fin, fout,
                                 sequential_mode, fragmentation_mode,
                                 720, 576);

    fclose(fin);
    fclose(fout);
    return ret;
}