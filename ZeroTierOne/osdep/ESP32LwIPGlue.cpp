#include "ESP32LwIPGlue.hpp"

#ifdef ESP_PLATFORM
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

namespace ZeroTier {

struct ZtLwipCall {
	enum Op { OP_NOOP, OP_ADD, OP_REMOVE, OP_SET_IPV4, OP_SET_UP, OP_SET_LINK } op;
	sys_sem_t sem;
	err_t rc;
	struct netif* n;
	void* state;
	netif_init_fn initFn;
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gw;
	bool flag;
	bool signalSem;
};

static volatile TaskHandle_t _ztTcpipThreadTask = (TaskHandle_t)0;

static void _zt_lwip_execute(ZtLwipCall* c)
{
	c->rc = ERR_OK;
	switch (c->op) {
		case ZtLwipCall::OP_NOOP:
			break;
		case ZtLwipCall::OP_ADD: {
			ip4_addr_t zero;
			IP4_ADDR(&zero, 0, 0, 0, 0);
			if (netif_add(c->n, &zero, &zero, &zero, c->state, c->initFn, tcpip_input) == (struct netif*)0) {
				c->rc = ERR_IF;
			}
			break;
		}
		case ZtLwipCall::OP_REMOVE:
			netif_remove(c->n);
			break;
		case ZtLwipCall::OP_SET_IPV4:
			netif_set_addr(c->n, &c->ip, &c->mask, &c->gw);
			break;
		case ZtLwipCall::OP_SET_UP:
			if (c->flag) {
				netif_set_up(c->n);
			}
			else {
				netif_set_down(c->n);
			}
			break;
		case ZtLwipCall::OP_SET_LINK:
			if (c->flag) {
				netif_set_link_up(c->n);
			}
			else {
				netif_set_link_down(c->n);
			}
			break;
	}
}

static void _zt_lwip_call_cb(void* arg)
{
	ZtLwipCall* c = (ZtLwipCall*)arg;
	if (! _ztTcpipThreadTask) {
		_ztTcpipThreadTask = xTaskGetCurrentTaskHandle();
	}
	_zt_lwip_execute(c);
	if (c->signalSem) {
		sys_sem_signal(&c->sem);
	}
}

static bool _zt_lwip_current_thread_is_tcpip()
{
	TaskHandle_t tcpipTask = (TaskHandle_t)_ztTcpipThreadTask;
	return (tcpipTask && (xTaskGetCurrentTaskHandle() == tcpipTask));
}

static bool _run_sync(ZtLwipCall& c)
{
	/* If this wrapper is called from lwIP's tcpip_thread, posting a callback and
	 * waiting would deadlock. Once captureTcpipThread() has learned the task
	 * handle, run the operation inline in that case. */
	if (_zt_lwip_current_thread_is_tcpip()) {
		c.signalSem = false;
		_zt_lwip_execute(&c);
		return (c.rc == ERR_OK);
	}
	if (sys_sem_new(&c.sem, 0) != ERR_OK) {
		return false;
	}
	c.rc = ERR_INPROGRESS;
	c.signalSem = true;
	if (tcpip_callback(&_zt_lwip_call_cb, &c) != ERR_OK) {
		sys_sem_free(&c.sem);
		return false;
	}
	sys_sem_wait(&c.sem);
	sys_sem_free(&c.sem);
	return (c.rc == ERR_OK);
}

bool ESP32LwIPGlue::captureTcpipThread(uint32_t timeoutMs)
{
	if (_ztTcpipThreadTask) {
		return true;
	}
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_NOOP;
	if (sys_sem_new(&c.sem, 0) != ERR_OK) {
		return false;
	}
	c.signalSem = true;
	c.rc = ERR_INPROGRESS;
	if (tcpip_callback(&_zt_lwip_call_cb, &c) != ERR_OK) {
		sys_sem_free(&c.sem);
		return false;
	}
	const u32_t waited = sys_arch_sem_wait(&c.sem, timeoutMs ? timeoutMs : 1000);
	sys_sem_free(&c.sem);
	return ((waited != SYS_ARCH_TIMEOUT) && (_ztTcpipThreadTask != (TaskHandle_t)0) && (c.rc == ERR_OK));
}

bool ESP32LwIPGlue::addNetif(struct netif* n, void* state, netif_init_fn initFn)
{
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_ADD;
	c.n = n;
	c.state = state;
	c.initFn = initFn;
	return _run_sync(c);
}

void ESP32LwIPGlue::removeNetif(struct netif* n)
{
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_REMOVE;
	c.n = n;
	_run_sync(c);
}

void ESP32LwIPGlue::setIpv4(struct netif* n, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gw)
{
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_SET_IPV4;
	c.n = n;
	if (ip) { c.ip = *ip; }
	if (mask) { c.mask = *mask; }
	if (gw) { c.gw = *gw; }
	_run_sync(c);
}

void ESP32LwIPGlue::setUp(struct netif* n, bool up)
{
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_SET_UP;
	c.n = n;
	c.flag = up;
	_run_sync(c);
}

void ESP32LwIPGlue::setLinkUp(struct netif* n, bool up)
{
	ZtLwipCall c;
	memset(&c, 0, sizeof(c));
	c.op = ZtLwipCall::OP_SET_LINK;
	c.n = n;
	c.flag = up;
	_run_sync(c);
}

err_t ESP32LwIPGlue::input(struct netif* n, struct pbuf* p)
{
	return tcpip_input(p, n);
}

} // namespace ZeroTier
#endif
