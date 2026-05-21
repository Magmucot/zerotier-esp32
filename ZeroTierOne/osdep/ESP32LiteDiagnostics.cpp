#include "ESP32LiteDiagnostics.hpp"
#include "ESP32NetLink.hpp"
#include "ESP32FramePool.hpp"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace ZeroTier {

void ESP32LiteDiagnostics::dumpHeapAndStack(const char* phase)
{
#ifdef ESP_PLATFORM
	const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
	const size_t min8 = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
	const UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
	fprintf(stderr, "[ZT_ESP32_DIAG] phase=%s heapFree=%u largest=%u min=%u stackHwm=%u\n", phase ? phase : "unknown", (unsigned)free8, (unsigned)largest, (unsigned)min8, (unsigned)hwm);
#else
	fprintf(stderr, "[ZT_ESP32_DIAG] phase=%s non_esp_build\n", phase ? phase : "unknown");
#endif
}

void ESP32LiteDiagnostics::dumpRoutes()
{
	ESP32NetLink::RouteEntry routes[ZT_ESP32_MAX_MANAGED_ROUTES];
	const unsigned int n = ESP32NetLink::getInstance().copyRoutes(routes, ZT_ESP32_MAX_MANAGED_ROUTES);
	fprintf(stderr, "[ZT_ESP32_ROUTE_TABLE] count=%u generation=%u\n", n, ESP32NetLink::getInstance().generation());
	for (unsigned int i = 0; i < n; ++i) {
		char t[64], v[64], s[64];
		fprintf(stderr, "[ZT_ESP32_ROUTE] idx=%u active=%d supported=%d target=%s via=%s src=%s device=%s reason=%s\n",
			i, routes[i].active ? 1 : 0, routes[i].supported ? 1 : 0,
			routes[i].target.toString(t), routes[i].via ? routes[i].via.toString(v) : "none",
			routes[i].src ? routes[i].src.toString(s) : "none", routes[i].device, routes[i].unsupportedReason);
	}
}

void ESP32LiteDiagnostics::dumpFramePool()
{
	fprintf(stderr, "[ZT_ESP32_POOL] free=%u capacity=%u\n", ESP32FramePool::instance().freeCount(), ESP32FramePool::instance().capacity());
}

void ESP32LiteDiagnostics::dumpAll(ESP32LitePort* port)
{
	(void)port;
	dumpHeapAndStack("dumpAll");
	dumpFramePool();
	dumpRoutes();
}

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" void zt_debug_dumpManagedRouteDecision_ipv4(uint64_t nwid, uint32_t ip_nbo, uint32_t marker_id)
{
	(void)nwid;
	ZeroTier::ESP32NetLink::LookupResult lr = ZeroTier::ESP32NetLink::getInstance().lookupIpv4(ip_nbo);
	fprintf(stderr, "[ZT_ROUTE_DECISION_BEGIN marker_id=%u marker=ESP32_MANUAL nwid=%llx ip_nbo=%08x]\n", (unsigned int)marker_id, (unsigned long long)nwid, (unsigned int)ip_nbo);
	fprintf(stderr, "[route_match]\n");
	fprintf(stderr, "route_decision=%s\n", lr.matched ? "matched" : "not_matched");
	fprintf(stderr, "route_reason=%s\n", lr.matched ? (lr.supported ? "matched_supported" : "matched_unsupported") : "no_lpm_match");
	fprintf(stderr, "[target_paths]\n");
	fprintf(stderr, "target_path_count=unknown\n");
	fprintf(stderr, "target_preferred_path=unknown\n");
	fprintf(stderr, "[send_decision]\n");
	fprintf(stderr, "matched_managed_route=%s\n", lr.matched ? "yes" : "none");
	fprintf(stderr, "network_ok=unknown\n");
	fprintf(stderr, "target_member_known=unknown\n");
	fprintf(stderr, "target_peer_exists=unknown\n");
	fprintf(stderr, "target_path_exists=unknown\n");
	fprintf(stderr, "decision_state=%s\n", lr.matched && lr.supported ? "route_available" : "no_managed_route");
	fprintf(stderr, "drop_stage=%s\n", lr.matched && lr.supported ? "none" : "route_match");
	fprintf(stderr, "drop_reason=%s\n", lr.matched ? (lr.supported ? "none" : lr.route.unsupportedReason) : "no_lpm_match");
	if (lr.matched) {
		char t[64], v[64], s[64];
		fprintf(stderr, "[esp32_route]\n");
		fprintf(stderr, "target=%s\n", lr.route.target.toString(t));
		fprintf(stderr, "via=%s\n", lr.route.via ? lr.route.via.toString(v) : "none");
		fprintf(stderr, "src=%s\n", lr.route.src ? lr.route.src.toString(s) : "none");
		fprintf(stderr, "prefix_bits=%u\n", (unsigned)lr.route.prefixBits);
		fprintf(stderr, "supported=%s\n", lr.route.supported ? "yes" : "no");
	}
	fprintf(stderr, "[ZT_ROUTE_DECISION_END marker_id=%u marker=ESP32_MANUAL]\n", (unsigned int)marker_id);
}

extern "C" void zt_debug_dumpESP32LiteState(uint32_t marker_id)
{
	fprintf(stderr, "[ZT_ESP32_STATE_BEGIN marker_id=%u]\n", (unsigned int)marker_id);
	ZeroTier::ESP32LiteDiagnostics::dumpHeapAndStack("gdb_manual");
	ZeroTier::ESP32LiteDiagnostics::dumpFramePool();
	ZeroTier::ESP32LiteDiagnostics::dumpRoutes();
	fprintf(stderr, "[ZT_ESP32_STATE_END marker_id=%u]\n", (unsigned int)marker_id);
}
#endif
