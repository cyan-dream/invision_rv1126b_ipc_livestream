/*
 * ws_live.c
 *
 * 与编码器同进程的 WebSocket 实时推流模块。
 * 依赖 libwebsockets 4.x。
 *
 * 编译：
 *   gcc -std=c99 -O2 -Wall ws_live.c encoder.c -o encoder \
 *       -lwebsockets -lpthread
 */

#include "ws_live.h"
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#define RING_CAPACITY      30            /* 环形缓冲区可保存的 NAL 数量 */
#define MAX_NAL_SIZE       (300 * 1024)   /* 单个 NAL 最大长度 */
//#define POLL_INTERVAL_US   30000           /* 无数据时的轮询间隔 */
#define LAG_THRESHOLD      (RING_CAPACITY / 2)
#define MAX_CLIENTS        4            /* 允许的最大并发连接数 */

/* ---------- 环形缓冲区里的一个 NAL ---------- */
typedef struct {
    unsigned char *data;
    int            len;
    int            type;
} RingNal;

/* ---------- 每个连接私有数据 ---------- */
typedef struct {
    unsigned int   read_seq;      /* 下一个要读取的全局序号 */
    int            started;       /* 是否已经定位到 SPS */
    //unsigned char *sendbuf;       /* 预分配，含 LWS_PRE */
    //size_t         sendbuf_size;
} per_session_data;

/* ---------- 服务器对象 ---------- */
struct ws_live_server {
    RingNal          ring[RING_CAPACITY];
    unsigned int     next_seq;      /* 下一个写入的全局序号 */
    pthread_mutex_t  lock;

    struct lws_context *ctx;
    pthread_t          thread;
    volatile int       running;
    int                port;

    /* 活跃连接列表。
     * 只在 lws 线程里被读写（ESTABLISHED / CLOSED / EVENT_WAIT_CANCELLED
     * 都运行在同一个 lws 线程上），因此无需加锁。 */
    struct lws        *clients[MAX_CLIENTS];
    int                client_n;
};

static struct ws_live_server *g_server = NULL;

static volatile int g_client_count = 0;

int ws_live_client_count(void) { return g_client_count; }

/* ---------- 向前查找最近的 SPS ---------- */
static int find_recent_sps(struct ws_live_server *svr, uint32_t *sps_seq)
{
    uint32_t n = svr->next_seq;
    if (n == 0) return -1;                       

    for (uint32_t i = 0; i < RING_CAPACITY; i++) {
        if (i >= n) break;                       
        uint32_t seq = n - 1 - i;
        RingNal *r = &svr->ring[seq % RING_CAPACITY];
        if (!r->data) break;
        if (r->type == 7 || r->type == 32) {
            *sps_seq = seq;
            return 0;
        }
    }
    return -1;
}

/* ---------- 向前查找最近的 IDR ---------- */
static uint32_t find_recent_idr(struct ws_live_server *svr, uint32_t not_before)
{
    uint32_t n = svr->next_seq;
    if (n == 0) return not_before;

    for (uint32_t i = 0; i < RING_CAPACITY; i++) {
        if (i >= n) break;
        uint32_t seq = n - 1 - i;
        RingNal *r = &svr->ring[seq % RING_CAPACITY];
        if (!r->data) break;
        if (r->type == 5 || r->type == 19) {
            if (seq >= not_before) return seq;
            break;
        }
    }
    return not_before;
}

/* ---------- libwebsockets 回调 ---------- */
static int ws_callback(struct lws *wsi,
                       enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    (void)in; (void)len;
    per_session_data *psd = (per_session_data *)user;
    struct ws_live_server *svr = g_server;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED:
        if (!svr) return -1;
        if (svr->client_n >= MAX_CLIENTS) {
            fprintf(stderr, "[WS] too many clients, rejecting\n");
            return -1;
        }
        svr->clients[svr->client_n++] = wsi;

        psd->started      = 0;
        psd->read_seq     = 0;
        // psd->sendbuf_size = (size_t)LWS_PRE + MAX_NAL_SIZE;
        // psd->sendbuf      = (unsigned char *)malloc(psd->sendbuf_size);
        // if (!psd->sendbuf) {
        //     fprintf(stderr, "[WS] malloc sendbuf fail\n");
        //     return -1;
        // }

        fprintf(stderr, "[WS] new client connected (total=%d)\n",
                svr->client_n);
        lws_callback_on_writable(wsi);
        g_client_count++;
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        //if (!svr || !psd->sendbuf) return -1;
        if (!svr) return -1;

        pthread_mutex_lock(&svr->lock);

        /* 先等 SPS */
        if (!psd->started) {
            uint32_t sps_seq = 0;
            int ret = find_recent_sps(svr, &sps_seq);
            if (ret >= 0) {
                psd->read_seq = sps_seq;
                psd->started  = 1;
                fprintf(stderr, "[WS] locate SPS at seq=%d\n", sps_seq);
            } else {
                pthread_mutex_unlock(&svr->lock);
                //lws_set_timer_usecs(wsi, POLL_INTERVAL_US);
                break;
            }
        }

        /* 落后太多：跳到最近的 I 帧 */
        if (svr->next_seq - psd->read_seq > LAG_THRESHOLD) {
            uint32_t idr_seq = find_recent_idr(svr, svr->next_seq - LAG_THRESHOLD);
            if (idr_seq > psd->read_seq) {
                fprintf(stderr, "[WS] client lag %d -> %d\n",
                        psd->read_seq, idr_seq);
                psd->read_seq = idr_seq;
            }
        }

        /* 没有新数据，挂起等待 */
        if (psd->read_seq >= svr->next_seq) {
            pthread_mutex_unlock(&svr->lock);
            //lws_set_timer_usecs(wsi, POLL_INTERVAL_US);
            break;
        }

        RingNal *r = &svr->ring[psd->read_seq % RING_CAPACITY];
        if (!r->data || r->len <= 0 || r->len > MAX_NAL_SIZE) {
            pthread_mutex_unlock(&svr->lock);
            //lws_set_timer_usecs(wsi, POLL_INTERVAL_US);
            break;
        }

        int nalu_len    = r->len;
        //memcpy(psd->sendbuf + LWS_PRE, r->data, (size_t)len);
        psd->read_seq++;

        //pthread_mutex_unlock(&svr->lock);

        //int n = lws_write(wsi, psd->sendbuf + LWS_PRE,
        //                  (size_t)len, LWS_WRITE_BINARY);

        int n = lws_write(wsi, r->data + LWS_PRE,
                          (size_t)nalu_len, LWS_WRITE_BINARY);   
        pthread_mutex_unlock(&svr->lock);                                 
        if (n < 0) {
            fprintf(stderr, "[WS] lws_write fail: %d\n", n);
            return -1;
        }

        /* 立刻请求下一次可写，让 libwebsockets 尽快调度 */
        lws_callback_on_writable(wsi);
        break;
    }

    // case LWS_CALLBACK_TIMER:
    //     lws_callback_on_writable(wsi);
    //     break;

    case LWS_CALLBACK_CLOSED:
        // if (psd->sendbuf) {
        //     free(psd->sendbuf);
        //     psd->sendbuf = NULL;
        //     psd->sendbuf_size = 0;
        // }
        if (svr) {
            for (int i = 0; i < svr->client_n; i++) {
                if (svr->clients[i] == wsi) {
                    svr->clients[i] = svr->clients[--svr->client_n];
                    g_client_count--;
                    break;
                }
            }
            fprintf(stderr, "[WS] client closed (total=%d)\n",
                    svr->client_n);
        }
        break;
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        if (!svr) break;
        for (int i = 0; i < svr->client_n; i++)
            lws_callback_on_writable(svr->clients[i]);
        break;    
    default:
        break;
    }
    return 0;
}

/* ---------- libwebsockets 线程 ---------- */
static void *lws_thread_fn(void *arg)
{
    struct ws_live_server *svr = (struct ws_live_server *)arg;

    struct lws_protocols protocols[3];
    memset(protocols, 0, sizeof(protocols));

    protocols[0].name                  = "";     /* ws://host:port */
    protocols[0].callback              = ws_callback;
    protocols[0].per_session_data_size = sizeof(per_session_data);
    protocols[0].rx_buffer_size        = 4096;

    protocols[1].name                  = "ws";   /* ws://host:port/ws */
    protocols[1].callback              = ws_callback;
    protocols[1].per_session_data_size = sizeof(per_session_data);
    protocols[1].rx_buffer_size        = 4096;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = svr->port;
    info.protocols = protocols;
    info.gid       = -1;
    info.uid       = -1;
    info.options   = LWS_SERVER_OPTION_VALIDATE_UTF8
                   | LWS_SERVER_OPTION_DISABLE_IPV6;

    svr->ctx = lws_create_context(&info);
    if (!svr->ctx) {
        fprintf(stderr, "[WS] lws_create_context failed\n");
        return NULL;
    }

    fprintf(stderr, "[WS] listening on ws://<ip>:%d\n", svr->port);

    while (svr->running) {
        lws_service(svr->ctx, 20);
    }

    lws_context_destroy(svr->ctx);
    svr->ctx = NULL;
    return NULL;
}

/* ---------- 对外 API ---------- */
ws_live_server *ws_live_start(int port)
{
    if (g_server) return g_server;

    struct ws_live_server *svr = (struct ws_live_server *)calloc(1, sizeof(*svr));
    if (!svr) return NULL;

    svr->port     = port;
    svr->next_seq = 0;
    svr->running  = 1;
    pthread_mutex_init(&svr->lock, NULL);

    g_server = svr;

    if (pthread_create(&svr->thread, NULL, lws_thread_fn, svr) != 0) {
        fprintf(stderr, "[WS] pthread_create failed\n");
        pthread_mutex_destroy(&svr->lock);
        free(svr);
        g_server = NULL;
        return NULL;
    }
    return svr;
}

void ws_live_push(ws_live_server *svr,
                  const unsigned char *data, int len,
                  int nal_type)
{
    if (!svr || !data || len <= 0 || len > MAX_NAL_SIZE) return;

    pthread_mutex_lock(&svr->lock);

    uint32_t slot  = svr->next_seq % RING_CAPACITY;
    RingNal *r = &svr->ring[slot];

    if (!r->data) {
        r->data = (unsigned char *)malloc(MAX_NAL_SIZE);
        if (!r->data) {
            pthread_mutex_unlock(&svr->lock);
            fprintf(stderr, "[WS] malloc %d fail\n", MAX_NAL_SIZE);
            return;
        }
    }
    
    memcpy(r->data+LWS_PRE, data, (size_t)len);
    r->len    = len;
    r->type   = nal_type;

    svr->next_seq++;

    pthread_mutex_unlock(&svr->lock);
    if (svr->ctx)
         lws_cancel_service(svr->ctx);
}

void ws_live_stop(ws_live_server *svr)
{
    if (!svr) return;

    svr->running = 0;
    pthread_join(svr->thread, NULL);

    pthread_mutex_lock(&svr->lock);
    int i;
    for (i = 0; i < RING_CAPACITY; i++) {
        if (svr->ring[i].data) {
            free(svr->ring[i].data);
            svr->ring[i].data = NULL;
        }
    }
    pthread_mutex_unlock(&svr->lock);

    pthread_mutex_destroy(&svr->lock);
    free(svr);
    g_server = NULL;
}