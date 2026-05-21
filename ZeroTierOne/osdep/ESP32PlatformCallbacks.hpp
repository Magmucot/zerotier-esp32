#ifndef ZT_ESP32PLATFORMCALLBACKS_HPP
#define ZT_ESP32PLATFORMCALLBACKS_HPP

#include "../include/ZeroTierOne.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional ready-to-use C API config callback. It is intentionally tiny:
 * copy/enqueue config to ESP32NodeTask and return. Do not perform lwIP or
 * multicast operations here.
 */
int zt_esp32_virtual_network_config_callback(
	ZT_Node* node,
	void* userPtr,
	void* threadPtr,
	uint64_t nwid,
	void** networkUserPtr,
	enum ZT_VirtualNetworkConfigOperation operation,
	const ZT_VirtualNetworkConfig* config);

#ifdef __cplusplus
}
#endif

#endif
