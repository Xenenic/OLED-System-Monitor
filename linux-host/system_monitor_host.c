/*
 * Copyright (c) 2026 Xenenic
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial 4.0
 * International License. To view a copy of this license, visit
 * https://creativecommons.org/licenses/by-nc/4.0/ or send a letter to
 * Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 *
 * You are free to:
 *  - Share: Copy and redistribute the material in any medium or format
 *  - Adapt: Remix, transform, and build upon the material
 *
 * Under the following terms:
 *  - Attribution: You must give appropriate credit, provide a link to the license,
 *    and indicate if changes were made.
 *  - NonCommercial: You may not use this material for commercial purposes.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct CpuTimes {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
} CpuTimes;

static int read_proc_stat_cpu(CpuTimes *out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[512];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    // Expected: cpu  user nice system idle iowait irq softirq steal guest guest_nice
    // We only need the first 8 fields after "cpu".
    memset(out, 0, sizeof(*out));
    int n = sscanf(line,
                   "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                   &out->user,
                   &out->nice,
                   &out->system,
                   &out->idle,
                   &out->iowait,
                   &out->irq,
                   &out->softirq,
                   &out->steal);
    return (n >= 4) ? 0 : -1;
}

static float cpu_util_percent(const CpuTimes *prev, const CpuTimes *cur) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long cur_idle = cur->idle + cur->iowait;

    unsigned long long prev_non_idle = prev->user + prev->nice + prev->system + prev->irq + prev->softirq + prev->steal;
    unsigned long long cur_non_idle = cur->user + cur->nice + cur->system + cur->irq + cur->softirq + cur->steal;

    unsigned long long prev_total = prev_idle + prev_non_idle;
    unsigned long long cur_total = cur_idle + cur_non_idle;

    unsigned long long totald = cur_total - prev_total;
    unsigned long long idled = cur_idle - prev_idle;

    if (totald == 0) return 0.0f;
    float util = (float)(totald - idled) * 100.0f / (float)totald;
    if (util < 0.0f) util = 0.0f;
    if (util > 100.0f) util = 100.0f;
    return util;
}

static int read_meminfo_kib(unsigned long long *mem_total,
                            unsigned long long *mem_avail,
                            unsigned long long *swap_total,
                            unsigned long long *swap_free) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    unsigned long long mt = 0, ma = 0, st = 0, sf = 0;
    char key[64];
    unsigned long long val;
    char unit[32];
    while (fscanf(f, "%63s %llu %31s", key, &val, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) {
            mt = val;
        } else if (strcmp(key, "MemAvailable:") == 0) {
            ma = val;
        } else if (strcmp(key, "SwapTotal:") == 0) {
            st = val;
        } else if (strcmp(key, "SwapFree:") == 0) {
            sf = val;
        }
    }
    fclose(f);

    if (mt == 0 || ma == 0) return -1;
    *mem_total = mt;
    *mem_avail = ma;
    *swap_total = st;
    *swap_free = sf;
    return 0;
}

static float percent_used(unsigned long long total, unsigned long long free_or_avail) {
    if (total == 0) return 0.0f;
    if (free_or_avail > total) free_or_avail = total;
    float used = (float)(total - free_or_avail) * 100.0f / (float)total;
    if (used < 0.0f) used = 0.0f;
    if (used > 100.0f) used = 100.0f;
    return used;
}

static int read_loadavg(float *l1, float *l5, float *l15) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;
    int n = fscanf(f, "%f %f %f", l1, l5, l15);
    fclose(f);
    return (n == 3) ? 0 : -1;
}

static int read_uptime_seconds(unsigned long long *uptime_seconds) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double up = 0.0;
    if (fscanf(f, "%lf", &up) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (up < 0.0) up = 0.0;
    *uptime_seconds = (unsigned long long)up;
    return 0;
}

static void format_uptime(unsigned long long sec, char *out, size_t out_len) {
    unsigned long long days = sec / 86400ULL;
    sec %= 86400ULL;
    unsigned long long hours = sec / 3600ULL;
    sec %= 3600ULL;
    unsigned long long mins = sec / 60ULL;
    unsigned long long secs = sec % 60ULL;
    snprintf(out, out_len, "%llud %02llu:%02llu:%02llu", days, hours, mins, secs);
}

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return 0;
    }
}

static int open_serial(const char *path, int baud) {
    int fd = open(path, O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);

    speed_t sp = baud_to_speed(baud);
    if (sp == 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (cfsetispeed(&tio, sp) != 0 || cfsetospeed(&tio, sp) != 0) {
        close(fd);
        return -1;
    }

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s -d /dev/ttyACM0 [-i seconds] [-b baud] [--stdout]\n"
            "  -d PATH     Serial device to write JSON to (e.g. /dev/ttyACM0)\n"
            "  -i SEC      Interval between updates (default: 1.0)\n"
            "  -b BAUD     Baud rate (default: 115200)\n"
            "  --stdout    Write JSON to stdout instead of serial\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *dev = NULL;
    double interval_s = 1.0;
    int baud = 115200;
    bool to_stdout = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dev = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval_s = strtod(argv[++i], NULL);
            if (interval_s <= 0.0) interval_s = 1.0;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stdout") == 0) {
            to_stdout = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    int out_fd = -1;
    if (!to_stdout) {
        if (!dev) {
            fprintf(stderr, "Error: serial device path required unless --stdout is used.\n");
            usage(argv[0]);
            return 2;
        }
        out_fd = open_serial(dev, baud);
        if (out_fd < 0) {
            fprintf(stderr, "Error: failed to open/configure serial device '%s': %s\n", dev, strerror(errno));
            return 1;
        }
    }

    CpuTimes prev, cur;
    if (read_proc_stat_cpu(&prev) != 0) {
        fprintf(stderr, "Error: failed to read /proc/stat\n");
        if (out_fd >= 0) close(out_fd);
        return 1;
    }

    struct timespec req;
    req.tv_sec = (time_t)interval_s;
    req.tv_nsec = (long)((interval_s - (double)req.tv_sec) * 1000000000.0);
    if (req.tv_nsec < 0) req.tv_nsec = 0;

    // Prime the CPU delta window.
    nanosleep(&req, NULL);

    while (1) {
        if (read_proc_stat_cpu(&cur) != 0) {
            fprintf(stderr, "Error: failed to read /proc/stat\n");
            break;
        }

        float cpu = cpu_util_percent(&prev, &cur);
        prev = cur;

        unsigned long long mem_total = 0, mem_avail = 0, swap_total = 0, swap_free = 0;
        if (read_meminfo_kib(&mem_total, &mem_avail, &swap_total, &swap_free) != 0) {
            fprintf(stderr, "Error: failed to read /proc/meminfo\n");
            break;
        }

        float ram = percent_used(mem_total, mem_avail);
        float swap = (swap_total == 0) ? 0.0f : percent_used(swap_total, swap_free);

        float l1 = 0.0f, l5 = 0.0f, l15 = 0.0f;
        if (read_loadavg(&l1, &l5, &l15) != 0) {
            fprintf(stderr, "Error: failed to read /proc/loadavg\n");
            break;
        }

        unsigned long long up_sec = 0;
        if (read_uptime_seconds(&up_sec) != 0) {
            fprintf(stderr, "Error: failed to read /proc/uptime\n");
            break;
        }
        char uptime[32];
        format_uptime(up_sec, uptime, sizeof(uptime));

        char json[256];
        int json_len = snprintf(json, sizeof(json),
                                "{\"cpu\":%.0f,\"ram\":%.0f,\"swap\":%.0f,\"load\":[%.2f,%.2f,%.2f],\"uptime\":\"%s\"}\n",
                                cpu, ram, swap, l1, l5, l15, uptime);
        if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
            fprintf(stderr, "Error: JSON buffer too small\n");
            break;
        }

        if (to_stdout) {
            fputs(json, stdout);
            fflush(stdout);
        } else {
            ssize_t w = write(out_fd, json, (size_t)json_len);
            if (w < 0) {
                fprintf(stderr, "Error: write() failed: %s\n", strerror(errno));
                break;
            }
        }

        nanosleep(&req, NULL);
    }

    if (out_fd >= 0) close(out_fd);
    return 1;
}
