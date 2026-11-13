/* Schlanker Ersatz fuer ESP-IDFs esp_http_server.h: nur Typen/Signaturen, die irm_web.c benutzt - fuer die reine Syntax-/Typpruefung auf dem Host */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef enum { HTTP_GET = 1, HTTP_POST = 3 } httpd_method_t;
typedef enum { HTTPD_400_BAD_REQUEST = 0, HTTPD_404_NOT_FOUND, HTTPD_500_INTERNAL_SERVER_ERROR } httpd_err_code_t;
typedef struct httpd_req { httpd_handle_t handle; int method; const char uri[512]; size_t content_len; void *aux; void *user_ctx; } httpd_req_t;
typedef struct httpd_uri { const char *uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t *r); void *user_ctx; } httpd_uri_t;
typedef struct { unsigned task_priority; size_t stack_size; int core_id; uint16_t server_port; uint16_t ctrl_port; uint16_t max_open_sockets; uint16_t max_uri_handlers;
                 uint16_t max_resp_headers; uint16_t backlog_conn; _Bool lru_purge_enable; uint16_t recv_wait_timeout; uint16_t send_wait_timeout; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() { 5, 4096, 0x7FFFFFFF, 80, 32768, 7, 8, 8, 5, 0, 5, 5 }
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *msg);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
size_t httpd_req_get_url_query_len(httpd_req_t *r);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
const char *esp_err_to_name(esp_err_t code);
#define ESP_ERROR_CHECK(x) do { esp_err_t e_ = (x); (void)e_; } while (0)
