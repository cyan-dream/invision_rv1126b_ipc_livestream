/* kv_demo.c —— 基于 SQLite 的键值对存储
 * 编译: gcc -Wall -O2 -o kv_demo kv_demo.c -lsqlite3
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kv_sqlite3.h"

/* ---------- 内部工具 ---------- */
static int kv_prepare(sqlite3 *db, const char *sql, sqlite3_stmt **out)
{
    if (sqlite3_prepare_v2(db, sql, -1, out, NULL) != SQLITE_OK) {
        fprintf(stderr, "prepare 失败: %s\n  SQL: %s\n",
                sqlite3_errmsg(db), sql);
        return KV_ERROR;
    }
    return KV_OK;
}

static int kv_exec(kv_t *kv, const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(kv->db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "exec 失败: %s\n  SQL: %s\n",
                err ? err : "?", sql);
        sqlite3_free(err);
        return KV_ERROR;
    }
    return KV_OK;
}

/* ---------- 打开 / 关闭 ---------- */
void kv_close(kv_t *kv)
{
    if (!kv) return;

    sqlite3_finalize(kv->st_set);  kv->st_set  = NULL;
    sqlite3_finalize(kv->st_get);  kv->st_get  = NULL;
    sqlite3_finalize(kv->st_del);  kv->st_del  = NULL;
    sqlite3_finalize(kv->st_scan); kv->st_scan = NULL;

    if (kv->db) {
        sqlite3_close(kv->db);
        kv->db = NULL;
    }
}

int kv_open(kv_t *kv, const char *path)
{
    if (!kv || !path) return KV_ERROR;
    memset(kv, 0, sizeof(*kv));

    if (sqlite3_open(path, &kv->db) != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库 %s: %s\n", path,
                kv->db ? sqlite3_errmsg(kv->db) : "unknown");
        sqlite3_close(kv->db);
        kv->db = NULL;
        return KV_ERROR;
    }

    /* 性能/并发调优（内存库不支持 WAL，失败可忽略） */
    sqlite3_exec(kv->db, "PRAGMA journal_mode=WAL;",   NULL, NULL, NULL);
    sqlite3_exec(kv->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(kv->db, "PRAGMA busy_timeout=5000;",  NULL, NULL, NULL);

    /* 建表：key 是主键，WITHOUT ROWID 让主键即数据，查询更快 */
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS kv ("
        "  key        TEXT PRIMARY KEY,"
        "  value      TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        "             DEFAULT (CAST(strftime('%s','now') AS INTEGER))"
        ") WITHOUT ROWID;";

    char *err = NULL;
    if (sqlite3_exec(kv->db, ddl, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "建表失败: %s\n", err ? err : "?");
        sqlite3_free(err);
        goto fail;
    }

    /* UPSERT：存在则更新，不存在则插入（需 SQLite >= 3.24.0） */
    if (kv_prepare(kv->db,
            "INSERT INTO kv(key, value) VALUES(?, ?) "
            "ON CONFLICT(key) DO UPDATE SET "
            "  value      = excluded.value,"
            "  updated_at = CAST(strftime('%s','now') AS INTEGER);",
            &kv->st_set) != KV_OK) goto fail;

    if (kv_prepare(kv->db,
            "SELECT value FROM kv WHERE key = ?;",
            &kv->st_get) != KV_OK) goto fail;

    if (kv_prepare(kv->db,
            "DELETE FROM kv WHERE key = ?;",
            &kv->st_del) != KV_OK) goto fail;

    if (kv_prepare(kv->db,
            "SELECT key, value FROM kv ORDER BY key;",
            &kv->st_scan) != KV_OK) goto fail;

    return KV_OK;

fail:
    kv_close(kv);
    return KV_ERROR;
}

/* ---------- 写入 / 修改 ---------- */
int kv_set(kv_t *kv, const char *key, const char *value)
{
    if (!kv || !key || !value) return KV_ERROR;

    sqlite3_reset(kv->st_set);
    sqlite3_clear_bindings(kv->st_set);
    sqlite3_bind_text(kv->st_set, 1, key,   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(kv->st_set, 2, value, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(kv->st_set);
    sqlite3_reset(kv->st_set);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "kv_set('%s') 失败: %s\n",
                key, sqlite3_errmsg(kv->db));
        return KV_ERROR;
    }
    return KV_OK;
}

/* ---------- 读取 ---------- */
int kv_get(kv_t *kv, const char *key, char *buf, int buf_size)
{
    if (!kv || !key || !buf || buf_size == 0) return KV_ERROR;
    buf[0] = '\0';

    sqlite3_reset(kv->st_get);
    sqlite3_clear_bindings(kv->st_get);
    sqlite3_bind_text(kv->st_get, 1, key, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(kv->st_get);

    if (rc == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(kv->st_get, 0);
        int len = sqlite3_column_bytes(kv->st_get, 0);

        /* 需要 len + 1 个字节（含结尾 '\0'） */
        if (len + 1 > buf_size) {
            sqlite3_reset(kv->st_get);
            return KV_TOOSMALL;
        }
        if (txt && len > 0) memcpy(buf, txt, len);
        buf[len] = '\0';

        sqlite3_reset(kv->st_get);
        return KV_OK;
    }

    sqlite3_reset(kv->st_get);
    if (rc == SQLITE_DONE) return KV_NOTFOUND;

    fprintf(stderr, "kv_get('%s') 失败: %s\n",
            key, sqlite3_errmsg(kv->db));
    return KV_ERROR;
}

/* ---------- 删除 ---------- */
int kv_del(kv_t *kv, const char *key)
{
    if (!kv || !key) return KV_ERROR;

    sqlite3_reset(kv->st_del);
    sqlite3_clear_bindings(kv->st_del);
    sqlite3_bind_text(kv->st_del, 1, key, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(kv->st_del);
    sqlite3_reset(kv->st_del);

    if (rc != SQLITE_DONE) return KV_ERROR;
    return sqlite3_changes(kv->db) > 0 ? KV_OK : KV_NOTFOUND;
}

/* ---------- 遍历 ---------- */
int kv_foreach(kv_t *kv, kv_visit_fn fn, void *arg)
{
    if (!kv || !fn) return KV_ERROR;

    sqlite3_reset(kv->st_scan);

    int rc;
    while ((rc = sqlite3_step(kv->st_scan)) == SQLITE_ROW) {
        const char *k = (const char *)sqlite3_column_text(kv->st_scan, 0);
        const char *v = (const char *)sqlite3_column_text(kv->st_scan, 1);
        if (fn(k ? k : "", v ? v : "", arg) != 0) break;
    }
    sqlite3_reset(kv->st_scan);

    return rc == SQLITE_DONE ? KV_OK : KV_ERROR;
}

/* ---------- 事务 ---------- */
int kv_begin   (kv_t *kv) { return kv_exec(kv, "BEGIN IMMEDIATE;"); }
int kv_commit  (kv_t *kv) { return kv_exec(kv, "COMMIT;"); }
int kv_rollback(kv_t *kv) { return kv_exec(kv, "ROLLBACK;"); }

/* ==================== 使用示例 ==================== */
int print_item(const char *key, const char *value, void *arg)
{
    int *n = (int *)arg;
    printf("  %-12s = %s\n", key, value);
    (*n)++;
    return 0;
}

static int main_kv_sqlite3(void)
{
    kv_t kv;

    if (kv_open(&kv, "demo.db") != KV_OK) return 1;

    /* 1. 存储 */
    kv_set(&kv, "name", "Alice");
    kv_set(&kv, "city", "Beijing");
    kv_set(&kv, "lang", "C");

    /* 2. 读取 */
    char val[256];
    if (kv_get(&kv, "name", val, sizeof val) == KV_OK)
        printf("读取 name = %s\n", val);

    if (kv_get(&kv, "nobody", val, sizeof val) == KV_NOTFOUND)
        printf("读取 nobody = <不存在>\n");

    /* 3. 修改 */
    kv_set(&kv, "city", "Shanghai");
    if (kv_get(&kv, "city", val, sizeof val) == KV_OK)
        printf("修改后 city = %s\n", val);

    /* 4. 批量写入 */
    kv_begin(&kv);
    for (int i = 0; i < 5; i++) {
        char k[32], v[32];
        snprintf(k, sizeof k, "user:%d", i);
        snprintf(v, sizeof v, "value-%d", i);
        kv_set(&kv, k, v);
    }
    kv_commit(&kv);

    /* 5. 遍历 */
    printf("--- 全部键值对 ---\n");
    int n = 0;
    kv_foreach(&kv, print_item, &n);
    printf("共 %d 条\n", n);

    /* 6. 删除 */
    kv_del(&kv, "lang");

    kv_close(&kv);
    return 0;
}