#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "database.h"
#include "exec_utils.h"
#include "json_builder.h"
#include "sms_email.h"

#define EMAIL_MAX_ATTEMPTS 5
#define EMAIL_LOG_RESPONSE_SIZE 1024
#define EMAIL_LOG_MAX_ROWS 200
#define EMAIL_QUEUE_TERMINAL_MAX_ROWS 200
#define EMAIL_QUEUE_PENDING_MAX_ROWS 200

static pthread_mutex_t g_email_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_email_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_email_worker;
static SmsEmailConfig g_email_config;
static int g_email_initialized = 0;
static int g_email_worker_running = 0;

static void copy_text(char *dst, size_t size, const char *src) {
    if (!dst || size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hex_decode(const char *hex, char *out, size_t size) {
    size_t i = 0, j = 0;
    if (!out || size == 0) return;
    if (!hex) {
        out[0] = '\0';
        return;
    }
    while (hex[i] && hex[i + 1] && j + 1 < size) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        out[j++] = (char)((hi << 4) | lo);
        i += 2;
    }
    out[j] = '\0';
}

static int split_fields(char *text, char **fields, int field_count) {
    int found = 0;
    char *cursor = text;
    if (!text || !fields || field_count <= 0) return 0;
    while (found < field_count) {
        fields[found++] = cursor;
        cursor = strchr(cursor, '|');
        if (!cursor) break;
        *cursor++ = '\0';
    }
    return found;
}

static int config_complete(const SmsEmailConfig *config) {
    return config && config->enabled && config->smtp_server[0] &&
           config->smtp_port > 0 && config->smtp_user[0] &&
           config->smtp_password[0] && config->from_addr[0] &&
           config->to_addr[0];
}

static int server_is_valid(const char *server) {
    if (!server || !server[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)server; *p; p++) {
        if (!(isalnum(*p) || *p == '.' || *p == '-')) return 0;
    }
    return 1;
}

static void load_config_locked(void) {
    char output[4096] = {0};
    char *fields[7];
    int count;

    memset(&g_email_config, 0, sizeof(g_email_config));
    g_email_config.smtp_port = 465;
    if (db_query_rows("SELECT enabled,hex(smtp_server),smtp_port,hex(smtp_user),hex(smtp_password),hex(from_addr),hex(to_addr) FROM sms_email_config WHERE id = 1;", "|", output, sizeof(output)) != 0 || !output[0]) {
        return;
    }

    count = split_fields(output, fields, 7);
    if (count != 7) return;
    g_email_config.enabled = atoi(fields[0]) ? 1 : 0;
    hex_decode(fields[1], g_email_config.smtp_server, sizeof(g_email_config.smtp_server));
    g_email_config.smtp_port = atoi(fields[2]);
    hex_decode(fields[3], g_email_config.smtp_user, sizeof(g_email_config.smtp_user));
    hex_decode(fields[4], g_email_config.smtp_password, sizeof(g_email_config.smtp_password));
    hex_decode(fields[5], g_email_config.from_addr, sizeof(g_email_config.from_addr));
    hex_decode(fields[6], g_email_config.to_addr, sizeof(g_email_config.to_addr));
    g_email_config.password_set = g_email_config.smtp_password[0] ? 1 : 0;
}

static void base64_encode(const unsigned char *src, size_t len, char *dst, size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;
    while (i < len && j + 5 < size) {
        size_t remaining = len - i;
        unsigned int a = src[i++];
        unsigned int b = remaining > 1 ? src[i++] : 0;
        unsigned int c = remaining > 2 ? src[i++] : 0;
        dst[j++] = alphabet[a >> 2];
        dst[j++] = alphabet[((a & 3) << 4) | (b >> 4)];
        dst[j++] = remaining > 1 ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        dst[j++] = remaining > 2 ? alphabet[c & 63] : '=';
    }
    dst[j] = '\0';
}

static void header_safe_text(const char *src, char *dst, size_t size) {
    size_t j = 0;
    if (!dst || size == 0) return;
    if (!src) src = "";
    for (size_t i = 0; src[i] && j + 1 < size; i++) {
        dst[j++] = (src[i] == '\r' || src[i] == '\n') ? ' ' : src[i];
    }
    dst[j] = '\0';
}

static int send_email(const SmsEmailConfig *config, const char *sender,
                      const char *content, time_t timestamp, char *result,
                      size_t result_size) {
    char template_path[] = "/tmp/sms-email-XXXXXX";
    char url[320];
    char auth[540];
    char subject[320];
    char safe_sender[96];
    char subject_b64[512];
    char time_text[64];
    struct tm tm_info;
    int fd;
    FILE *mail;
    int rc;

    if (!config_complete(config) || !server_is_valid(config->smtp_server)) {
        copy_text(result, result_size, "SMTP configuration is incomplete or invalid");
        return -1;
    }

    fd = mkstemp(template_path);
    if (fd < 0) {
        copy_text(result, result_size, "Unable to create email message file");
        return -1;
    }
    fchmod(fd, 0600);
    mail = fdopen(fd, "w");
    if (!mail) {
        close(fd);
        unlink(template_path);
        copy_text(result, result_size, "Unable to open email message file");
        return -1;
    }

    header_safe_text(sender, safe_sender, sizeof(safe_sender));
    snprintf(subject, sizeof(subject), "SMS forward: %s", safe_sender);
    base64_encode((const unsigned char *)subject, strlen(subject), subject_b64, sizeof(subject_b64));
    localtime_r(&timestamp, &tm_info);
    strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &tm_info);
    fprintf(mail, "From: %s\r\nTo: %s\r\nSubject: =?UTF-8?B?%s?=\r\n"
                  "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                  "Content-Transfer-Encoding: 8bit\r\n\r\nSender: %s\nTime: %s\n\n%s\n",
            config->from_addr, config->to_addr, subject_b64, safe_sender, time_text,
            content ? content : "");
    if (fclose(mail) != 0) {
        unlink(template_path);
        copy_text(result, result_size, "Unable to write email message file");
        return -1;
    }

    snprintf(auth, sizeof(auth), "%s:%s", config->smtp_user, config->smtp_password);
    if (config->smtp_port == 465) {
        snprintf(url, sizeof(url), "smtps://%s:%d", config->smtp_server, config->smtp_port);
        rc = run_command(result, result_size, "curl", "--silent", "--show-error", "--fail",
                         "--connect-timeout", "15", "--max-time", "60", "--url", url,
                         "--user", auth, "--mail-from", config->from_addr, "--mail-rcpt",
                         config->to_addr, "--upload-file", template_path, NULL);
    } else {
        snprintf(url, sizeof(url), "smtp://%s:%d", config->smtp_server, config->smtp_port);
        rc = run_command(result, result_size, "curl", "--silent", "--show-error", "--fail",
                         "--connect-timeout", "15", "--max-time", "60", "--ssl-reqd",
                         "--url", url, "--user", auth, "--mail-from", config->from_addr,
                         "--mail-rcpt", config->to_addr, "--upload-file", template_path, NULL);
    }
    unlink(template_path);
    return rc;
}

static void save_log(const char *sender, const char *status, int attempts,
                     const char *response) {
    char escaped_sender[256];
    char escaped_status[64];
    char escaped_response[EMAIL_LOG_RESPONSE_SIZE * 2 + 1];
    char sql[4096];
    char limited_response[EMAIL_LOG_RESPONSE_SIZE];

    copy_text(limited_response, sizeof(limited_response), response);
    db_escape_string(sender ? sender : "", escaped_sender, sizeof(escaped_sender));
    db_escape_string(status ? status : "", escaped_status, sizeof(escaped_status));
    db_escape_string(limited_response, escaped_response, sizeof(escaped_response));
    snprintf(sql, sizeof(sql),
             "INSERT INTO sms_email_log (sender,status,attempts,response,created_at) "
             "VALUES ('%s','%s',%d,'%s',%ld);",
             escaped_sender, escaped_status, attempts, escaped_response, (long)time(NULL));
    db_execute_safe(sql);

    /* 邮件记录用于诊断，不应随设备运行时间无限增长。 */
    snprintf(sql, sizeof(sql),
             "DELETE FROM sms_email_log WHERE id NOT IN ("
             "SELECT id FROM sms_email_log ORDER BY id DESC LIMIT %d);"
             "DELETE FROM sms_email_queue WHERE status IN ('sent','failed') "
             "AND id NOT IN (SELECT id FROM sms_email_queue "
             "WHERE status IN ('sent','failed') ORDER BY id DESC LIMIT %d);",
             EMAIL_LOG_MAX_ROWS, EMAIL_QUEUE_TERMINAL_MAX_ROWS);
    db_execute_safe(sql);
}

static void process_one_email(void) {
    char output[8192] = {0};
    char *fields[5];
    char sender[128];
    char content[4096];
    char result[EMAIL_LOG_RESPONSE_SIZE] = {0};
    SmsEmailConfig config;
    char sql[4096];
    int count;
    int id;
    int attempts;
    time_t timestamp;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_email_mutex);
    config = g_email_config;
    pthread_mutex_unlock(&g_email_mutex);
    if (!config_complete(&config)) return;

    snprintf(sql, sizeof(sql),
             "SELECT id,hex(sender),hex(content),timestamp,attempts FROM sms_email_queue "
             "WHERE status='pending' AND next_attempt <= %ld ORDER BY id ASC LIMIT 1;",
             (long)now);
    if (db_query_rows(sql, "|", output, sizeof(output)) != 0 || !output[0]) return;
    count = split_fields(output, fields, 5);
    if (count != 5) return;

    id = atoi(fields[0]);
    hex_decode(fields[1], sender, sizeof(sender));
    hex_decode(fields[2], content, sizeof(content));
    timestamp = (time_t)atol(fields[3]);
    attempts = atoi(fields[4]);
    if (id <= 0) return;

    if (send_email(&config, sender, content, timestamp, result, sizeof(result)) == 0) {
        snprintf(sql, sizeof(sql),
                 "UPDATE sms_email_queue SET status='sent',attempts=%d,last_error='' WHERE id=%d;",
                 attempts + 1, id);
        db_execute_safe(sql);
        save_log(sender, "sent", attempts + 1, result[0] ? result : "SMTP accepted message");
        return;
    }

    attempts++;
    if (attempts >= EMAIL_MAX_ATTEMPTS) {
        char escaped_result[EMAIL_LOG_RESPONSE_SIZE * 2 + 1];
        db_escape_string(result, escaped_result, sizeof(escaped_result));
        snprintf(sql, sizeof(sql),
                 "UPDATE sms_email_queue SET status='failed',attempts=%d,last_error='%s' WHERE id=%d;",
                 attempts, escaped_result, id);
        db_execute_safe(sql);
        save_log(sender, "failed", attempts, result);
    } else {
        int delay = 30 << (attempts - 1);
        char escaped_result[EMAIL_LOG_RESPONSE_SIZE * 2 + 1];
        if (delay > 1800) delay = 1800;
        db_escape_string(result, escaped_result, sizeof(escaped_result));
        snprintf(sql, sizeof(sql),
                 "UPDATE sms_email_queue SET attempts=%d,next_attempt=%ld,last_error='%s' WHERE id=%d;",
                 attempts, (long)(now + delay), escaped_result, id);
        db_execute_safe(sql);
        save_log(sender, "retry", attempts, result);
    }
}

static void *email_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_email_mutex);
    while (g_email_worker_running) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 5;
        pthread_cond_timedwait(&g_email_cond, &g_email_mutex, &deadline);
        if (!g_email_worker_running) break;
        pthread_mutex_unlock(&g_email_mutex);
        process_one_email();
        pthread_mutex_lock(&g_email_mutex);
    }
    pthread_mutex_unlock(&g_email_mutex);
    return NULL;
}

int sms_email_init(void) {
    pthread_mutex_lock(&g_email_mutex);
    if (g_email_initialized) {
        pthread_mutex_unlock(&g_email_mutex);
        return 0;
    }
    load_config_locked();
    g_email_worker_running = 1;
    if (pthread_create(&g_email_worker, NULL, email_worker, NULL) != 0) {
        g_email_worker_running = 0;
        pthread_mutex_unlock(&g_email_mutex);
        return -1;
    }
    g_email_initialized = 1;
    pthread_cond_signal(&g_email_cond);
    pthread_mutex_unlock(&g_email_mutex);
    return 0;
}

void sms_email_deinit(void) {
    pthread_mutex_lock(&g_email_mutex);
    if (!g_email_initialized) {
        pthread_mutex_unlock(&g_email_mutex);
        return;
    }
    g_email_worker_running = 0;
    pthread_cond_signal(&g_email_cond);
    pthread_mutex_unlock(&g_email_mutex);
    pthread_join(g_email_worker, NULL);
    pthread_mutex_lock(&g_email_mutex);
    g_email_initialized = 0;
    pthread_mutex_unlock(&g_email_mutex);
}

int sms_email_get_config(SmsEmailConfig *config) {
    if (!config) return -1;
    pthread_mutex_lock(&g_email_mutex);
    *config = g_email_config;
    config->password_set = config->smtp_password[0] ? 1 : 0;
    pthread_mutex_unlock(&g_email_mutex);
    return 0;
}

int sms_email_save_config(const SmsEmailConfig *config) {
    SmsEmailConfig updated;
    char server[512], user[512], password[512], from[512], to[512];
    char sql[4096];

    if (!config || config->smtp_port < 1 || config->smtp_port > 65535 ||
        (config->smtp_server[0] && !server_is_valid(config->smtp_server))) {
        return -1;
    }
    pthread_mutex_lock(&g_email_mutex);
    updated = *config;
    if (!updated.smtp_password[0]) {
        copy_text(updated.smtp_password, sizeof(updated.smtp_password), g_email_config.smtp_password);
    }
    updated.password_set = updated.smtp_password[0] ? 1 : 0;
    if (updated.enabled && !config_complete(&updated)) {
        pthread_mutex_unlock(&g_email_mutex);
        return -1;
    }

    db_escape_string(updated.smtp_server, server, sizeof(server));
    db_escape_string(updated.smtp_user, user, sizeof(user));
    db_escape_string(updated.smtp_password, password, sizeof(password));
    db_escape_string(updated.from_addr, from, sizeof(from));
    db_escape_string(updated.to_addr, to, sizeof(to));
    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO sms_email_config "
             "(id,enabled,smtp_server,smtp_port,smtp_user,smtp_password,from_addr,to_addr) "
             "VALUES (1,%d,'%s',%d,'%s','%s','%s','%s');",
             updated.enabled ? 1 : 0, server, updated.smtp_port, user, password, from, to);
    if (db_execute_safe(sql) != 0) {
        pthread_mutex_unlock(&g_email_mutex);
        return -1;
    }
    g_email_config = updated;
    pthread_cond_signal(&g_email_cond);
    pthread_mutex_unlock(&g_email_mutex);
    return 0;
}

int sms_email_enqueue(const char *sender, const char *content, time_t timestamp) {
    SmsEmailConfig config;
    char escaped_sender[256];
    char escaped_content[8192];
    char sql[12288];

    if (!sender || !content) return -1;
    pthread_mutex_lock(&g_email_mutex);
    config = g_email_config;
    pthread_mutex_unlock(&g_email_mutex);
    if (!config_complete(&config)) return 0;

    db_escape_string(sender, escaped_sender, sizeof(escaped_sender));
    db_escape_string(content, escaped_content, sizeof(escaped_content));
    snprintf(sql, sizeof(sql),
             "BEGIN;"
             "INSERT INTO sms_email_queue (sender,content,timestamp,status,attempts,next_attempt,last_error,kind) "
             "VALUES ('%s','%s',%ld,'pending',0,%ld,'','sms');"
             "DELETE FROM sms_email_queue WHERE status = 'pending' "
             "AND id NOT IN (SELECT id FROM sms_email_queue "
             "WHERE status = 'pending' ORDER BY id DESC LIMIT %d);"
             "COMMIT;",
             escaped_sender, escaped_content, (long)timestamp, (long)time(NULL),
             EMAIL_QUEUE_PENDING_MAX_ROWS);
    if (db_execute_safe(sql) != 0) return -1;

    pthread_mutex_lock(&g_email_mutex);
    pthread_cond_signal(&g_email_cond);
    pthread_mutex_unlock(&g_email_mutex);
    return 0;
}

int sms_email_test(void) {
    SmsEmailConfig config;
    pthread_mutex_lock(&g_email_mutex);
    config = g_email_config;
    pthread_mutex_unlock(&g_email_mutex);
    if (!config_complete(&config)) return -1;
    return sms_email_enqueue("TEST", "This is a test email generated by the SMS forwarding service.", time(NULL));
}

int sms_email_get_logs(char *json_output, size_t size, int max_count) {
    char output[65536] = {0};
    char sql[256];
    JsonBuilder *json;
    char *line;

    if (!json_output || size == 0) return -1;
    if (max_count <= 0 || max_count > 100) max_count = 20;
    snprintf(sql, sizeof(sql),
             "SELECT id,hex(sender),status,attempts,hex(response),created_at FROM sms_email_log ORDER BY id DESC LIMIT %d;",
             max_count);
    if (db_query_rows(sql, "|", output, sizeof(output)) != 0) return -1;

    json = json_new();
    if (!json) return -1;
    json_arr_open(json, NULL);
    line = output;
    while (line && *line) {
        char *next = strchr(line, '\n');
        char *fields[6];
        char sender[128];
        char response[EMAIL_LOG_RESPONSE_SIZE];
        if (next) *next++ = '\0';
        if (split_fields(line, fields, 6) == 6) {
            hex_decode(fields[1], sender, sizeof(sender));
            hex_decode(fields[4], response, sizeof(response));
            json_arr_obj_open(json);
            json_add_int(json, "id", atoi(fields[0]));
            json_add_str(json, "sender", sender);
            json_add_str(json, "status", fields[2]);
            json_add_int(json, "attempts", atoi(fields[3]));
            json_add_str(json, "response", response);
            json_add_long(json, "created_at", atoll(fields[5]));
            json_obj_close(json);
        }
        line = next;
    }
    json_arr_close(json);
    char *result = json_finish(json);
    if (!result) return -1;
    copy_text(json_output, size, result);
    free(result);
    return 0;
}
