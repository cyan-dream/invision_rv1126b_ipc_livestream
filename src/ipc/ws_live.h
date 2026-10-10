#ifndef WS_LIVE_H
#define WS_LIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_live_server ws_live_server;

/* 启动 WebSocket 服务，返回句柄；失败返回 NULL */
ws_live_server *ws_live_start(int port);

/* 停止服务并释放资源 */
void            ws_live_stop(ws_live_server *svr);

/* 编码线程调用：把一段 NAL 单元塞进环形缓冲区
 *   data     : NAL 数据（含起始码 00 00 00 01）
 *   len      : NAL 总长度
 *   nal_type : h264 NAL 类型（7=SPS 8=PPS 5=IDR 1=非IDR）
 *              h265 NAL 类型（32=VPS 33=SPS 34=PPS 19=IDR 1=非IDR）
 */
void            ws_live_push(ws_live_server *svr,
                             const unsigned char *data, int len,
                             int nal_type);

int ws_live_client_count(void);                             

#ifdef __cplusplus
}
#endif

#endif /* WS_LIVE_H */