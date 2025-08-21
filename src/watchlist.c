#include "watchlist.h"
#include "model.h"   // use shared DeviceInfo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>  /* added */


// ...existing code...
// REMOVE: local typedef DeviceInfo { ... } – we now use model.h
// REMOVE: read_entire_file(...) and parse_discovered_json(...) – no file input
// ...existing code...

static void trim_spaces(char *s) {
    if (!s) return;
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

static bool prompt_exclude(const DeviceInfo *d) {
    printf("Exclude device %u (%s:%u) from watchlist? [y/N]: ",
           d->device_instance, d->ip, d->port);
    fflush(stdout);
    char line[32];
    if (!fgets(line, sizeof line, stdin)) return false; // default include
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

static int write_devices_with_flags(const char *path,
                                    const DeviceInfo *devices,
                                    const bool *include, size_t count)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen"); return -1; }
    fprintf(f, "{\n  \"devices\": [\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(f,
            "    {\"device_instance\":%u,\"ip\":\"%s\",\"port\":%u,\"selected\":%s}%s\n",
            devices[i].device_instance, devices[i].ip, devices[i].port,
            include[i] ? "true" : "false",
            (i + 1 < count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return 0;
}

// New API: consume in-memory devices, prompt, and write one file with flags
int watchlist_from_devices(const DeviceInfo *devices, size_t count, const char *out_path)
{
    if (!devices || count == 0) {
        printf("No devices provided to watchlist.\n");
        return 0;
    }
    bool *include = (bool *)calloc(count, sizeof(bool));
    if (!include) return 1;
    for (size_t i = 0; i < count; i++) include[i] = true;

    for (size_t i = 0; i < count; i++) {
        if (prompt_exclude(&devices[i])) include[i] = false;
    }

    print_selection(devices, include, count);

    const char *path = out_path ? out_path : PROJECT_ROOT "/data/inventory.json";
    if (write_devices_with_flags(path, devices, include, count) == 0) {
        fprintf(stderr, "Watchlist saved to %s\n", path);
    }

    free(include);
    return 0;
}

