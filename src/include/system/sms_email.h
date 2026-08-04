#ifndef SMS_EMAIL_H
#define SMS_EMAIL_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int enabled;
    char smtp_server[256];
    int smtp_port;
    char smtp_user[256];
    char smtp_password[256];
    char from_addr[256];
    char to_addr[256];
    int password_set;
} SmsEmailConfig;

int sms_email_init(void);
void sms_email_deinit(void);
int sms_email_enqueue(const char *sender, const char *content, time_t timestamp);
int sms_email_get_config(SmsEmailConfig *config);
int sms_email_save_config(const SmsEmailConfig *config);
int sms_email_test(void);
int sms_email_get_logs(char *json_output, size_t size, int max_count);

#ifdef __cplusplus
}
#endif

#endif /* SMS_EMAIL_H */
