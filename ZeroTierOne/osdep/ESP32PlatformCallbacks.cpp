#include "ESP32PlatformCallbacks.hpp"
#include "ESP32NodeTask.hpp"

#include <stdio.h>

extern "C" int zt_esp32_virtual_network_config_callback(
	ZT_Node* node,
	void* userPtr,
	void* threadPtr,
	uint64_t nwid,
	void** networkUserPtr,
	enum ZT_VirtualNetworkConfigOperation operation,
	const ZT_VirtualNetworkConfig* config)
{
	(void)node;
	(void)threadPtr;
	(void)networkUserPtr;
	ZeroTier::ESP32NodeTask* task = (ZeroTier::ESP32NodeTask*)userPtr;
	if (! task) {
		task = ZeroTier::ESP32NodeTask::defaultTask();
	}
	if (! task) {
		fprintf(stderr, "[ZT_ESP32_CALLBACK] config drop reason=no_owner_task nwid=%llx op=%d\n", (unsigned long long)nwid, (int)operation);
		return -1;
	}
	if (! task->captureConfigUpdate(nwid, operation, config, 0)) {
		fprintf(stderr, "[ZT_ESP32_CALLBACK] config drop reason=queue_full nwid=%llx op=%d\n", (unsigned long long)nwid, (int)operation);
		return -1;
	}
	return 0;
}
