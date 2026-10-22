// ESP32 ZeroTier storage wrapper — NVS + embedded test identity for QEMU
#include <string.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "ZeroTierOne.h"

static const char *TAG = "zt_storage";
static const char *NVS_NS = "zerotier";

static void make_key(enum ZT_StateObjectType type, const uint64_t id[2], char *out, size_t len) {
    uint32_t h = 0;
    if (id) {
        h = (uint32_t)((id[0] & 0xffffffffULL) ^ (id[0] >> 32) ^ (id[1] & 0xffffffffULL) ^ (id[1] >> 32));
    }
    snprintf(out, len, "z%d_%08x", (int)type, (unsigned int)h);
}

extern "C" void zt_state_put_impl(ZT_Node *node, void *uptr, void *tptr,
    enum ZT_StateObjectType type, const uint64_t id[2],
    const void *data, int len)
{
    if (len < 0) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            char key[16];
            make_key(type, id, key, sizeof(key));
            nvs_erase_key(h, key);
            nvs_commit(h);
            nvs_close(h);
        }
        return;
    }
    // cc-step123b Phase 5: [ZT_NETCONF_SIZE] — measure actual serialized dict size.
    // type==6 is ZT_STATE_OBJECT_NETWORK_CONFIG. This directly answers whether
    // ZT_NETWORKCONFIG_DICT_CAPACITY_OVERRIDE can safely be reduced from 4096 to 2048.
    if (type == 6) {
        ESP_LOGI(TAG, "[ZT_NETCONF_SIZE] type=NETWORK_CONFIG len=%d", len);
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    char key[16];
    make_key(type, id, key, sizeof(key));
    nvs_set_blob(h, key, data, (size_t)len);
    nvs_commit(h);
    nvs_close(h);
}

extern "C" int zt_state_get_impl(ZT_Node *node, void *uptr, void *tptr,
    enum ZT_StateObjectType type, const uint64_t id[2],
    void *data, unsigned int maxlen)
{
    // Try NVS first
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char key[16];
        make_key(type, id, key, sizeof(key));
        size_t actual = maxlen;
        esp_err_t err = nvs_get_blob(h, key, data, &actual);
        nvs_close(h);
        if (err == ESP_OK) return (int)actual;
    }

    return -1;
}
