#define _GNU_SOURCE
#include "list.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define COUNT_FORMAT (PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING)
#define L1D_ACCESS(op, result) ((uint64_t)PERF_COUNT_HW_CACHE_L1D | \
                                ((uint64_t)(op) << 8) | ((uint64_t)(result) << 16))

typedef struct {
    uint64_t value, enabled, running;
} Reading;

typedef struct {
    const char *name;
    uint32_t type;
    uint64_t config;
    int fd;
    Reading reading;
} Counter;

enum { INSTR, CYCLES, LOAD, MISS, STORE, RAW_LOAD, RAW_STORE, COUNTERS };

static int open_counter(const Counter *counter, int group_fd, bool group_leader)
{
    struct perf_event_attr attr = {
        .type = counter->type,
        .size = sizeof(attr),
        .config = counter->config,
        .read_format = COUNT_FORMAT | (group_leader ? PERF_FORMAT_GROUP : 0),
        .disabled = group_fd < 0,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    return (int)syscall(SYS_perf_event_open, &attr, 0, -1, group_fd,
                        PERF_FLAG_FD_CLOEXEC);
}

static bool control(Counter *counter, unsigned long request, unsigned long flags)
{
    if (ioctl(counter->fd, request, flags) == 0)
        return true;
    fprintf(stderr, "%s perf ioctl: %s\n", counter->name, strerror(errno));
    return false;
}

static bool start(Counter *counter, unsigned long flags)
{
    return control(counter, PERF_EVENT_IOC_RESET, flags) &&
           control(counter, PERF_EVENT_IOC_ENABLE, flags);
}

static bool read_bytes(int fd, void *buffer, size_t size)
{
    ssize_t result;
    do {
        result = read(fd, buffer, size);
    } while (result < 0 && errno == EINTR);
    return result >= 0 && (size_t)result == size;
}

static void finish(Counter *counters, bool *active, bool l1_pair)
{
    for (size_t i = 0; i < COUNTERS; ++i) {
        if (!active[i])
            continue;
        unsigned long flags = i == LOAD && l1_pair ? PERF_IOC_FLAG_GROUP : 0;
        active[i] = control(&counters[i], PERF_EVENT_IOC_DISABLE, flags);
    }
    for (size_t i = 0; i < COUNTERS; ++i) {
        if (!active[i])
            continue;
        if (i == LOAD && l1_pair) {
            struct { uint64_t nr, enabled, running, values[2]; } group;
            if (read_bytes(counters[i].fd, &group, sizeof(group)) && group.nr == 2) {
                counters[LOAD].reading = (Reading){group.values[0], group.enabled, group.running};
                counters[MISS].reading = (Reading){group.values[1], group.enabled, group.running};
            } else {
                fputs("L1D event group read failed\n", stderr);
            }
        } else if (!read_bytes(counters[i].fd, &counters[i].reading, sizeof(Reading))) {
            counters[i].reading = (Reading){0};
            fprintf(stderr, "%s counter read failed\n", counters[i].name);
        }
    }
}

static long double scaled(Reading r)
{
    return (long double)r.value * r.enabled / r.running;
}

static void show_counter(const Counter *counter)
{
    Reading r = counter->reading;
    if (!r.running) {
        printf("%s: N/A\n", counter->name);
        return;
    }
    printf("%s: %.0Lf (raw=%" PRIu64 ", scheduled=%.1Lf%%)\n",
           counter->name, scaled(r), r.value, 100.0L * r.running / r.enabled);
}

static bool parse_number(const char *str, int base, uint64_t *number)
{
    char *end;
    if (*str < '0' || *str > '9')
        return false;
    errno = 0;
    uintmax_t value = strtoumax(str, &end, base);
    if (errno || *end || value > UINT64_MAX)
        return false;
    *number = (uint64_t)value;
    return true;
}

static bool target_for(const char *mode, size_t count, size_t *key)
{
    if (strcmp(mode, "head") == 0)
        *key = 0;
    else if (strcmp(mode, "middle") == 0)
        *key = count / 2;
    else if (strcmp(mode, "tail") == 0)
        *key = count - 1;
    else if (strcmp(mode, "miss") == 0)
        *key = count;
    else
        return false;
    return true;
}

static LinkData *build_ring(uint64_t count, int payload_size)
{
    char payload[LINK_NODE_MAX_BYTES] = {0};
    LinkData *head = InitLink(payload_size, payload);
    if (!head)
        return NULL;
    for (uint64_t i = 1; i < count; ++i) {
        memcpy(payload, &i, sizeof(i));
        LinkData *old_tail = head->prev;
        AppendNode(head, payload_size, payload);
        if (head->prev == old_tail) {
            FreeLink(head);
            return NULL;
        }
    }
    return head;
}

int main(int argc, char **argv)
{
    uint64_t count = 100000, searches = 200, payload_size = 32;
    uint64_t raw_load = 0, raw_store = 0;
    const char *mode = argc >= 4 ? argv[3] : "tail";
    bool use_raw = argc == 8 && strcmp(argv[5], "--raw-mem") == 0;
    size_t key;
    if ((argc > 5 && !use_raw) ||
        (argc >= 2 && !parse_number(argv[1], 10, &count)) ||
        (argc >= 3 && !parse_number(argv[2], 10, &searches)) ||
        (argc >= 5 && !parse_number(argv[4], 10, &payload_size)) ||
        !count || count > SIZE_MAX || !searches || count > UINT64_MAX / searches ||
        payload_size < sizeof(uint64_t) || payload_size > LINK_NODE_MAX_BYTES - sizeof(LinkData) ||
        !target_for(mode, (size_t)count, &key) ||
        (use_raw && (!parse_number(argv[6], 0, &raw_load) ||
                     !parse_number(argv[7], 0, &raw_store)))) {
        fprintf(stderr, "Usage: %s [nodes [searches [head|middle|tail|miss [payload_bytes]]]]"
                        " [--raw-mem LOAD_EVENT STORE_EVENT]\n", argv[0]);
        return EXIT_FAILURE;
    }

    LinkData *head = build_ring(count, (int)payload_size);
    if (!head) {
        perror("Unable to allocate list");
        return EXIT_FAILURE;
    }
    char target[LINK_NODE_MAX_BYTES] = {0};
    uint64_t target_key = key;
    memcpy(target, &target_key, sizeof(target_key));

    Counter counters[COUNTERS] = {
        {.name="Instructions", .type=PERF_TYPE_HARDWARE, .config=PERF_COUNT_HW_INSTRUCTIONS},
        {.name="Cycles", .type=PERF_TYPE_HARDWARE, .config=PERF_COUNT_HW_CPU_CYCLES},
        {.name="L1D load accesses", .type=PERF_TYPE_HW_CACHE,
         .config=L1D_ACCESS(PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
        {.name="L1D load misses", .type=PERF_TYPE_HW_CACHE,
         .config=L1D_ACCESS(PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)},
        {.name="L1D store accesses", .type=PERF_TYPE_HW_CACHE,
         .config=L1D_ACCESS(PERF_COUNT_HW_CACHE_OP_WRITE, PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
        {.name="Raw retired loads", .type=PERF_TYPE_RAW, .config=raw_load},
        {.name="Raw retired stores", .type=PERF_TYPE_RAW, .config=raw_store},
    };
    bool active[COUNTERS] = {false};
    bool l1_pair = false;
    for (size_t i = 0; i < COUNTERS; ++i)
        counters[i].fd = -1;
    for (size_t i = 0; i < COUNTERS; ++i) {
        if (i >= RAW_LOAD && !use_raw)
            break;
        int group_fd = i == MISS ? counters[LOAD].fd : -1;
        if (i == MISS && group_fd < 0)
            continue;
        counters[i].fd = open_counter(&counters[i], group_fd, i == LOAD);
        if (counters[i].fd < 0)
            fprintf(stderr, "%s perf_event_open: %s\n", counters[i].name, strerror(errno));
        if (i == MISS) {
            l1_pair = counters[i].fd >= 0;
            if (!l1_pair) {
                close(counters[LOAD].fd);
                counters[LOAD].fd = open_counter(&counters[LOAD], -1, false);
                if (counters[LOAD].fd < 0)
                    fprintf(stderr, "%s perf_event_open: %s\n", counters[LOAD].name,
                            strerror(errno));
            }
        }
    }

    struct timespec begin, end;
    if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0) {
        perror("clock_gettime");
        FreeLink(head);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < COUNTERS; ++i) {
        if (counters[i].fd >= 0 && !(i == MISS && l1_pair))
            active[i] = start(&counters[i], (i == LOAD && l1_pair) ? PERF_IOC_FLAG_GROUP : 0);
    }
    uint64_t hits = 0;
    for (uint64_t i = 0; i < searches; ++i)
        hits += FindNode(head, (int)payload_size, target) != NULL;
    finish(counters, active, l1_pair);
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime");
        FreeLink(head);
        return EXIT_FAILURE;
    }
    for (size_t i = COUNTERS; i-- > 0;) {
        if (counters[i].fd >= 0)
            close(counters[i].fd);
    }

    printf("Nodes: %" PRIu64 "\nSearches: %" PRIu64 "\nTarget: %s\nHits: %" PRIu64 "\n",
           count, searches, mode, hits);
    printf("Payload bytes: %" PRIu64 "\n", payload_size);
    printf("Nodes visited (by workload): %" PRIu64 "\n",
           (key == count ? count : key + 1) * searches);
    printf("Elapsed incl. counter control: %.6f s\n",
           (double)(end.tv_sec - begin.tv_sec) + (end.tv_nsec - begin.tv_nsec) / 1e9);
    for (size_t i = 0; i < (use_raw ? COUNTERS : RAW_LOAD); ++i)
        show_counter(&counters[i]);

    Reading loads = counters[LOAD].reading, misses = counters[MISS].reading;
    Reading memory_loads = counters[use_raw ? RAW_LOAD : LOAD].reading;
    Reading memory_stores = counters[use_raw ? RAW_STORE : STORE].reading;
    if (memory_loads.running && memory_stores.running)
        printf("Memory accesses (%s): %.0Lf\n",
               use_raw ? "retired loads + stores" : "L1D loads + stores",
               scaled(memory_loads) + scaled(memory_stores));
    else if (memory_loads.running)
        printf("Memory accesses (loads only; stores unavailable): %.0Lf\n", scaled(memory_loads));
    else
        puts("Memory accesses: N/A");

    if (loads.running && misses.running && loads.value && misses.value <= loads.value)
        printf("L1D load hit rate: %.2Lf%%\n", 100.0L * (loads.value - misses.value) / loads.value);
    else
        puts("L1D load hit rate: N/A");

    FreeLink(head);
    return hits == (key == count ? 0 : searches) ? EXIT_SUCCESS : EXIT_FAILURE;
}
