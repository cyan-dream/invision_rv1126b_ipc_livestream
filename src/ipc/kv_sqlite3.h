#ifndef KV_SQLITE3_H
#define KV_SQLITE3_H

#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 返回码 ---------- */
#define KV_OK        0
#define KV_NOTFOUND  1
#define KV_TOOSMALL  2
#define KV_ERROR    (-1)

/* ---------- 句柄 ---------- */
typedef struct {
    sqlite3      *db;
    sqlite3_stmt *st_set;    /* 插入/更新 */
    sqlite3_stmt *st_get;    /* 读取       */
    sqlite3_stmt *st_del;    /* 删除       */
    sqlite3_stmt *st_scan;   /* 遍历       */
} kv_t;

/* 遍历回调：返回非 0 可提前终止 */
typedef int (*kv_visit_fn)(const char *key, const char *value, void *arg);

/* ---------- 生命周期 ---------- */
int  kv_open (kv_t *kv, const char *path);
void kv_close(kv_t *kv);

/* ---------- CRUD ---------- */
int  kv_set    (kv_t *kv, const char *key, const char *value);
int  kv_get    (kv_t *kv, const char *key, char *buf, int buf_size);
int  kv_del    (kv_t *kv, const char *key);
int  kv_foreach(kv_t *kv, kv_visit_fn fn, void *arg);

/* ---------- 事务 ---------- */
int  kv_begin   (kv_t *kv);
int  kv_commit  (kv_t *kv);
int  kv_rollback(kv_t *kv);

int print_item(const char *key, const char *value, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* KV_SQLITE3_H */