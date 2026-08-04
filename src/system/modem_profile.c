#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "database.h"
#include "modem_profile.h"

#define PROFILE_KEY_PREFIX "modem_profile."

typedef struct {
    const char *key;
    size_t offset;
    size_t size;
    const char *default_value;
} ProfileStringField;

static const ModemProfile g_default_profile = {
    "UDX710 SPRD default",
    "/ril_0",
    "/ril_0",
    "/ril_1",
    "/ril_0/context2",
    1,
    "bitmask_matrix",
    100,
    "50",
    4, 9, 1,
    6, 7, 8, 10, 12, 13,
    "AT+CNMI=3,2,0,1,0",
    "AT+CNMI=3,1,0,1,0",
    "AT+SPLBAND=0",
    "AT+SPLBAND=3",
    "AT+SFUN=5",
    "AT+SFUN=4",
    "AT+CGACT=0,1",
    "AT+SPLBAND=1,0,0,0,0,0",
    "AT+SPLBAND=2,0,0,0,0",
    "AT+SPLBAND=1,0,%d,0,%d,0",
    "AT+SPLBAND=2,%d,0,%d,0",
    "AT+SPENGMD=0,6,0",
    "AT+SPENGMD=0,6,6",
    "AT+SPENGMD=0,14,1",
    "AT+SPENGMD=0,14,2",
    "AT+SPFORCEFRQ=12,0",
    "AT+SPFORCEFRQ=16,0",
    "AT+SPFORCEFRQ=%s,2,%s,%s",
    "AT+SPFORCEFRQ=12,2,%s,%s",
    "AT+SPFORCEFRQ=16,2,%s,%s",
    "AT+SPIMEI?",
    "AT+SPIMEI=%d,\"%s\""
};

#define PROFILE_FIELD(member, key) { key, offsetof(ModemProfile, member), sizeof(((ModemProfile *)0)->member), g_default_profile.member }
static const ProfileStringField g_fields[] = {
    PROFILE_FIELD(name, "name"),
    PROFILE_FIELD(default_modem_path, "default_modem_path"),
    PROFILE_FIELD(slot1_modem_path, "slot1_modem_path"),
    PROFILE_FIELD(slot2_modem_path, "slot2_modem_path"),
    PROFILE_FIELD(default_context_path, "default_context_path"),
    PROFILE_FIELD(advanced_strategy, "advanced_strategy"),
    PROFILE_FIELD(nr_band_prefix, "nr_band_prefix"),
    PROFILE_FIELD(sms_cnmi_enabled, "sms_cnmi_enabled"),
    PROFILE_FIELD(sms_cnmi_disabled, "sms_cnmi_disabled"),
    PROFILE_FIELD(band_query_lte, "band_query_lte"),
    PROFILE_FIELD(band_query_nr, "band_query_nr"),
    PROFILE_FIELD(radio_off, "radio_off"),
    PROFILE_FIELD(radio_on, "radio_on"),
    PROFILE_FIELD(pdp_reactivate, "pdp_reactivate"),
    PROFILE_FIELD(band_reset_lte, "band_reset_lte"),
    PROFILE_FIELD(band_reset_nr, "band_reset_nr"),
    PROFILE_FIELD(band_set_lte, "band_set_lte"),
    PROFILE_FIELD(band_set_nr, "band_set_nr"),
    PROFILE_FIELD(cell_lte_serving, "cell_lte_serving"),
    PROFILE_FIELD(cell_lte_neighbor, "cell_lte_neighbor"),
    PROFILE_FIELD(cell_nr_serving, "cell_nr_serving"),
    PROFILE_FIELD(cell_nr_neighbor, "cell_nr_neighbor"),
    PROFILE_FIELD(cell_unlock_lte, "cell_unlock_lte"),
    PROFILE_FIELD(cell_unlock_nr, "cell_unlock_nr"),
    PROFILE_FIELD(cell_lock, "cell_lock"),
    PROFILE_FIELD(cell_lock_lte, "cell_lock_lte"),
    PROFILE_FIELD(cell_lock_nr, "cell_lock_nr"),
    PROFILE_FIELD(imei_query, "imei_query"),
    PROFILE_FIELD(imei_set, "imei_set")
};

static ModemProfile g_profile_cache;
static pthread_mutex_t g_profile_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_profile_loaded = 0;

typedef struct { const char *key; size_t offset; int default_value; } ProfileIntField;
#define PROFILE_INT_FIELD(member, key) { key, offsetof(ModemProfile, member), g_default_profile.member }
static const ProfileIntField g_int_fields[] = {
    PROFILE_INT_FIELD(lte_band_offset, "lte_band_offset"),
    PROFILE_INT_FIELD(cell_lte_rat, "cell_lte_rat"),
    PROFILE_INT_FIELD(cell_nr_rat, "cell_nr_rat"),
    PROFILE_INT_FIELD(cell_serving_value, "cell_serving_value"),
    PROFILE_INT_FIELD(cell_arfcn_column, "cell_arfcn_column"),
    PROFILE_INT_FIELD(cell_pci_column, "cell_pci_column"),
    PROFILE_INT_FIELD(cell_band_column, "cell_band_column"),
    PROFILE_INT_FIELD(cell_sinr_column, "cell_sinr_column"),
    PROFILE_INT_FIELD(cell_rsrp_column, "cell_rsrp_column"),
    PROFILE_INT_FIELD(cell_rsrq_column, "cell_rsrq_column")
};

static void make_key(char *key, size_t size, const char *field) {
    snprintf(key, size, "%s%s", PROFILE_KEY_PREFIX, field);
}

static void profile_load_from_rows(ModemProfile *profile) {
    char rows[4096] = {0};
    const char separator[] = "\x1f";
    char *line;
    size_t i;

    *profile = g_default_profile;
    if (db_query_rows("SELECT key,value FROM config WHERE key LIKE 'modem_profile.%';",
                      separator, rows, sizeof(rows)) != 0) {
        return;
    }

    line = strtok(rows, "\n");
    while (line) {
        char *value = strchr(line, separator[0]);
        if (value) {
            *value++ = '\0';
            if (strcmp(line, PROFILE_KEY_PREFIX "advanced_network_enabled") == 0) {
                profile->advanced_network_enabled = atoi(value) ? 1 : 0;
            } else if (strncmp(line, PROFILE_KEY_PREFIX,
                               strlen(PROFILE_KEY_PREFIX)) == 0) {
                const char *field_name = line + strlen(PROFILE_KEY_PREFIX);
                for (i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]); i++) {
                    if (strcmp(field_name, g_fields[i].key) == 0) {
                        char *target = (char *)profile + g_fields[i].offset;
                        strncpy(target, value, g_fields[i].size - 1);
                        target[g_fields[i].size - 1] = '\0';
                        break;
                    }
                }
                for (i = 0; i < sizeof(g_int_fields) / sizeof(g_int_fields[0]); i++) {
                    if (strcmp(field_name, g_int_fields[i].key) == 0) {
                        *(int *)((char *)profile + g_int_fields[i].offset) = atoi(value);
                        break;
                    }
                }
            }
        }
        line = strtok(NULL, "\n");
    }

}

int modem_profile_reload(void) {
    ModemProfile profile;
    profile_load_from_rows(&profile);
    pthread_mutex_lock(&g_profile_mutex);
    g_profile_cache = profile;
    g_profile_loaded = 1;
    pthread_mutex_unlock(&g_profile_mutex);
    return 0;
}

void modem_profile_get(ModemProfile *profile) {
    if (!profile) return;
    pthread_mutex_lock(&g_profile_mutex);
    *profile = g_profile_loaded ? g_profile_cache : g_default_profile;
    pthread_mutex_unlock(&g_profile_mutex);
}

int modem_profile_save(const ModemProfile *profile) {
    size_t i;
    char sql[16384];
    size_t used = 0;
    if (!profile || !profile->name[0] || !profile->default_modem_path[0]) return -1;

    used += snprintf(sql + used, sizeof(sql) - used, "BEGIN;");
    for (i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]); i++) {
        char key[96];
        char escaped_key[192];
        char escaped_value[256];
        const char *value = (const char *)profile + g_fields[i].offset;
        make_key(key, sizeof(key), g_fields[i].key);
        db_escape_string(key, escaped_key, sizeof(escaped_key));
        db_escape_string(value, escaped_value, sizeof(escaped_value));
        if (used >= sizeof(sql) || snprintf(sql + used, sizeof(sql) - used,
            "INSERT OR REPLACE INTO config (key,value) VALUES ('%s','%s');",
            escaped_key, escaped_value) >= (int)(sizeof(sql) - used)) return -1;
        used = strlen(sql);
    }
    for (i = 0; i < sizeof(g_int_fields) / sizeof(g_int_fields[0]); i++) {
        char key[96];
        int value = *(const int *)((const char *)profile + g_int_fields[i].offset);
        make_key(key, sizeof(key), g_int_fields[i].key);
        if (used >= sizeof(sql) || snprintf(sql + used, sizeof(sql) - used,
            "INSERT OR REPLACE INTO config (key,value) VALUES ('%s','%d');", key, value) >= (int)(sizeof(sql) - used)) return -1;
        used = strlen(sql);
    }
    if (used >= sizeof(sql) || snprintf(sql + used, sizeof(sql) - used,
        "INSERT OR REPLACE INTO config (key,value) VALUES ('%s','%d');COMMIT;",
        PROFILE_KEY_PREFIX "advanced_network_enabled",
        profile->advanced_network_enabled ? 1 : 0) >= (int)(sizeof(sql) - used)) return -1;
    if (db_execute_safe(sql) != 0) return -1;

    pthread_mutex_lock(&g_profile_mutex);
    g_profile_cache = *profile;
    g_profile_loaded = 1;
    pthread_mutex_unlock(&g_profile_mutex);
    return 0;
}

int modem_profile_reset(void) {
    return modem_profile_save(&g_default_profile);
}

const char *modem_profile_command(ModemProfileCommand command) {
    static __thread char value[128];
    ModemProfile profile;
    const char *source = "";
    modem_profile_get(&profile);
    switch (command) {
    case MODEM_CMD_SMS_CNMI_ENABLED: source = profile.sms_cnmi_enabled; break;
    case MODEM_CMD_SMS_CNMI_DISABLED: source = profile.sms_cnmi_disabled; break;
    case MODEM_CMD_BAND_QUERY_LTE: source = profile.band_query_lte; break;
    case MODEM_CMD_BAND_QUERY_NR: source = profile.band_query_nr; break;
    case MODEM_CMD_RADIO_OFF: source = profile.radio_off; break;
    case MODEM_CMD_RADIO_ON: source = profile.radio_on; break;
    case MODEM_CMD_PDP_REACTIVATE: source = profile.pdp_reactivate; break;
    case MODEM_CMD_BAND_RESET_LTE: source = profile.band_reset_lte; break;
    case MODEM_CMD_BAND_RESET_NR: source = profile.band_reset_nr; break;
    case MODEM_CMD_BAND_SET_LTE: source = profile.band_set_lte; break;
    case MODEM_CMD_BAND_SET_NR: source = profile.band_set_nr; break;
    case MODEM_CMD_CELL_LTE_SERVING: source = profile.cell_lte_serving; break;
    case MODEM_CMD_CELL_LTE_NEIGHBOR: source = profile.cell_lte_neighbor; break;
    case MODEM_CMD_CELL_NR_SERVING: source = profile.cell_nr_serving; break;
    case MODEM_CMD_CELL_NR_NEIGHBOR: source = profile.cell_nr_neighbor; break;
    case MODEM_CMD_CELL_UNLOCK_LTE: source = profile.cell_unlock_lte; break;
    case MODEM_CMD_CELL_UNLOCK_NR: source = profile.cell_unlock_nr; break;
    case MODEM_CMD_CELL_LOCK: source = profile.cell_lock; break;
    case MODEM_CMD_IMEI_QUERY: source = profile.imei_query; break;
    case MODEM_CMD_IMEI_SET: source = profile.imei_set; break;
    default: break;
    }
    strncpy(value, source, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    return value;
}

const char *modem_profile_default_modem_path(void) {
    static __thread char value[64];
    ModemProfile profile;
    modem_profile_get(&profile);
    strncpy(value, profile.default_modem_path, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    return value;
}

const char *modem_profile_slot_modem_path(int slot) {
    static __thread char value[64];
    ModemProfile profile;
    modem_profile_get(&profile);
    strncpy(value, slot == 2 ? profile.slot2_modem_path : profile.slot1_modem_path,
            sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    return value;
}

const char *modem_profile_default_context_path(void) {
    static __thread char value[128];
    ModemProfile profile;
    modem_profile_get(&profile);
    strncpy(value, profile.default_context_path, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    return value;
}

int modem_profile_advanced_network_enabled(void) {
    ModemProfile profile;
    modem_profile_get(&profile);
    return profile.advanced_network_enabled;
}
