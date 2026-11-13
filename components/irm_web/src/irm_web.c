#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "irm_control.h"
#include "irm_core.h"
#include "irm_kiwi_uri.h"
#include "irm_web_dir.h"
#if CONFIG_IRM_DIR_ENABLE
#include "irm_dir.h"
#endif
#include "irm_web.h"

static const char *TAG = "irm_web";
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
static httpd_handle_t s_srv;
static char s_auth[96];   /* "Basic <base64>" oder leer */

static void b64(const char *in, char *out, size_t out_sz)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = strlen(in), o = 0;
    for (size_t i = 0; i < n && o + 4 < out_sz; i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (i + 1 < n ? (unsigned char)in[i + 1] << 8 : 0) | (i + 2 < n ? (unsigned char)in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

static bool authorized(httpd_req_t *req)
{
    if (!s_auth[0]) return true;
    char h[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", h, sizeof(h)) == ESP_OK && strcmp(h, s_auth) == 0) return true;
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"iRadioMini2\"");
    httpd_resp_sendstr(req, "Authentifizierung erforderlich");
    return false;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json"); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(req, s);
    free(s);
    return e;
}

static cJSON *read_json(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 512) return NULL;
    char buf[513]; size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) return NULL;
        got += (size_t)r;
    }
    buf[got] = 0;
    return cJSON_Parse(buf);
}

static esp_err_t h_index(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t h_status(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    static const char *const ps[] = { "idle", "connecting", "playing", "paused", "error" };
    irm_status_t s; irm_status_get(&s);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "source", s.source == IRM_SOURCE_RADIO ? "radio" : (s.source == IRM_SOURCE_KIWI ? "kiwi" : "media"));
    cJSON_AddStringToObject(j, "player", ps[s.player <= IRM_PLAYER_ERROR ? s.player : 0]);
    cJSON_AddNumberToObject(j, "index", s.index);
    cJSON_AddNumberToObject(j, "count", s.count);
    cJSON_AddNumberToObject(j, "volume", s.volume);
    cJSON_AddStringToObject(j, "name", s.name);
    cJSON_AddBoolToObject(j, "net", s.net_up);
    cJSON_AddNumberToObject(j, "sample_rate", s.sample_rate);
    cJSON_AddNumberToObject(j, "bitrate", s.bitrate);
    cJSON *eq = cJSON_AddArrayToObject(j, "eq");
    for (int i = 0; i < IRM_EQ_BANDS; i++) cJSON_AddItemToArray(eq, cJSON_CreateNumber(s.eq.gain_db[i]));
    if (s.source == IRM_SOURCE_KIWI) {   /* Abstimmung: Frequenz in Hz, Modus, Feldstaerke (nur wenn bekannt) und die Modus-Tabelle */
        cJSON *k = cJSON_AddObjectToObject(j, "kiwi");
        cJSON_AddNumberToObject(k, "freq_hz", s.kiwi_freq_hz);
        cJSON_AddStringToObject(k, "mode", s.kiwi_mode);
        if (s.kiwi_rssi_dbm != IRM_RSSI_NONE) cJSON_AddNumberToObject(k, "rssi_dbm", s.kiwi_rssi_dbm);
        cJSON *m = cJSON_AddArrayToObject(k, "modes");
        for (size_t i = 0; i < irm_kiwi_mode_count(); i++) cJSON_AddItemToArray(m, cJSON_CreateString(irm_kiwi_mode_name(i)));
    }
    return send_json(req, j);
}

static esp_err_t h_list(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    irm_status_t s; irm_status_get(&s);
    irm_source_t src = s.source;   /* Liste der aktiven Quelle */
    cJSON *j = cJSON_CreateArray();
    size_t n = irm_control_item_count(src);
    for (size_t i = 0; i < n; i++) {
        char name[IRM_NAME_MAX_LEN];
        if (irm_control_item(src, i, name, sizeof(name), NULL, 0)) cJSON_AddItemToArray(j, cJSON_CreateString(name));
    }
    return send_json(req, j);
}

static const struct { const char *name; irm_cmd_id_t id; } k_cmds[] = {
    { "next", IRM_CMD_NEXT }, { "prev", IRM_CMD_PREV }, { "play", IRM_CMD_PLAY }, { "pause", IRM_CMD_PAUSE },
    { "toggle", IRM_CMD_TOGGLE_PAUSE }, { "stop", IRM_CMD_STOP }, { "vol_up", IRM_CMD_VOLUME_UP },
    { "vol_down", IRM_CMD_VOLUME_DOWN }, { "volume", IRM_CMD_SET_VOLUME }, { "goto", IRM_CMD_GOTO },
    { "source", IRM_CMD_SET_SOURCE }, { "toggle_source", IRM_CMD_TOGGLE_SOURCE },
    { "kiwi_tune", IRM_CMD_KIWI_TUNE },   /* value = Hz */
    { "kiwi_step", IRM_CMD_KIWI_STEP },   /* value = +-Hz */
    { "kiwi_mode", IRM_CMD_KIWI_MODE },   /* value = Index, oder "mode":"usb" */
    { "kiwi_fav",  IRM_CMD_KIWI_FAVORITE },   /* aktuellen Kiwi-Eintrag samt Frequenz in kiwi.m3u speichern */
};

static esp_err_t h_cmd(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *j = read_json(req);
    const cJSON *c = j ? cJSON_GetObjectItemCaseSensitive(j, "cmd") : NULL;
    const cJSON *v = j ? cJSON_GetObjectItemCaseSensitive(j, "value") : NULL;
    const cJSON *mo = j ? cJSON_GetObjectItemCaseSensitive(j, "mode") : NULL;
    esp_err_t e = ESP_ERR_NOT_FOUND;
    if (cJSON_IsString(c)) {
        for (size_t i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
            if (strcmp(c->valuestring, k_cmds[i].name) == 0) {
                int32_t val = cJSON_IsNumber(v) ? (int32_t)v->valuedouble : 0;
                if (k_cmds[i].id == IRM_CMD_KIWI_MODE && !cJSON_IsNumber(v) && !cJSON_IsString(mo)) val = -1;   /* ohne Angabe: naechster Modus */
                if (k_cmds[i].id == IRM_CMD_KIWI_MODE && cJSON_IsString(mo)) {
                    val = irm_kiwi_mode_index(mo->valuestring);
                    if (val < 0) { cJSON_Delete(j); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unbekannter Modus"); }
                }
                if (k_cmds[i].id == IRM_CMD_KIWI_TUNE && (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 2e9)) {
                    cJSON_Delete(j);
                    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "kiwi_tune: value = Frequenz in Hz");
                }
                e = irm_cmd_post_id(k_cmds[i].id, val);
                break;
            }
        }
    }
    cJSON_Delete(j);
    if (e == ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unbekanntes Kommando");
    if (e != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Kommando-Queue voll");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

#if CONFIG_IRM_DIR_ENABLE
/* ---- Serververzeichnis ---- */
static esp_err_t send_json_status(httpd_req_t *req, const char *status, cJSON *j)
{
    httpd_resp_set_status(req, status);
    return send_json(req, j);
}

static esp_err_t h_dir_info(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    irm_dir_info_t i;
    irm_dir_info(&i);
    return send_json(req, irm_web_dir_info_json(&i));
}

static esp_err_t h_dir_list(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    char qs[160] = "";
    if (httpd_req_get_url_query_len(req) >= sizeof(qs)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Anfrage zu lang");
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) != ESP_OK) qs[0] = 0;   /* ohne Query: Vorgaben */
    irm_web_dir_req_t r;
    if (!irm_web_dir_parse_query(qs, &r)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ungueltige Parameter");
    cJSON *j = irm_web_dir_list_json(irm_dir_db(), &r);
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher");
    return send_json(req, j);
}

static esp_err_t h_dir_countries(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *j = irm_web_dir_countries_json(irm_dir_db());
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher");
    return send_json(req, j);
}

/* POST {"force":true} -> Liste im Hintergrund aktualisieren; Fortschritt ueber GET /api/dir */
static esp_err_t h_dir_update(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *b = req->content_len ? read_json(req) : NULL;
    const cJSON *f = b ? cJSON_GetObjectItemCaseSensitive(b, "force") : NULL;
    const bool force = cJSON_IsTrue(f);
    cJSON_Delete(b);
    const esp_err_t e = irm_dir_update(force);
    cJSON *j = cJSON_CreateObject();
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher");
    cJSON_AddBoolToObject(j, "ok", e == ESP_OK);
    if (e == ESP_OK) return send_json_status(req, "202 Accepted", j);
    const char *st = "500 Internal Server Error", *msg = "Fehler";
    switch (e) {
    case ESP_ERR_NOT_SUPPORTED: st = "501 Not Implemented"; msg = "Partition kiwidb fehlt (partitions.csv aktualisieren und neu flashen)"; break;
    case ESP_ERR_INVALID_STATE: st = "409 Conflict"; msg = "Aktualisierung laeuft bereits"; break;
    case ESP_ERR_TIMEOUT: {
        st = "429 Too Many Requests"; msg = "Mindestabstand noch nicht erreicht (erzwingen moeglich)";
        irm_dir_info_t i; irm_dir_info(&i);
        cJSON_AddNumberToObject(j, "retry_in_s", i.retry_in_s);
        break;
    }
    case ESP_ERR_NO_MEM: st = "503 Service Unavailable"; msg = "Task nicht startbar (Speicher)"; break;
    default: break;
    }
    cJSON_AddStringToObject(j, "error", msg);
    return send_json_status(req, st, j);
}

/* POST {"host":"...","port":8073} -> Server aus dem Verzeichnis anhoeren (Host/Port statt Index: bleibt auch nach einem Update gueltig) */
static esp_err_t h_dir_connect(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *b = read_json(req);
    const cJSON *h = b ? cJSON_GetObjectItemCaseSensitive(b, "host") : NULL;
    const cJSON *p = b ? cJSON_GetObjectItemCaseSensitive(b, "port") : NULL;
    uint32_t idx = 0;
    esp_err_t e = ESP_ERR_NOT_FOUND;
    if (cJSON_IsString(h) && cJSON_IsNumber(p) && p->valuedouble >= 1 && p->valuedouble <= 65535 && strlen(h->valuestring) < KIWI_DIR_HOST_LEN &&
        kiwi_dir_db_lookup(irm_dir_db(), h->valuestring, (uint16_t)p->valuedouble, &idx) == KIWI_DIR_OK)
        e = irm_cmd_post_id(IRM_CMD_KIWI_CONNECT, (int32_t)idx);
    cJSON_Delete(b);
    if (e == ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Server nicht im Verzeichnis");
    if (e != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Kommando-Queue voll");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
#endif

static esp_err_t h_eq(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *j = read_json(req);
    const cJSON *g = j ? cJSON_GetObjectItemCaseSensitive(j, "gains") : NULL;
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(j, "action")) && !g) {
        const char *a = cJSON_GetObjectItemCaseSensitive(j, "action")->valuestring;
        irm_cmd_post_id(strcmp(a, "reset") == 0 ? IRM_CMD_RESET_EQ : IRM_CMD_SAVE_EQ, 0);
        cJSON_Delete(j);
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    if (!cJSON_IsArray(g) || cJSON_GetArraySize(g) != IRM_EQ_BANDS) {
        cJSON_Delete(j);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "gains: genau 10 Werte erwartet");
    }
    irm_cmd_t m = { .id = IRM_CMD_SET_EQ };
    for (int i = 0; i < IRM_EQ_BANDS; i++) {
        int v = (int)cJSON_GetArrayItem(g, i)->valuedouble;
        m.eq.gain_db[i] = (int8_t)(v < IRM_EQ_MIN_DB ? IRM_EQ_MIN_DB : (v > IRM_EQ_MAX_DB ? IRM_EQ_MAX_DB : v));
    }
    const cJSON *save = cJSON_GetObjectItemCaseSensitive(j, "save");
    bool do_save = cJSON_IsTrue(save);
    cJSON_Delete(j);
    irm_cmd_post(&m, 0);
    if (do_save) irm_cmd_post_id(IRM_CMD_SAVE_EQ, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t irm_web_start(void)
{
    if (s_srv) return ESP_OK;
    if (CONFIG_IRM_WEB_PASSWORD[0]) {
        char cred[96], enc[80];
        snprintf(cred, sizeof(cred), "%s:%s", CONFIG_IRM_WEB_USER, CONFIG_IRM_WEB_PASSWORD);
        b64(cred, enc, sizeof(enc));
        snprintf(s_auth, sizeof(s_auth), "Basic %s", enc);
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;   /* Standard 8; es sind 5 Basis-Endpunkte + 5 fuer das Serververzeichnis */
    cfg.server_port = CONFIG_IRM_WEB_PORT;
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    esp_err_t e = httpd_start(&s_srv, &cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(e)); return e; }
    const httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/list",     .method = HTTP_GET,  .handler = h_list },
        { .uri = "/api/cmd",      .method = HTTP_POST, .handler = h_cmd },
        { .uri = "/api/eq",       .method = HTTP_POST, .handler = h_eq },
#if CONFIG_IRM_DIR_ENABLE
        { .uri = "/api/dir",           .method = HTTP_GET,  .handler = h_dir_info },
        { .uri = "/api/dir/list",      .method = HTTP_GET,  .handler = h_dir_list },
        { .uri = "/api/dir/countries", .method = HTTP_GET,  .handler = h_dir_countries },
        { .uri = "/api/dir/update",    .method = HTTP_POST, .handler = h_dir_update },
        { .uri = "/api/dir/connect",   .method = HTTP_POST, .handler = h_dir_connect },
#endif
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) ESP_ERROR_CHECK(httpd_register_uri_handler(s_srv, &uris[i]));
    ESP_LOGI(TAG, "Web-UI auf Port %d%s", CONFIG_IRM_WEB_PORT, s_auth[0] ? " (Basic Auth)" : " (OHNE Authentifizierung)");
    return ESP_OK;
}
