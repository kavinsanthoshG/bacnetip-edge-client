#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned device_instance;
    char ip[64];
    unsigned port;
} DeviceInfo;

#ifdef __cplusplus
}
#endif