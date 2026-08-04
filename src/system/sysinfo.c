 /**
 * @file sysinfo.c
 * @brief System information implementation (Go: system/sysinfo.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <glib.h>
#include "sysinfo.h"
#include "dbus_core.h"
#include "ofono.h"
#include "modem_profile.h"

#define SYSINFO_DBUS_TIMEOUT_MS 2000
#define IDENTITY_CACHE_TTL_US (5 * G_USEC_PER_SEC * 60)

typedef struct {
    char ril_path[64] = "unknown";
    char imei[20];
    char iccid[24];
    char imsi[20];
    char carrier[32];
    char manufacturer[64];
    char model[64];
    char revision[64];
    gint64 refreshed_at;
} ModemIdentityCache;

static GMutex g_identity_cache_lock;
static ModemIdentityCache g_identity_cache;

/* 读取文件内容 */
static int read_file(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 0;
}

/* 解析 /proc/meminfo */
static void parse_meminfo(SystemInfo *info) {
    char buf[4096];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) != 0) return;

    char *line = strtok(buf, "\n");
    while (line) {
        unsigned long val;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1) {
            info->total_ram = val / 1024;
        } else if (sscanf(line, "MemFree: %lu kB", &val) == 1) {
            info->free_ram = val / 1024;
        } else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1) {
            info->free_ram = val / 1024;
        } else if (sscanf(line, "Cached: %lu kB", &val) == 1) {
            info->cached_ram = val / 1024;
        }
        line = strtok(NULL, "\n");
    }
}

/* Read root filesystem capacity in MB. */
static void parse_storage_info(SystemInfo *info) {
    struct statvfs fs;
    unsigned long long block_size;

    if (statvfs("/", &fs) != 0) return;

    block_size = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
    info->storage_total = (unsigned long) ((block_size * fs.f_blocks) / (1024 * 1024));
    info->storage_free = (unsigned long) ((block_size * fs.f_bavail) / (1024 * 1024));
}

double get_uptime(void) {
    char buf[64];
    if (read_file("/proc/uptime", buf, sizeof(buf)) != 0) return -1;
    double uptime;
    if (sscanf(buf, "%lf", &uptime) == 1) return uptime;
    return -1;
}

static int get_default_ipv4(char *ip, size_t size) {
    FILE *route;
    char line[256];
    char interface[IFNAMSIZ] = {0};
    struct ifaddrs *addresses = NULL;
    struct ifaddrs *address;

    if (!ip || size == 0) return -1;
    ip[0] = '\0';
    route = fopen("/proc/net/route", "r");
    if (!route) return -1;

    while (fgets(line, sizeof(line), route)) {
        unsigned long destination;
        unsigned int flags;
        if (sscanf(line, "%15s %lx %*lx %x", interface, &destination,
                   &flags) == 3 && destination == 0 && (flags & 0x1)) {
            break;
        }
        interface[0] = '\0';
    }
    fclose(route);
    if (!interface[0] || getifaddrs(&addresses) != 0) return -1;

    for (address = addresses; address; address = address->ifa_next) {
        if (!address->ifa_addr || address->ifa_addr->sa_family != AF_INET ||
            strcmp(address->ifa_name, interface) != 0) continue;
        if (inet_ntop(AF_INET,
                      &((struct sockaddr_in *)address->ifa_addr)->sin_addr,
                      ip, size)) {
            freeifaddrs(addresses);
            return 0;
        }
    }
    freeifaddrs(addresses);
    return -1;
}


int get_serial(char *serial, size_t size) {
    char buf[1024];
    if (read_file("/home/cpuinfo", buf, sizeof(buf)) != 0) return -1;

    char *p = strstr(buf, "Serial");
    if (!p) return -1;

    /* 跳过 "Serial" 和非数字字符 */
    p += 6;
    while (*p && (*p < '0' || *p > '9')) p++;

    /* 提取数字 */
    size_t i = 0;
    while (*p >= '0' && *p <= '9' && i < size - 1) {
        serial[i++] = *p++;
    }
    serial[i] = '\0';
    return i > 0 ? 0 : -1;
}

int get_current_slot(char *slot, char *ril_path) {
    strcpy(slot, "unknown");
    strcpy(ril_path, "unknown");

    /* 使用 ofono D-Bus 接口获取数据卡 */
    char *datacard = ofono_get_datacard();
    if (!datacard) {
        return -1;
    }

    /* 解析路径 */
    if (strstr(datacard, modem_profile_slot_modem_path(1))) {
        strcpy(slot, "slot1");
        strncpy(ril_path, modem_profile_slot_modem_path(1), 63);
        ril_path[63] = '\0';
    } else if (strstr(datacard, modem_profile_slot_modem_path(2))) {
        strcpy(slot, "slot2");
        strncpy(ril_path, modem_profile_slot_modem_path(2), 63);
        ril_path[63] = '\0';
    } else {
        /* 其他路径格式，直接使用 */
        strncpy(ril_path, datacard, 63);
        ril_path[63] = '\0';
    }

    g_free(datacard);
    return 0;
}

static int get_signal_strength_for_path(const char *ril_path, char *strength,
                                        size_t size, int *percent, int *dbm) {
    int strength_val = 0, dbm_val = 0;

    strcpy(strength, "N/A");
    if (!ril_path || strcmp(ril_path, "unknown") == 0) {
        return -1;
    }

    /* 使用 ofono D-Bus 接口获取信号强度 */
    if (ofono_network_get_signal_strength(ril_path, &strength_val, &dbm_val, OFONO_TIMEOUT_MS) != 0) {
        return -1;
    }

    if (dbm_val > 0) dbm_val = -dbm_val;
    snprintf(strength, size, "%d%%, %d dBm", strength_val, dbm_val);
    if (percent) *percent = strength_val;
    if (dbm) *dbm = dbm_val;
    return 0;
}

int get_signal_strength(char *strength, size_t size) {
    char slot[16], ril_path[64];

    if (get_current_slot(slot, ril_path) != 0) {
        strcpy(strength, "N/A");
        return -1;
    }
    return get_signal_strength_for_path(ril_path, strength, size, NULL, NULL);
}

double get_thermal_temp(void) {
    DIR *dir;
    struct dirent *entry;
    double total = 0;
    int count = 0;

    dir = opendir("/sys/class/thermal");
    if (!dir) return -1;
    while ((entry = readdir(dir)) != NULL) {
        char path[256], output[64];
        long value;
        if (strncmp(entry->d_name, "thermal_zone", 12) != 0) continue;
        snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
        if (read_file(path, output, sizeof(output)) == 0 &&
            sscanf(output, "%ld", &value) == 1) {
            total += value / 1000.0;
            count++;
        }
    }
    closedir(dir);
    return count ? total / count : -1;
}


/* 前向声明 airplane.h 中的函数 */
extern int get_imei(char *imei, size_t size);
extern int get_iccid(char *iccid, size_t size);
extern int get_imsi(char *imsi, size_t size);
extern const char *get_carrier_from_imsi(const char *imsi);
extern int get_airplane_mode(void);

void sysinfo_invalidate_identity_cache(void) {
    g_mutex_lock(&g_identity_cache_lock);
    memset(&g_identity_cache, 0, sizeof(g_identity_cache));
    g_mutex_unlock(&g_identity_cache_lock);
}

static void copy_identity_to_info(const ModemIdentityCache *identity,
                                  SystemInfo *info) {
    strncpy(info->imei, identity->imei, sizeof(info->imei) - 1);
    strncpy(info->iccid, identity->iccid, sizeof(info->iccid) - 1);
    strncpy(info->imsi, identity->imsi, sizeof(info->imsi) - 1);
    strncpy(info->carrier, identity->carrier, sizeof(info->carrier) - 1);
    strncpy(info->modem_manufacturer, identity->manufacturer,
            sizeof(info->modem_manufacturer) - 1);
    strncpy(info->modem_model, identity->model,
            sizeof(info->modem_model) - 1);
    strncpy(info->modem_revision, identity->revision,
            sizeof(info->modem_revision) - 1);
}

static void populate_modem_identity(SystemInfo *info, const char *ril_path) {
    ModemIdentityCache identity = {0};
    gint64 now = g_get_monotonic_time();

    g_mutex_lock(&g_identity_cache_lock);
    if (ril_path && strcmp(ril_path, g_identity_cache.ril_path) == 0 &&
        g_identity_cache.refreshed_at > 0 &&
        now - g_identity_cache.refreshed_at < IDENTITY_CACHE_TTL_US) {
        copy_identity_to_info(&g_identity_cache, info);
        g_mutex_unlock(&g_identity_cache_lock);
        return;
    }
    g_mutex_unlock(&g_identity_cache_lock);

    if (ril_path) strncpy(identity.ril_path, ril_path, sizeof(identity.ril_path) - 1);
    get_imei(identity.imei, sizeof(identity.imei));
    get_iccid(identity.iccid, sizeof(identity.iccid));
    if (get_imsi(identity.imsi, sizeof(identity.imsi)) == 0) {
        const char *carrier = get_carrier_from_imsi(identity.imsi);
        if (carrier) strncpy(identity.carrier, carrier, sizeof(identity.carrier) - 1);
    }

    OfonoModemDetails details;
    if (ofono_get_modem_details(ril_path, &details, SYSINFO_DBUS_TIMEOUT_MS) == 0) {
        strncpy(identity.manufacturer, details.manufacturer,
                sizeof(identity.manufacturer) - 1);
        strncpy(identity.model, details.model, sizeof(identity.model) - 1);
        strncpy(identity.revision, details.revision, sizeof(identity.revision) - 1);
    }
    identity.refreshed_at = now;

    g_mutex_lock(&g_identity_cache_lock);
    g_identity_cache = identity;
    copy_identity_to_info(&g_identity_cache, info);
    g_mutex_unlock(&g_identity_cache_lock);
}

int get_system_info(SystemInfo *info) {
    struct utsname uts;

    /* 初始化默认值 */
    memset(info, 0, sizeof(SystemInfo));
    strcpy(info->hostname, "N/A");
    strcpy(info->sysname, "N/A");
    strcpy(info->release, "N/A");
    strcpy(info->version, "N/A");
    strcpy(info->machine, "N/A");
    strcpy(info->bridge_status, "N/A");
    strcpy(info->sim_slot, "N/A");
    strcpy(info->signal_strength, "N/A");
    strcpy(info->select_network_mode, "N/A");
    strcpy(info->network_mode, "N/A");
    strcpy(info->network_type, "N/A");
    strcpy(info->network_band, "N/A");
    info->is_activated = 1;

    /* uname 信息 */
    if (uname(&uts) == 0) {
        strncpy(info->sysname, uts.sysname, sizeof(info->sysname) - 1);
        strncpy(info->release, uts.release, sizeof(info->release) - 1);
        strncpy(info->version, uts.version, sizeof(info->version) - 1);
        strncpy(info->machine, uts.machine, sizeof(info->machine) - 1);
        strncpy(info->hostname, uts.nodename, sizeof(info->hostname) - 1);
    }

    /* 内存信息 */
    parse_meminfo(info);

    /* 根文件系统存储空间 */
    parse_storage_info(info);

    /* 运行时间 */
    info->uptime = get_uptime();

    /* 序列号 */
    get_serial(info->serial, sizeof(info->serial));

    /* SIM 卡槽 */
    char ril_path[64];
    if (get_current_slot(info->sim_slot, ril_path) == 0) {
        strncpy(info->network_mode, ril_path, sizeof(info->network_mode) - 1);
    }

    /* 信号强度 */
    get_signal_strength_for_path(ril_path, info->signal_strength,
                                 sizeof(info->signal_strength),
                                 &info->signal_percent, &info->signal_dbm);

    /* 默认路由的 IPv4 地址 */
    get_default_ipv4(info->ip, sizeof(info->ip));

    /* 温度 */
    info->thermal_temp = get_thermal_temp();

    /* SIM identity and modem details are stable between SIM switches. */
    populate_modem_identity(info, ril_path);

    char registered_name[32] = {0};
    char registered_technology[32] = {0};
    if (ofono_get_network_registration(ril_path, registered_name,
                                       sizeof(registered_name),
                                       registered_technology,
                                        sizeof(registered_technology),
                                        SYSINFO_DBUS_TIMEOUT_MS) == 0 &&
        registered_name[0] != '\0') {
        strncpy(info->carrier, registered_name, sizeof(info->carrier) - 1);
    }

    /* 飞行模式 */
    int airplane = get_airplane_mode();
    info->airplane_mode = (airplane == 1) ? 1 : 0;

    /* 网络模式选择 - 使用 ofono D-Bus 接口 */
    char mode_buf[64] = {0};
    if (strcmp(ril_path, "unknown") != 0 && strlen(ril_path) > 0) {
        if (ofono_network_get_mode_sync(ril_path, mode_buf, sizeof(mode_buf), OFONO_TIMEOUT_MS) == 0) {
            strncpy(info->select_network_mode, mode_buf, sizeof(info->select_network_mode) - 1);
        }
    }

    /* 网络类型和频段 */
    get_network_type_and_band(info->network_type, sizeof(info->network_type),
                              info->network_band, sizeof(info->network_band));
    if (strcmp(info->network_type, "N/A") == 0 && registered_technology[0] != '\0') {
        strncpy(info->network_type, registered_technology,
                sizeof(info->network_type) - 1);
    }

    /* QoS 签约速率 */
    get_qos_info(&info->qci, &info->downlink_rate, &info->uplink_rate);

    /* CPU 使用率 */
    info->cpu_usage = get_cpu_usage();

    return 0;
}


int get_qos_info(int *qci, int *downlink, int *uplink) {
    ModemProfile profile;
    char *result = NULL;
    char *p;
    int values[32] = {0};
    int count = 0;
    int max_index;

    if (!qci || !downlink || !uplink) return -1;
    *qci = 0;
    *downlink = 0;
    *uplink = 0;

    modem_profile_get(&profile);
    if (!profile.qos_query[0] ||
        execute_at(profile.qos_query, &result) != 0 || !result) {
        return -1;
    }

    p = profile.qos_response_prefix[0]
            ? strstr(result, profile.qos_response_prefix)
            : result;
    if (!p) {
        g_free(result);
        return -1;
    }

    if (profile.qos_response_prefix[0]) p += strlen(profile.qos_response_prefix);
    char *token = strtok(p, ",\n\r");
    while (token && count < (int)(sizeof(values) / sizeof(values[0]))) {
        values[count++] = atoi(token);
        token = strtok(NULL, ",\n\r");
    }

    max_index = profile.qos_qci_index;
    if (profile.qos_downlink_index > max_index) max_index = profile.qos_downlink_index;
    if (profile.qos_uplink_index > max_index) max_index = profile.qos_uplink_index;
    if (max_index >= count) {
        g_free(result);
        return -1;
    }

    *qci = values[profile.qos_qci_index];
    *downlink = values[profile.qos_downlink_index];
    *uplink = values[profile.qos_uplink_index];

    g_free(result);
    return 0;
}

/* 获取网络类型和频段 */
int get_network_type_and_band(char *net_type, size_t type_size, char *band, size_t band_size) {
    char tech[32] = {0};
    int band_num = 0;
    
    strncpy(net_type, "N/A", type_size - 1);
    strncpy(band, "N/A", band_size - 1);

    /* 使用C语言D-Bus API获取网络信息 */
    if (ofono_get_serving_cell_info(tech, sizeof(tech), &band_num) != 0) {
        return -1;
    }

    /* 判断网络类型 */
    if (strcmp(tech, "nr") == 0) {
        strncpy(net_type, "5G NR", type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "N%d", band_num);
        }
    } else if (strcmp(tech, "lte") == 0) {
        strncpy(net_type, "4G LTE", type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "B%d", band_num);
        }
    } else if (strlen(tech) > 0) {
        strncpy(net_type, tech, type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "%d", band_num);
        }
    }

    return 0;
}

/* 获取 CPU 使用率 - 通过读取 /proc/stat 计算 */
/* /proc/stat 格式: cpu user nice system idle iowait irq softirq steal guest guest_nice */
/* CPU使用率 = 100 - (idle_diff / total_diff * 100) */

/* 静态变量保存上次采样数据 */
static unsigned long long prev_user = 0, prev_nice = 0, prev_system = 0;
static unsigned long long prev_idle = 0, prev_iowait = 0, prev_irq = 0;
static unsigned long long prev_softirq = 0, prev_steal = 0;
static int cpu_initialized = 0;

double get_cpu_usage(void) {
    char buf[1024];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    
    /* 读取 /proc/stat */
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    
    if (fgets(buf, sizeof(buf), f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);
    
    /* 解析第一行 cpu 数据 */
    /* 格式: cpu  user nice system idle iowait irq softirq steal [guest guest_nice] */
    int ret = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    if (ret < 4) return 0;
    
    /* 如果某些字段不存在，设为0 */
    if (ret < 5) iowait = 0;
    if (ret < 6) irq = 0;
    if (ret < 7) softirq = 0;
    if (ret < 8) steal = 0;
    
    /* 首次调用，保存数据并返回0 */
    if (!cpu_initialized) {
        prev_user = user;
        prev_nice = nice;
        prev_system = system;
        prev_idle = idle;
        prev_iowait = iowait;
        prev_irq = irq;
        prev_softirq = softirq;
        prev_steal = steal;
        cpu_initialized = 1;
        return 0;
    }
    
    /* 计算差值 */
    unsigned long long user_diff = user - prev_user;
    unsigned long long nice_diff = nice - prev_nice;
    unsigned long long system_diff = system - prev_system;
    unsigned long long idle_diff = idle - prev_idle;
    unsigned long long iowait_diff = iowait - prev_iowait;
    unsigned long long irq_diff = irq - prev_irq;
    unsigned long long softirq_diff = softirq - prev_softirq;
    unsigned long long steal_diff = steal - prev_steal;
    
    /* 总时间差 */
    unsigned long long total_diff = user_diff + nice_diff + system_diff + idle_diff +
                                    iowait_diff + irq_diff + softirq_diff + steal_diff;
    
    /* 保存当前值供下次使用 */
    prev_user = user;
    prev_nice = nice;
    prev_system = system;
    prev_idle = idle;
    prev_iowait = iowait;
    prev_irq = irq;
    prev_softirq = softirq;
    prev_steal = steal;
    
    /* 避免除零 */
    if (total_diff == 0) return 0;
    
    /* CPU使用率 = 100 - idle百分比 */
    /* idle时间包括 idle + iowait */
    double idle_percent = (double)(idle_diff + iowait_diff) / total_diff * 100.0;
    double usage = 100.0 - idle_percent;
    
    /* 限制范围 0-100 */
    if (usage < 0) usage = 0;
    if (usage > 100) usage = 100;
    
    return usage;
}
