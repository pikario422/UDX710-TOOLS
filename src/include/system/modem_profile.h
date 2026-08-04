#ifndef MODEM_PROFILE_H
#define MODEM_PROFILE_H

#include <stddef.h>

typedef enum {
    MODEM_CMD_SMS_CNMI_ENABLED,
    MODEM_CMD_SMS_CNMI_DISABLED,
    MODEM_CMD_BAND_QUERY_LTE,
    MODEM_CMD_BAND_QUERY_NR,
    MODEM_CMD_RADIO_OFF,
    MODEM_CMD_RADIO_ON,
    MODEM_CMD_PDP_REACTIVATE,
    MODEM_CMD_BAND_RESET_LTE,
    MODEM_CMD_BAND_RESET_NR,
    MODEM_CMD_BAND_SET_LTE,
    MODEM_CMD_BAND_SET_NR,
    MODEM_CMD_CELL_LTE_SERVING,
    MODEM_CMD_CELL_LTE_NEIGHBOR,
    MODEM_CMD_CELL_NR_SERVING,
    MODEM_CMD_CELL_NR_NEIGHBOR,
    MODEM_CMD_CELL_UNLOCK_LTE,
    MODEM_CMD_CELL_UNLOCK_NR,
    MODEM_CMD_CELL_LOCK,
    MODEM_CMD_IMEI_QUERY,
    MODEM_CMD_IMEI_SET,
    MODEM_CMD_ICCID_QUERY,
    MODEM_CMD_IMSI_QUERY,
    MODEM_CMD_QOS_QUERY,
    MODEM_CMD_AIRPLANE_QUERY
} ModemProfileCommand;

typedef struct {
    char name[64];
    char default_modem_path[64];
    char slot1_modem_path[64];
    char slot2_modem_path[64];
    char default_context_path[128];
    int advanced_network_enabled;
    char advanced_strategy[16];
    int lte_band_offset;
    char nr_band_prefix[8];
    int cell_lte_rat;
    int cell_nr_rat;
    int cell_serving_value;
    int cell_arfcn_column;
    int cell_pci_column;
    int cell_band_column;
    int cell_sinr_column;
    int cell_rsrp_column;
    int cell_rsrq_column;
    char sms_cnmi_enabled[96];
    char sms_cnmi_disabled[96];
    char band_query_lte[96];
    char band_query_nr[96];
    char radio_off[96];
    char radio_on[96];
    char pdp_reactivate[96];
    char band_reset_lte[96];
    char band_reset_nr[96];
    char band_set_lte[128];
    char band_set_nr[128];
    char cell_lte_serving[96];
    char cell_lte_neighbor[96];
    char cell_nr_serving[96];
    char cell_nr_neighbor[96];
    char cell_unlock_lte[96];
    char cell_unlock_nr[96];
    char cell_lock[128];
    char cell_lock_lte[128];
    char cell_lock_nr[128];
    char imei_query[96];
    char imei_set[128];
    char iccid_query[96];
    char imsi_query[96];
    char qos_query[96];
    char qos_response_prefix[32];
    int qos_qci_index;
    int qos_downlink_index;
    int qos_uplink_index;
    char airplane_query[96];
} ModemProfile;

/* Reload the persisted profile after the database is available. */
int modem_profile_reload(void);
void modem_profile_get(ModemProfile *profile);
int modem_profile_save(const ModemProfile *profile);
int modem_profile_reset(void);
const char *modem_profile_command(ModemProfileCommand command);
const char *modem_profile_default_modem_path(void);
const char *modem_profile_slot_modem_path(int slot);
const char *modem_profile_default_context_path(void);
int modem_profile_advanced_network_enabled(void);

#endif
