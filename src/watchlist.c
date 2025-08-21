#include "watchlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>


typedef struct {
    unsigned device_instance;
    char ip[64];
    unsigned port;
} DeviceInfo;

static void trim_spaces(char *s) {
    if (!s) return;
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

static char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* Very simple parser for the fixed schema:
   [ {"device_instance":N,"ip":"A.B.C.D","port":P}, ... ] */
static bool parse_discovered_json(const char *json, DeviceInfo **out, size_t *out_count) {
    const char *p = json;
    DeviceInfo *arr = NULL;
    size_t count = 0, cap = 0;

    while ((p = strstr(p, "\"device_instance\"")) != NULL) {
        DeviceInfo d;
        memset(&d, 0, sizeof d);

        // device_instance
        const char *q = strchr(p, ':');
        if (!q) break;
        d.device_instance = (unsigned)strtoul(q + 1, NULL, 10);

        // ip
        q = strstr(q, "\"ip\"");
        if (!q) break;
        q = strchr(q, ':'); if (!q) break;
        while (*q && *q != '\"') q++;
        if (*q == '\"') {
            q++;
            size_t i = 0;
            while (*q && *q != '\"' && i + 1 < sizeof(d.ip)) {
                d.ip[i++] = *q++;
            }
            d.ip[i] = '\0';
        }

        // port
        q = strstr(q, "\"port\"");
        if (!q) break;
        q = strchr(q, ':'); if (!q) break;
        d.port = (unsigned)strtoul(q + 1, NULL, 10);

        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 8;
            void *tmp = realloc(arr, new_cap * sizeof(DeviceInfo));
            if (!tmp) { free(arr); return false; }
            arr = (DeviceInfo *)tmp;
            cap = new_cap;
        }
        arr[count++] = d;

        p = q; // advance
    }

    *out = arr;
    *out_count = count;
    return true;
}

static bool prompt_exclude(const DeviceInfo *d) {
    printf("Exclude device %u (%s:%u) from watchlist? [y/N]: ",
           d->device_instance, d->ip, d->port);
    fflush(stdout);
    char line[32];
    if (!fgets(line, sizeof line, stdin)) {
        return false; // default include
    }
    trim_spaces(line);
    return (line[0] == 'y' || line[0] == 'Y');
}

static void print_selection(const DeviceInfo *devices, const bool *include, size_t count) {
    printf("\nThese are the devices that you have chosen to add to the watchlist:\n");
    for (size_t i = 0; i < count; i++) {
        if (include[i]) {
            printf(" - device_instance=%u (%s:%u)\n",
                   devices[i].device_instance, devices[i].ip, devices[i].port);
        }
    }
}

// static void print_usage(const char *prog) {
//     fprintf(stderr,
//         "Usage: %s [--input <path>] [--yes-to-all]\n"
//         "  --input <path>   Path to discovered.json (default: %s/data/discovered.json)\n"
//         "  --yes-to-all     Do not prompt; include all devices\n",
//         prog, PROJECT_ROOT);
// }

int device_watchlist(int argc, char **argv) {
    const char *in_path = PROJECT_ROOT "/data/discovery.json";
    // const char *in_path = "./data/discovered.json"; 
    bool yes_to_all = false;

    

    size_t len = 0;
    char *json = read_entire_file(in_path, &len);
    if (!json) {
        fprintf(stderr, "Failed to read %s\n", in_path);
        return 1;
    }

    DeviceInfo *devices = NULL;
    size_t count = 0;
    if (!parse_discovered_json(json, &devices, &count)) {
        fprintf(stderr, "Failed to parse discovered.json\n");
        free(json);
        return 1;
    }
    free(json);

    if (count == 0) {
        printf("No devices found in %s\n", in_path);
        free(devices);
        return 0;
    }

    bool *include = (bool *)calloc(count, sizeof(bool));
    if (!include) { free(devices); return 1; }
    for (size_t i = 0; i < count; i++) include[i] = true;

    if (!yes_to_all) {
        for (size_t i = 0; i < count; i++) {
            if (prompt_exclude(&devices[i])) {
                include[i] = false;
            }
        }
    }

    print_selection(devices, include, count);

    free(include);
    free(devices);
    return 0;
}