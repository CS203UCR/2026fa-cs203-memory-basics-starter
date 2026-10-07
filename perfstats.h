#ifndef PERFSTATS_H
#define PERFSTATS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_LINUX_PERF_EVENT_H

int restore_cpufrequnecy(void);
int change_cpufrequnecy(int MHz);
unsigned long long *flush_caches(void);

void perfstats_init(void);
void perfstats_deinit(void);
void perfstats_reset(void);
void perfstats_enable(void);
void perfstats_disable(void);

void perfstats_print_header(char *filename, char *header);
void perfstats_print(char *preamble, char *filename, char *epilogue);

// Prevents the compiler from moving memory instructions across the measurement boundary
#define PERF_COMPILER_BARRIER() asm volatile("" ::: "memory")

#else

static inline void perfstats_init(void) {}
static inline void perfstats_deinit(void) {}
static inline void perfstats_reset(void) {}
static inline void perfstats_enable(void) {}
static inline void perfstats_disable(void) {}
static inline void perfstats_print_header(char *f, char *h) { (void)f; (void)h; }
static inline void perfstats_print(char *p, char *f, char *e) { (void)p; (void)f; (void)e; }
#define PERF_COMPILER_BARRIER() do {} while(0)

#endif

#ifdef __cplusplus
}
#endif

#endif // PERFSTATS_H