#pragma once
#include "model.h"
#include <stddef.h>   // for size_t
#ifdef __cplusplus
extern "C" {
#endif


/* New: consume in-memory devices, prompt, and write one JSON with flags */
int watchlist_from_devices(const DeviceInfo *devices, size_t count,
                           const char *out_path);

#ifdef __cplusplus
}
#endif