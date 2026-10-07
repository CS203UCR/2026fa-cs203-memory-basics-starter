#ifdef HAVE_LINUX_PERF_EVENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "perfstats.h"

#include <linux/perf_event.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>

int cpu = 0;
struct timeval time_start, time_end;
double elapsed_time;

enum {
    COUNTER_CPU_CYCLES,
    INSTRUCTIONS,
    LS_LOADS,           // AMD Zen 3 ls_dispatch.ld_dispatch (0x0129)
    LS_STORES,          // AMD Zen 3 ls_dispatch.store_dispatch (0x0229)
    L1D_READ_MISSES,    // L1 Data Cache Read Misses
    LLC_MISSES,         // Last Level Cache Misses
};

static struct perf_event_attr attrs[] = {
    // 0: Cycles
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CPU_CYCLES },

    // 1: Instructions
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_INSTRUCTIONS },

    // 2: AMD Zen 3 LS Loads (Event 0x29, Unit Mask 0x01)
    { .type = PERF_TYPE_RAW,      .config = 0x0129 },

    // 3: AMD Zen 3 LS Stores (Event 0x29, Unit Mask 0x02)
    { .type = PERF_TYPE_RAW,      .config = 0x0229 },

    // 4: L1-Dcache Read Misses
    { 
        .type = PERF_TYPE_HW_CACHE, 
        .config = PERF_COUNT_HW_CACHE_L1D | 
                  (PERF_COUNT_HW_CACHE_OP_READ << 8) | 
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16) 
    },

    // 5: Last Level Cache (LLC) Misses
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CACHE_MISSES },
};

#define STAT_COUNT (sizeof(attrs) / sizeof(*attrs))
unsigned long long performance_counters[STAT_COUNT];
static int fds[STAT_COUNT];

static inline int sys_perf_event_open(struct perf_event_attr *attr,
                                     pid_t pid, int cpu, int group_fd,
                                     unsigned long flags) {
    attr->size = sizeof(*attr);
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

unsigned long long* flush_caches(void) {
    size_t num_elements = 16777216; // 128 MB buffer to blow out 16MB L3 cache
    unsigned long long* garbage = (unsigned long long*)malloc(sizeof(unsigned long long) * num_elements);
    if (!garbage) return NULL;
    for (size_t i = 0; i < num_elements; i++) {
        garbage[i] = 1;
    }
    return garbage;
}

int restore_cpufrequnecy(void) {
    char cmdline[1024];
    sprintf(cmdline, "/usr/sbin/changefreq 0 422334 4673823");
    return system(cmdline);
}

int change_cpufrequnecy(int MHz) {
    int KHz = MHz * 1000;
    char cmdline[1024];
    cpu = sched_getcpu();
    sprintf(cmdline, "/usr/sbin/changefreq 0 %d %d", KHz, KHz);
    return system(cmdline);
}

void perfstats_init(void) {
    int pid = 0; // Measure calling process/thread
    for (size_t i = 0; i < STAT_COUNT; i++) {
        attrs[i].size = sizeof(struct perf_event_attr);
        attrs[i].inherit = 0;
        attrs[i].disabled = 1;
        attrs[i].exclude_kernel = 1; // Do NOT count kernel / page fault overhead
        attrs[i].exclude_hv = 1;     // Exclude hypervisor
        attrs[i].enable_on_exec = 0;

        fds[i] = sys_perf_event_open(&attrs[i], pid, -1, -1, 0);
        if (fds[i] < 0) {
            fprintf(stderr, "Warning: Failed to open perf event %zu (config: 0x%llx)\n", 
                    i, (unsigned long long)attrs[i].config);
        }
    }
}

void perfstats_deinit(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            close(fds[i]);
            fds[i] = -1;
        }
    }
}

void perfstats_reset(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
        }
    }
}

void perfstats_enable(void) {
    perfstats_reset();
    gettimeofday(&time_start, NULL);
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
        }
    }
}

void perfstats_disable(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
        }
    }
    gettimeofday(&time_end, NULL);

    elapsed_time = ((time_end.tv_sec * 1000000.0 + time_end.tv_usec) -
                    (time_start.tv_sec * 1000000.0 + time_start.tv_usec)) / 1000000.0;
}

static void readall(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            unsigned long long val = 0;
            if (read(fds[i], &val, sizeof(val)) > 0) {
                performance_counters[i] = val;
            }
        }
    }
}

void perfstats_print_header(char *filename, char *header) {
    FILE *fout = fopen(filename, "w");
    if (fout) {
        fprintf(fout, "%s\n", header);
        fclose(fout);
    }
}

void perfstats_print(char *preamble, char *filename, char *epilogue) {
    FILE *fout = fopen(filename, "a");
    if (!fout) return;

    readall();

    unsigned long long ic          = performance_counters[INSTRUCTIONS];
    unsigned long long cycles      = performance_counters[COUNTER_CPU_CYCLES];
    unsigned long long loads       = performance_counters[LS_LOADS];
    unsigned long long stores      = performance_counters[LS_STORES];
    unsigned long long l1d_misses  = performance_counters[L1D_READ_MISSES];
    unsigned long long llc_misses  = performance_counters[LLC_MISSES];

    double cpi = (cycles > 0) ? (cycles/(double)ic) : 0.0;
    double ct = elapsed_time/cycles;
    double miss_rate = (loads + stores > 0) ? ((double)l1d_misses / (loads + stores)) : 0.0;

    fprintf(fout, "%s%llu,%llu,%lf,%lf,%lf,%lf,%llu,%llu%s",
            preamble,
            ic,
            cycles,
            cpi,
            ct,
            elapsed_time,
            miss_rate,
            l1d_misses,
            loads+stores,
            epilogue);

    fclose(fout);
}

#ifdef __cplusplus
}
#endif
#endif