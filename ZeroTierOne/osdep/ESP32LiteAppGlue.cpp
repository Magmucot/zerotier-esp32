#include "ESP32LiteAppGlue.hpp"
#include "ESP32LiteDiagnostics.hpp"

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#endif

namespace ZeroTier {
static ESP32LitePort _defaultPort;
}

extern "C" ZeroTier::ESP32LitePort* zt_esp32_lite_default_port(void)
{
	return &ZeroTier::_defaultPort;
}

extern "C" int zt_esp32_lite_nvs_init(void)
{
#ifdef ESP_PLATFORM
	esp_err_t err = nvs_flash_init();
	if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		/* This helper is intentionally conservative: only erase if NVS is unusable.
		 * Production apps may want to handle this outside the ZeroTier component. */
		err = nvs_flash_erase();
		if (err != ESP_OK) {
			return -1;
		}
		err = nvs_flash_init();
	}
	return (err == ESP_OK) ? 0 : -1;
#else
	return -1;
#endif
}

extern "C" int zt_esp32_lite_start_default(uint64_t nwid)
{
	ZeroTier::ESP32LitePort::Config cfg;
	ZeroTier::ESP32LitePort::defaultConfig(cfg, nwid);
	return ZeroTier::_defaultPort.start(cfg);
}

extern "C" int zt_esp32_lite_start_with_namespace(uint64_t nwid, const char* nvs_namespace)
{
	ZeroTier::ESP32LitePort::Config cfg;
	ZeroTier::ESP32LitePort::defaultConfig(cfg, nwid);
	cfg.nvsNamespace = nvs_namespace ? nvs_namespace : "zerotier";
	return ZeroTier::_defaultPort.start(cfg);
}

extern "C" int zt_esp32_lite_join(uint64_t nwid)
{
	return ZeroTier::_defaultPort.join(nwid);
}

extern "C" void zt_esp32_lite_stop(void)
{
	ZeroTier::_defaultPort.stop();
}

extern "C" int zt_esp32_lite_identity_ready(void)
{
	return ZeroTier::_defaultPort.verifyProvisionedIdentity() ? 1 : 0;
}

extern "C" uint64_t zt_esp32_lite_node_id(void)
{
	return ZeroTier::_defaultPort.node() ? ZT_Node_address(ZeroTier::_defaultPort.node()) : 0;
}

extern "C" void zt_esp32_lite_dump_diagnostics(void)
{
	ZeroTier::ESP32LiteDiagnostics::dumpAll(&ZeroTier::_defaultPort);
}
