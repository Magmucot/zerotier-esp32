#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>

// --- LwIP Mock State ---
static tcpip_callback_fn s_pending_cb = nullptr;
static void* s_pending_arg = nullptr;
static bool s_delay_callback = false;
static bool s_simulate_timeout = false;
static TaskHandle_t s_current_task = (TaskHandle_t)0x1000;
static TaskHandle_t s_tcpip_task = (TaskHandle_t)0x2000;

void mock_lwip_reset(void) {
    s_pending_cb = nullptr;
    s_pending_arg = nullptr;
    s_delay_callback = false;
    s_simulate_timeout = false;
}

void mock_lwip_set_callback_delay(bool delay) {
    s_delay_callback = delay;
}

void mock_lwip_set_simulate_timeout(bool timeout) {
    s_simulate_timeout = timeout;
}

bool mock_lwip_has_pending_callback(void) {
    return s_pending_cb != nullptr;
}

void mock_lwip_execute_pending_callback(void) {
    if (s_pending_cb) {
        tcpip_callback_fn cb = s_pending_cb;
        void* arg = s_pending_arg;
        s_pending_cb = nullptr;
        s_pending_arg = nullptr;
        // Run in simulated tcpip thread context
        TaskHandle_t prev = s_current_task;
        s_current_task = s_tcpip_task;
        cb(arg);
        s_current_task = prev;
    }
}

tcpip_callback_fn mock_lwip_get_last_callback(void) {
    return s_pending_cb;
}

void* mock_lwip_get_last_callback_arg(void) {
    return s_pending_arg;
}

// --- LwIP sys_sem ---
err_t sys_sem_new(sys_sem_t* sem, u8_t count) {
    if (!sem) return ERR_VAL;
    sys_sem_t s = (sys_sem_t)malloc(sizeof(struct sys_sem_record));
    if (!s) return ERR_MEM;
    s->count = count;
    s->valid = true;
    s->magic = SYS_SEM_MAGIC;
    *sem = s;
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t* sem) {
    if (sem && *sem) {
        sys_sem_t s = *sem;
        if (s->magic == SYS_SEM_MAGIC && s->valid) {
            s->count++;
        }
    }
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout) {
    if (s_simulate_timeout) {
        return SYS_ARCH_TIMEOUT;
    }
    if (sem && *sem) {
        sys_sem_t s = *sem;
        if (s->magic == SYS_SEM_MAGIC && s->valid) {
            if (s->count > 0) {
                s->count--;
                return 0; // Signaled
            }
        }
    }
    return SYS_ARCH_TIMEOUT;
}

void sys_sem_wait(sys_sem_t* sem) {
    if (sem && *sem) {
        sys_sem_t s = *sem;
        if (s->magic == SYS_SEM_MAGIC && s->valid && s->count > 0) {
            s->count--;
        }
    }
}

void sys_sem_free(sys_sem_t* sem) {
    if (sem && *sem) {
        sys_sem_t s = *sem;
        s->valid = false;
        s->magic = 0xDEADBEEF;
        free(s);
        *sem = nullptr;
    }
}

int sys_sem_valid(sys_sem_t* sem) {
    return (sem && *sem && (*sem)->valid && (*sem)->magic == SYS_SEM_MAGIC) ? 1 : 0;
}

void sys_sem_set_invalid(sys_sem_t* sem) {
    if (sem) *sem = nullptr;
}

// --- LwIP tcpip ---
err_t tcpip_callback(tcpip_callback_fn function, void *ctx) {
    s_pending_cb = function;
    s_pending_arg = ctx;
    if (!s_delay_callback) {
        mock_lwip_execute_pending_callback();
    }
    return ERR_OK;
}

err_t tcpip_input(struct pbuf *p, struct netif *inp) {
    return ERR_OK;
}

// --- LwIP netif ---
struct netif* netif_add(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask,
                        const ip4_addr_t *gw, void *state, netif_init_fn init, netif_input_fn input) {
    if (!netif) return nullptr;
    memset(netif, 0, sizeof(*netif));
    if (ipaddr) netif->ip_addr = *ipaddr;
    if (netmask) netif->netmask = *netmask;
    if (gw) netif->gw = *gw;
    netif->state = state;
    netif->input = input;
    if (init && init(netif) != ERR_OK) {
        return nullptr;
    }
    return netif;
}

void netif_remove(struct netif *netif) {
    if (netif) {
        netif->flags &= ~(NETIF_FLAG_UP | NETIF_FLAG_LINK_UP);
    }
}

void netif_set_addr(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask, const ip4_addr_t *gw) {
    if (netif) {
        if (ipaddr) netif->ip_addr = *ipaddr;
        if (netmask) netif->netmask = *netmask;
        if (gw) netif->gw = *gw;
    }
}

void netif_set_up(struct netif *netif) {
    if (netif) netif->flags |= NETIF_FLAG_UP;
}

void netif_set_down(struct netif *netif) {
    if (netif) netif->flags &= ~NETIF_FLAG_UP;
}

void netif_set_link_up(struct netif *netif) {
    if (netif) netif->flags |= NETIF_FLAG_LINK_UP;
}

void netif_set_link_down(struct netif *netif) {
    if (netif) netif->flags &= ~NETIF_FLAG_LINK_UP;
}

struct pbuf* pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type) {
    struct pbuf* p = (struct pbuf*)malloc(sizeof(struct pbuf) + length);
    if (!p) return nullptr;
    memset(p, 0, sizeof(*p));
    p->payload = (char*)p + sizeof(struct pbuf);
    p->len = length;
    p->tot_len = length;
    p->ref = 1;
    return p;
}

uint8_t pbuf_free(struct pbuf *p) {
    if (p) {
        free(p);
        return 1;
    }
    return 0;
}

uint16_t pbuf_copy_partial(const struct pbuf *p, void *dataptr, uint16_t len, uint16_t offset) {
    if (!p || !dataptr || offset >= p->len) return 0;
    uint16_t to_copy = (p->len - offset < len) ? (p->len - offset) : len;
    memcpy(dataptr, (const char*)p->payload + offset, to_copy);
    return to_copy;
}

err_t etharp_output(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr) {
    return ERR_OK;
}

err_t ethernet_input(struct pbuf *p, struct netif *netif) {
    return ERR_OK;
}

// --- FreeRTOS task mocks ---
TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return s_current_task;
}

void mock_freertos_set_current_task(TaskHandle_t task) {
    s_current_task = task;
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       const uint32_t usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask) {
    if (pxCreatedTask) {
        *pxCreatedTask = (TaskHandle_t)0x3000;
    }
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char * const pcName,
                                   const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pxCreatedTask,
                                   const BaseType_t xCoreID) {
    if (pxCreatedTask) {
        *pxCreatedTask = (TaskHandle_t)0x3000;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t xTaskToDelete) {}

void vTaskDelay(const TickType_t xTicksToDelay) {}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask) {
    return 4096;
}

// --- FreeRTOS Queue / EventGroup ---
struct MockEventGroup {
    EventBits_t bits;
};

EventGroupHandle_t xEventGroupCreate(void) {
    MockEventGroup* eg = new MockEventGroup();
    eg->bits = 0;
    return (EventGroupHandle_t)eg;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait) {
    if (!xEventGroup) return 0;
    MockEventGroup* eg = (MockEventGroup*)xEventGroup;
    EventBits_t res = eg->bits & uxBitsToWaitFor;
    if (xClearOnExit) {
        eg->bits &= ~uxBitsToWaitFor;
    }
    return res;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet) {
    if (!xEventGroup) return 0;
    MockEventGroup* eg = (MockEventGroup*)xEventGroup;
    eg->bits |= uxBitsToSet;
    return eg->bits;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear) {
    if (!xEventGroup) return 0;
    MockEventGroup* eg = (MockEventGroup*)xEventGroup;
    eg->bits &= ~uxBitsToClear;
    return eg->bits;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup) {
    if (xEventGroup) {
        delete (MockEventGroup*)xEventGroup;
    }
}

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    return (QueueHandle_t)malloc(16);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void * const pvItemToQueue, TickType_t xTicksToWait) {
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void * const pvBuffer, TickType_t xTicksToWait) {
    return pdPASS;
}

void vQueueDelete(QueueHandle_t xQueue) {
    if (xQueue) free(xQueue);
}

// --- ESP-IDF system & timer ---
const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        default: return "ESP_UNKNOWN";
    }
}

int64_t esp_timer_get_time(void) {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    auto elapsed = duration_cast<microseconds>(steady_clock::now() - start);
    return elapsed.count();
}

uint32_t esp_random(void) {
    return (uint32_t)rand();
}

void esp_fill_random(void *buf, size_t len) {
    uint8_t *p = (uint8_t*)buf;
    for (size_t i = 0; i < len; ++i) {
        p[i] = (uint8_t)(rand() & 0xff);
    }
}

size_t heap_caps_get_free_size(uint32_t caps) {
    return 1024 * 1024;
}

size_t heap_caps_get_largest_free_block(uint32_t caps) {
    return 512 * 1024;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps) {
    return 256 * 1024;
}

void* heap_caps_malloc(size_t size, uint32_t caps) {
    return malloc(size);
}

void heap_caps_free(void *ptr) {
    if (ptr) free(ptr);
}

// --- NVS Mocks ---
static bool s_nvs_initialized = false;

esp_err_t nvs_flash_init(void) {
    s_nvs_initialized = true;
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    return ESP_OK;
}

esp_err_t nvs_flash_deinit(void) {
    s_nvs_initialized = false;
    return ESP_OK;
}

esp_err_t nvs_open(const char* name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) {
    if (out_handle) *out_handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length) {
    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    return ESP_OK;
}

// --- Wi-Fi & Event Mocks ---
const esp_event_base_t WIFI_EVENT = "WIFI_EVENT";
const esp_event_base_t IP_EVENT = "IP_EVENT";

static bool s_netif_inited = false;
static bool s_wifi_inited = false;
static bool s_wifi_started = false;
static bool s_wifi_connected = false;

esp_err_t esp_netif_init(void) {
    s_netif_inited = true;
    return ESP_OK;
}

esp_netif_t* esp_netif_create_default_wifi_sta(void) {
    static int dummy = 1;
    return (esp_netif_t*)&dummy;
}

esp_err_t esp_event_loop_create_default(void) {
    return ESP_OK;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    s_wifi_inited = true;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(uint32_t interface, wifi_config_t *conf) {
    return ESP_OK;
}

esp_err_t esp_wifi_start(void) {
    s_wifi_started = true;
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void) {
    s_wifi_connected = true;
    return ESP_OK;
}

esp_err_t esp_wifi_stop(void) {
    s_wifi_started = false;
    return ESP_OK;
}

esp_err_t esp_wifi_deinit(void) {
    s_wifi_inited = false;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void* event_handler_arg,
                                              esp_event_handler_instance_t* instance) {
    if (instance) *instance = (esp_event_handler_instance_t)0x5000;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t instance) {
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t event_base,
                         int32_t event_id,
                         void* event_data,
                         size_t event_data_size,
                         uint32_t ticks_to_wait) {
    return ESP_OK;
}

#ifdef bind
#undef bind
#endif

extern "C" int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int rc = ::bind(sockfd, addr, addrlen);
    if (rc != 0 && (errno == EADDRINUSE || errno == EACCES)) {
        struct sockaddr_in anyPort;
        if (addrlen >= sizeof(anyPort)) {
            memcpy(&anyPort, addr, sizeof(anyPort));
            anyPort.sin_port = 0;
            rc = ::bind(sockfd, (const struct sockaddr*)&anyPort, sizeof(anyPort));
        }
    }
    return rc;
}
