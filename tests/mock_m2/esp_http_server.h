#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* httpd_handle_t;

typedef enum {
    HTTP_GET = 1,
    HTTP_POST = 2
} httpd_method_t;

#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTPD_RESP_USE_STRLEN -1

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char *uri;
    size_t content_len;
    void *user_ctx;
    void *sess_ctx;
    void *free_ctx;
    bool ignore_sess_ctx_changes;
} httpd_req_t;

typedef esp_err_t (*httpd_uri_func_t)(httpd_req_t *r);

typedef struct httpd_uri {
    const char *uri;
    httpd_method_t method;
    httpd_uri_func_t handler;
    void *user_ctx;
} httpd_uri_t;

typedef struct httpd_config {
    unsigned task_priority;
    size_t stack_size;
    int core_id;
    uint16_t server_port;
    uint16_t ctrl_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    uint16_t max_resp_headers;
    uint16_t backlog_conn;
    bool lru_purge_enable;
    void *open_fn;
    void *close_fn;
    void *global_user_ctx;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() { \
    .task_priority = 5, \
    .stack_size = 4096, \
    .core_id = 0, \
    .server_port = 80, \
    .ctrl_port = 32768, \
    .max_open_sockets = 7, \
    .max_uri_handlers = 8, \
    .max_resp_headers = 8, \
    .backlog_conn = 5, \
    .lru_purge_enable = false, \
    .open_fn = NULL, \
    .close_fn = NULL, \
    .global_user_ctx = NULL \
}

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
