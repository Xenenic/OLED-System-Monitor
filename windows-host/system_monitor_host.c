#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <psapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CpuTimes100ns {
    unsigned long long idle;
    unsigned long long kernel;
    unsigned long long user;
} CpuTimes100ns;

static unsigned long long filetime_to_ull(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (unsigned long long)u.QuadPart;
}

static int read_cpu_times(CpuTimes100ns *out) {
    FILETIME idle_ft, kernel_ft, user_ft;
    if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) return -1;

    out->idle = filetime_to_ull(idle_ft);
    out->kernel = filetime_to_ull(kernel_ft);
    out->user = filetime_to_ull(user_ft);
    return 0;
}

static float cpu_util_percent(const CpuTimes100ns *prev, const CpuTimes100ns *cur) {
    unsigned long long idle = cur->idle - prev->idle;
    unsigned long long kernel = cur->kernel - prev->kernel;
    unsigned long long user = cur->user - prev->user;

    // Per Microsoft docs, kernel includes idle time.
    unsigned long long total = kernel + user;
    if (total == 0ULL) return 0.0f;

    unsigned long long busy = total - idle;
    float util = (float)busy * 100.0f / (float)total;
    if (util < 0.0f) util = 0.0f;
    if (util > 100.0f) util = 100.0f;
    return util;
}

static float percent_used_ull(unsigned long long total, unsigned long long avail_or_free) {
    if (total == 0ULL) return 0.0f;
    if (avail_or_free > total) avail_or_free = total;
    float used = (float)(total - avail_or_free) * 100.0f / (float)total;
    if (used < 0.0f) used = 0.0f;
    if (used > 100.0f) used = 100.0f;
    return used;
}

static int read_ram_percent(float *ram_percent_out) {
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return -1;

    *ram_percent_out = percent_used_ull(ms.ullTotalPhys, ms.ullAvailPhys);
    return 0;
}

static int read_commit_percent(float *commit_percent_out) {
    PERFORMANCE_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!GetPerformanceInfo(&pi, sizeof(pi))) return -1;

    // CommitLimit is the max commit charge, roughly RAM + pagefile.
    // CommitTotal is the current commit charge.
    if (pi.CommitLimit == 0) {
        *commit_percent_out = 0.0f;
        return 0;
    }
    float used = (float)pi.CommitTotal * 100.0f / (float)pi.CommitLimit;
    if (used < 0.0f) used = 0.0f;
    if (used > 100.0f) used = 100.0f;
    *commit_percent_out = used;
    return 0;
}

typedef struct PagefileAgg {
    unsigned long long total_pages;
    unsigned long long used_pages;
} PagefileAgg;

static BOOL CALLBACK enum_pagefiles_cb(LPVOID ctx, PENUM_PAGE_FILE_INFORMATION info, LPCWSTR filename) {
    (void)filename;

    if (!ctx || !info) return FALSE;
    PagefileAgg *agg = (PagefileAgg *)ctx;
    agg->total_pages += (unsigned long long)info->TotalSize;
    agg->used_pages += (unsigned long long)info->TotalInUse;
    return TRUE;
}

static int read_pagefile_percent(float *pagefile_percent_out) {
    // Correct pagefile usage via PSAPI EnumPageFiles:
    // provides TotalSize/TotalInUse in pages for each paging file.
    PagefileAgg agg;
    memset(&agg, 0, sizeof(agg));

    if (!EnumPageFilesW(enum_pagefiles_cb, &agg)) {
        return -1;
    }

    if (agg.total_pages == 0ULL) {
        *pagefile_percent_out = 0.0f;
        return 0;
    }

    double used = (double)agg.used_pages * 100.0 / (double)agg.total_pages;
    if (used < 0.0) used = 0.0;
    if (used > 100.0) used = 100.0;
    *pagefile_percent_out = (float)used;
    return 0;
}

static int read_swap_percent(float *swap_percent_out) {
    // SWAP on Windows is mapped to pagefile usage (not overall commit charge).
    if (read_pagefile_percent(swap_percent_out) == 0) return 0;
    return read_commit_percent(swap_percent_out);
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

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s -d COM3 [-i seconds] [-b baud] [--stdout]\n"
            "  -d PORT     Serial device to write JSON to (e.g. COM3 or \\\\.\\COM10)\n"
            "  -i SEC      Interval between updates (default: 1.0)\n"
            "  -b BAUD     Baud rate (default: 115200)\n"
            "  --stdout    Write JSON to stdout instead of serial\n",
            argv0);
}

static HANDLE open_serial(const char *port, int baud) {
    HANDLE h = CreateFileA(port,
                           GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(h, &timeouts);

    return h;
}

static int write_all(HANDLE h, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        DWORD written = 0;
        if (!WriteFile(h, buf + off, (DWORD)(len - off), &written, NULL)) return -1;
        if (written == 0) return -1;
        off += (size_t)written;
    }
    return 0;
}

static double now_seconds_qpc(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
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

    HANDLE out_h = INVALID_HANDLE_VALUE;
    if (!to_stdout) {
        if (!dev) {
            fprintf(stderr, "Error: serial port required unless --stdout is used.\n");
            usage(argv[0]);
            return 2;
        }
        out_h = open_serial(dev, baud);
        if (out_h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            fprintf(stderr, "Error: failed to open/configure serial port '%s' (GetLastError=%lu)\n", dev, (unsigned long)err);
            return 1;
        }
    }

    CpuTimes100ns prev, cur;
    if (read_cpu_times(&prev) != 0) {
        fprintf(stderr, "Error: failed to read CPU times\n");
        if (out_h != INVALID_HANDLE_VALUE) CloseHandle(out_h);
        return 1;
    }

    DWORD cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (cores == 0) cores = 1;

    // EWMA load averages.
    double last_t = now_seconds_qpc();
    double la1 = 0.0, la5 = 0.0, la15 = 0.0;
    bool have_la = false;

    // Prime CPU delta window.
    Sleep((DWORD)(interval_s * 1000.0));

    while (1) {
        if (read_cpu_times(&cur) != 0) {
            fprintf(stderr, "Error: failed to read CPU times\n");
            break;
        }
        float cpu = cpu_util_percent(&prev, &cur);
        prev = cur;

        float ram = 0.0f;
        if (read_ram_percent(&ram) != 0) {
            fprintf(stderr, "Error: failed to read RAM usage\n");
            break;
        }

        float swap = 0.0f;
        if (read_swap_percent(&swap) != 0) {
            fprintf(stderr, "Error: failed to read swap/commit usage\n");
            break;
        }

        unsigned long long up_sec = (unsigned long long)(GetTickCount64() / 1000ULL);
        char uptime[32];
        format_uptime(up_sec, uptime, sizeof(uptime));

        // Windows has no native 1/5/15-min load averages like Unix.
        // We approximate "load" as CPU utilization scaled by logical core count,
        // then smooth it using standard exponential decay windows.
        double t = now_seconds_qpc();
        double dt = t - last_t;
        if (dt <= 0.0) dt = interval_s;
        last_t = t;

        double inst_load = (double)cores * ((double)cpu / 100.0);

        if (!have_la) {
            la1 = la5 = la15 = inst_load;
            have_la = true;
        } else {
            const double tau1 = 60.0;
            const double tau5 = 300.0;
            const double tau15 = 900.0;
            double a1 = exp(-dt / tau1);
            double a5 = exp(-dt / tau5);
            double a15 = exp(-dt / tau15);
            la1 = la1 * a1 + inst_load * (1.0 - a1);
            la5 = la5 * a5 + inst_load * (1.0 - a5);
            la15 = la15 * a15 + inst_load * (1.0 - a15);
        }

        char json[256];
        int json_len = snprintf(json, sizeof(json),
                                "{\"cpu\":%.0f,\"ram\":%.0f,\"page\":%.0f,\"load\":[%.2f,%.2f,%.2f],\"uptime\":\"%s\"}\n",
                                cpu, ram, swap, (float)la1, (float)la5, (float)la15, uptime);
        if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
            fprintf(stderr, "Error: JSON buffer too small\n");
            break;
        }

        if (to_stdout) {
            fputs(json, stdout);
            fflush(stdout);
        } else {
            if (write_all(out_h, json, (size_t)json_len) != 0) {
                DWORD err = GetLastError();
                fprintf(stderr, "Error: WriteFile failed (GetLastError=%lu)\n", (unsigned long)err);
                break;
            }
        }

        Sleep((DWORD)(interval_s * 1000.0));
    }

    if (out_h != INVALID_HANDLE_VALUE) CloseHandle(out_h);
    return 1;
}
