#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <random>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ESP32LwIPGlue.hpp"

// Task handles
static const TaskHandle_t TASK_TCPIP = (TaskHandle_t)0x2000;
static const TaskHandle_t TASK_WORKER = (TaskHandle_t)0x1000;
static std::atomic<std::thread::id> s_tcpip_thread_id;

// FreeRTOS mock
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle() {
    if (std::this_thread::get_id() == s_tcpip_thread_id.load()) {
        return TASK_TCPIP;
    }
    return TASK_WORKER;
}

// Sem synchronization map
struct SemSync {
    std::mutex mtx;
    std::condition_variable cv;
};
static std::mutex s_sem_map_mtx;
static std::map<sys_sem_record*, std::shared_ptr<SemSync>> s_sem_map;

static std::shared_ptr<SemSync> get_sem_sync(sys_sem_record* s) {
    std::lock_guard<std::mutex> lock(s_sem_map_mtx);
    auto it = s_sem_map.find(s);
    if (it != s_sem_map.end()) {
        return it->second;
    }
    auto sync = std::make_shared<SemSync>();
    s_sem_map[s] = sync;
    return sync;
}

extern "C" {

err_t sys_sem_new(sys_sem_t* sem, u8_t count) {
    if (!sem) return ERR_VAL;
    sys_sem_record* s = (sys_sem_record*)malloc(sizeof(sys_sem_record));
    s->count = count;
    s->valid = true;
    s->magic = SYS_SEM_MAGIC;
    *sem = s;
    get_sem_sync(s);
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t* sem) {
    if (!sem || !*sem) return;
    sys_sem_record* s = *sem;
    auto sync = get_sem_sync(s);
    std::lock_guard<std::mutex> lock(sync->mtx);
    if (s->magic == SYS_SEM_MAGIC && s->valid) {
        s->count++;
        sync->cv.notify_one();
    }
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout) {
    if (!sem || !*sem) return SYS_ARCH_TIMEOUT;
    sys_sem_record* s = *sem;
    auto sync = get_sem_sync(s);
    std::unique_lock<std::mutex> lock(sync->mtx);
    if (!s->valid || s->magic != SYS_SEM_MAGIC) return SYS_ARCH_TIMEOUT;

    if (timeout == 0) {
        sync->cv.wait(lock, [&]() { return s->count > 0; });
        s->count--;
        return 1;
    } else {
        auto start = std::chrono::steady_clock::now();
        bool ok = sync->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&]() { return s->count > 0; });
        if (!ok) {
            return SYS_ARCH_TIMEOUT;
        }
        s->count--;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        return (u32_t)(elapsed > 0 ? elapsed : 1);
    }
}

void sys_sem_wait(sys_sem_t* sem) {
    sys_arch_sem_wait(sem, 0);
}

void sys_sem_free(sys_sem_t* sem) {
    if (!sem || !*sem) return;
    sys_sem_record* s = *sem;
    {
        auto sync = get_sem_sync(s);
        std::lock_guard<std::mutex> lock(sync->mtx);
        s->valid = false;
        s->magic = 0xDEADBEEF;
    }
    {
        std::lock_guard<std::mutex> lock(s_sem_map_mtx);
        s_sem_map.erase(s);
    }
    free(s);
    *sem = nullptr;
}

int sys_sem_valid(sys_sem_t* sem) {
    return (sem && *sem && (*sem)->valid && (*sem)->magic == SYS_SEM_MAGIC) ? 1 : 0;
}

void sys_sem_set_invalid(sys_sem_t* sem) {
    if (sem) *sem = nullptr;
}

// Queue for tcpip_callback
struct TcpipCallbackMsg {
    tcpip_callback_fn fn;
    void* ctx;
};

static std::queue<TcpipCallbackMsg> s_tcpip_queue;
static std::mutex s_queue_mtx;
static std::condition_variable s_queue_cv;
static std::atomic<bool> s_stop_tcpip_thread{false};

err_t tcpip_callback(tcpip_callback_fn function, void* ctx) {
    std::lock_guard<std::mutex> lock(s_queue_mtx);
    s_tcpip_queue.push({function, ctx});
    s_queue_cv.notify_one();
    return ERR_OK;
}

err_t tcpip_input(struct pbuf* p, struct netif* inp) { return ERR_OK; }
struct netif* netif_add(struct netif* netif, const ip4_addr_t* ipaddr, const ip4_addr_t* netmask, const ip4_addr_t* gw, void* state, netif_init_fn init, netif_input_fn input) { return netif; }
void netif_remove(struct netif* netif) {}
void netif_set_addr(struct netif* netif, const ip4_addr_t* ipaddr, const ip4_addr_t* netmask, const ip4_addr_t* gw) {}
void netif_set_up(struct netif* netif) {}
void netif_set_down(struct netif* netif) {}
void netif_set_link_up(struct netif* netif) {}
void netif_set_link_down(struct netif* netif) {}

} // extern "C"

// Mock tcpip_thread loop introducing random 0-20ms delays
void tcpip_thread_func() {
    s_tcpip_thread_id.store(std::this_thread::get_id());
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> dist(0, 20);

    while (!s_stop_tcpip_thread) {
        TcpipCallbackMsg msg{nullptr, nullptr};
        {
            std::unique_lock<std::mutex> lock(s_queue_mtx);
            s_queue_cv.wait_for(lock, std::chrono::milliseconds(50), [&]() {
                return !s_tcpip_queue.empty() || s_stop_tcpip_thread;
            });
            if (s_stop_tcpip_thread && s_tcpip_queue.empty()) break;
            if (!s_tcpip_queue.empty()) {
                msg = s_tcpip_queue.front();
                s_tcpip_queue.pop();
            }
        }

        if (msg.fn) {
            int delay_ms = dist(rng);
            if (delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            // Execute callback in tcpip thread context
            msg.fn(msg.ctx);
        }
    }
}

int main(int argc, char** argv) {
    std::printf("[TEST RUN] Starting Multithreaded ASan/UBSan Stress Test for ESP32LwIPGlue\n");

    s_stop_tcpip_thread = false;
    std::thread tcpip_thread(tcpip_thread_func);

    // Wait until tcpip thread sets its thread id
    while (s_tcpip_thread_id.load() == std::thread::id()) {
        std::this_thread::yield();
    }

    // Stress test 1: Single worker repeatedly calling captureTcpipThread(5) with 0-20ms random delays
    std::printf("[INFO] Part 1: Repeated calls with 5ms timeout against 0-20ms delays...\n");
    int total_timeouts = 0;
    int total_successes = 0;

    for (int attempt = 0; attempt < 20; ++attempt) {
        bool captured = ZeroTier::ESP32LwIPGlue::captureTcpipThread(5);
        if (captured) {
            total_successes++;
        } else {
            total_timeouts++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("[INFO] Part 1 finished: %d successes, %d timeouts\n", total_successes, total_timeouts);
    assert(total_successes > 0 && "Should succeed after callback finishes");

    // Stress test 2: 4 concurrent worker threads calling captureTcpipThread(5) simultaneously
    std::printf("[INFO] Part 2: Concurrent worker threads calling captureTcpipThread(5)...\n");
    std::vector<std::thread> workers;
    std::atomic<int> concurrent_success_count{0};

    for (int w = 0; w < 4; ++w) {
        workers.emplace_back([&concurrent_success_count]() {
            for (int i = 0; i < 20; ++i) {
                if (ZeroTier::ESP32LwIPGlue::captureTcpipThread(5)) {
                    concurrent_success_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }
    std::printf("[INFO] Part 2 finished: concurrent_success_count = %d / 80\n", concurrent_success_count.load());
    assert(concurrent_success_count.load() == 80 && "All concurrent calls must return true once captured");

    // Clean up
    s_stop_tcpip_thread = true;
    s_queue_cv.notify_all();
    tcpip_thread.join();

    std::printf("[PASS] All multithreaded ASan/UBSan stress tests passed with ZERO memory errors and ZERO crashes!\n");
    return 0;
}
