/**
 * @file
 * @brief command line tool that sends a BACnet Who-Is request to devices,
 * and prints any I-Am responses received.  This is useful for finding
 * devices on a network, or for finding devices that are in a specific
 * instance range.
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2006
 * @copyright SPDX-License-Identifier: MIT
 */
#include "model.h"
#include "discovery.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bactext.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/bactext.h"
#include "bacnet/version.h"
/* some demo stuff needed */
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacport.h"
#include <string.h>  /* added */




/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* global variables used in this file */
static int32_t Target_Object_Instance_Min = -1;
static int32_t Target_Object_Instance_Max = -1;
static bool Error_Detected = false;
/* debug info printing */
static bool BACnet_Debug_Enabled;

#define BAC_ADDRESS_MULT 1

struct address_entry {
    struct address_entry *next;
    uint8_t Flags;
    uint32_t device_id;
    unsigned max_apdu;
    BACNET_ADDRESS address;
};

static struct address_table {
    struct address_entry *first;
    struct address_entry *last;
} Address_Table = { 0 };

static struct address_entry *alloc_address_entry(void)
{
    struct address_entry *rval;
    rval = (struct address_entry *)calloc(1, sizeof(struct address_entry));
    if (Address_Table.first == 0) {
        Address_Table.first = Address_Table.last = rval;
    } else {
        Address_Table.last->next = rval;
        Address_Table.last = rval;
    }
    return rval;
}

static bool bacaddr_to_ip_port(const BACNET_ADDRESS *addr,
                               char *ip_buf, size_t ip_buf_sz,
                               unsigned *port_out);
                               
static size_t address_table_count(void) {
    size_t n = 0;
    struct address_entry *p = Address_Table.first;
    while (p) { n++; p = p->next; }
    return n;
}
/* ADD: helper to convert Address_Table -> DeviceInfo[]
   - Allocates an array; caller must free(*out_devices) */
static int address_table_to_devices(DeviceInfo **out_devices, size_t *out_count) {
    if (!out_devices || !out_count) return -1;
    *out_devices = NULL;
    *out_count = 0;

    size_t count = address_table_count();
    if (count == 0) return 0;

    DeviceInfo *arr = (DeviceInfo *)calloc(count, sizeof(DeviceInfo));
    if (!arr) return -1;

    struct address_entry *p = Address_Table.first;
    size_t i = 0;
    while (p && i < count) {
        char ip[64] = {0};
        unsigned port = 0;
        (void)bacaddr_to_ip_port(&p->address, ip, sizeof(ip), &port);

        arr[i].device_instance = p->device_id;
        strncpy(arr[i].ip, ip, sizeof(arr[i].ip) - 1);
        arr[i].port = port;
        i++;
        p = p->next;
    }

    *out_devices = arr;
    *out_count = i;
    return 0;
}
/* helper: count entries in Address_Table */




static void address_table_add(
    uint32_t device_id, unsigned max_apdu, BACNET_ADDRESS *src)
{
    struct address_entry *pMatch;
    uint8_t flags = 0;

    pMatch = Address_Table.first;
    while (pMatch) {
        if (pMatch->device_id == device_id) {
            if (bacnet_address_same(&pMatch->address, src)) {
                return;
            }
            flags |= BAC_ADDRESS_MULT;
            pMatch->Flags |= BAC_ADDRESS_MULT;
        }
        pMatch = pMatch->next;
    }

    pMatch = alloc_address_entry();

    pMatch->Flags = flags;
    pMatch->device_id = device_id;
    pMatch->max_apdu = max_apdu;
    pMatch->address = *src;

    return;
}

static void my_i_am_handler(
    uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    int len = 0;
    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;
    unsigned i = 0;

    (void)service_len;
    len = iam_decode_service_request(
        service_request, &device_id, &max_apdu, &segmentation, &vendor_id);
    if (BACnet_Debug_Enabled) {
        fprintf(stderr, "Received I-Am Request");
    }
    if (len != -1) {
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, " from %lu, MAC = ", (unsigned long)device_id);
            if ((src->mac_len == 6) && (src->len == 0)) {
                fprintf(
                    stderr, "%u.%u.%u.%u %02X%02X\n", (unsigned)src->mac[0],
                    (unsigned)src->mac[1], (unsigned)src->mac[2],
                    (unsigned)src->mac[3], (unsigned)src->mac[4],
                    (unsigned)src->mac[5]);
            } else {
                for (i = 0; i < src->mac_len; i++) {
                    fprintf(stderr, "%02X", (unsigned)src->mac[i]);
                    if (i < (src->mac_len - 1)) {
                        fprintf(stderr, ":");
                    }
                }
                fprintf(stderr, "\n");
            }
        }
        address_table_add(device_id, max_apdu, src);
    } else {
        if (BACnet_Debug_Enabled) {
            fprintf(stderr, ", but unable to decode it.\n");
        }
    }

    return;
}

/* helper: extract IPv4 and port from a BACnet/IP address into strings/ints */
static bool bacaddr_to_ip_port(const BACNET_ADDRESS *addr, char *ip_buf, size_t ip_buf_sz, unsigned *port_out)
{
    if (addr->mac_len == 6 && addr->len == 0) {
        snprintf(ip_buf, ip_buf_sz, "%u.%u.%u.%u",
                 (unsigned)addr->mac[0], (unsigned)addr->mac[1],
                 (unsigned)addr->mac[2], (unsigned)addr->mac[3]);
        unsigned port = ((unsigned)addr->mac[4] << 8) | (unsigned)addr->mac[5];
        if (port_out) *port_out = port;
        return true;
    }
    /* Not a plain BACnet/IP MAC, cannot extract IP:port */
    if (ip_buf && ip_buf_sz) ip_buf[0] = '\0';
    if (port_out) *port_out = 0;
    return false;
}

/* persist discovered devices as a JSON array */
static int persist_discovery_to_json(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("persist_discovery_to_json fopen");
        return -1;
    }
    fprintf(f, "[\n");
    struct address_entry *addr = Address_Table.first;
    bool first = true;
    while (addr) {
        char ip[32];
        unsigned port = 0;
        (void)bacaddr_to_ip_port(&addr->address, ip, sizeof(ip), &port);

        if (!first) {
            fprintf(f, ",\n");
        }
        first = false;
        fprintf(f,
            "  {\"device_instance\":%u,"
            "\"ip\":\"%s\","
            "\"port\":%u}",
            addr->device_id,
            ip,
            port);

        addr = addr->next;
    }
    fprintf(f, "\n]\n");
    fclose(f);
    return 0;
}
static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    (void)server;
    fprintf(
        stderr, "BACnet Abort: %s\n", bactext_abort_reason_name(abort_reason));
    Error_Detected = true;
}

static void
MyRejectHandler(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    /* FIXME: verify src and invoke id */
    (void)src;
    (void)invoke_id;
    fprintf(
        stderr, "BACnet Reject: %s\n",
        bactext_reject_reason_name(reject_reason));
    Error_Detected = true;
}
// ...existing code...
static void handler_who_am_i_json_print(
    uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    (void)service_request;
    (void)service_len;
    (void)src;
    /* TODO: decode Who-Am-I payload and print JSON if you need this feature */
}
// ...existing code...

static void init_service_handlers(void)
{
    Device_Init(NULL);
    /* Note: this applications doesn't need to handle who-is
       it is confusing for the user! */
    /* set the handler for all the services we don't implement
       It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* handle the reply (request) coming back */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
    /* handle any Who-Am-I requests we receive */
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_WHO_AM_I, handler_who_am_i_json_print);
    /* handle any errors coming back */
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}



static void print_macaddr(const uint8_t *addr, int len)
{
    int j = 0;

    while (j < len) {
        if (j != 0) {
            printf(":");
        }
        printf("%02X", addr[j]);
        j++;
    }
    while (j < MAX_MAC_LEN) {
        printf("   ");
        j++;
    }
}

static void print_address_cache(void)
{
    BACNET_ADDRESS address;
    unsigned total_addresses = 0;
    unsigned dup_addresses = 0;
    struct address_entry *addr;
    uint8_t local_sadr = 0;

    /*  NOTE: this string format is parsed by src/address.c,
       so these must be compatible. */

    printf(
        ";%-7s  %-20s %-5s %-20s %-4s\n", "Device", "MAC (hex)", "SNET",
        "SADR (hex)", "APDU");
    printf(";-------- -------------------- ----- -------------------- ----\n");

    addr = Address_Table.first;
    while (addr) {
        bacnet_address_copy(&address, &addr->address);
        total_addresses++;
        if (addr->Flags & BAC_ADDRESS_MULT) {
            dup_addresses++;
            printf(";");
        } else {
            printf(" ");
        }
        printf(" %-7u ", addr->device_id);
        print_macaddr(address.mac, address.mac_len);
        printf(" %-5hu ", address.net);
        if (address.net) {
            print_macaddr(address.adr, address.len);
        } else {
            print_macaddr(&local_sadr, 1);
        }
        printf(" %-4u ", (unsigned)addr->max_apdu);
        printf("\n");

        addr = addr->next;
    }
    printf(";\n; Total Devices: %u\n", total_addresses);
    if (dup_addresses) {
        printf("; * Duplicate Devices: %u\n", dup_addresses);
    }
}



int discovery_collect_devices(DeviceInfo **out_devices, size_t *out_count)
{
    // 1) Initialize outputs
    if (!out_devices || !out_count) return -1;
    *out_devices = NULL;
    *out_count = 0;

    // 2) BACnet stack setup (no CLI parsing)
    BACNET_ADDRESS src = {0};
    uint16_t pdu_len = 0;
    unsigned timeout_milliseconds = 0;
    unsigned delay_milliseconds = 100;
    struct mstimer apdu_timer = {0};
    struct mstimer datalink_timer = {0};
    BACNET_ADDRESS dest = {0};

    if (getenv("BACNET_DEBUG")) {
        BACnet_Debug_Enabled = true;
    }

    // Broadcast by default
    datalink_get_broadcast_address(&dest);

    // Device/stack init
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    init_service_handlers();
    address_init();
    dlenv_init();
    atexit(datalink_cleanup);

    // Timers: single discovery window (no retries/repeat)
    if (timeout_milliseconds == 0) {
        timeout_milliseconds = apdu_timeout() * apdu_retries();
    }
    mstimer_set(&apdu_timer, timeout_milliseconds);
    mstimer_set(&datalink_timer, 1000);

    // 3) Send Who-Is once
    Send_WhoIs_To_Network(&dest, Target_Object_Instance_Min, Target_Object_Instance_Max);

    // 4) Receive loop until the discovery window expires
    for (;;) {
        /* returns 0 bytes on timeout slice */
        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, delay_milliseconds);

        if (pdu_len) {
            /* I-Am responses land in my_i_am_handler and populate Address_Table */
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }
        if (Error_Detected) {
            break;
        }
        if (mstimer_expired(&datalink_timer)) {
            datalink_maintenance_timer(mstimer_interval(&datalink_timer) / 1000);
            mstimer_reset(&datalink_timer);
        }
        if (mstimer_expired(&apdu_timer)) {
            /* End of discovery window: stop looping */
            break;
        }
    }

    // Optional: keep this for console visibility
    print_address_cache();

    // 5) Build and return DeviceInfo[]
    return address_table_to_devices(out_devices, out_count);

    /* REMOVED: file persistence (persist_discovery_to_json and out_path).
       We no longer write discovered.json here; watchlist will handle
       writing a single file with 'selected' flags.
    */
}