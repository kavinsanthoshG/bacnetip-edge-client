#pragma once
#include "model.h"
#include <stddef.h>   // for size_t
#ifdef __cplusplus
extern "C" {
#endif



/* New: run discovery and return devices in-memory (no file persisted) */
int discovery_collect_devices(DeviceInfo **out_devices, size_t *out_count);

#ifdef __cplusplus
}
#endif