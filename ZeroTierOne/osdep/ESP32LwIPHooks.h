/* Include this file through ESP-IDF's ESP_IDF_LWIP_HOOK_FILENAME mechanism.
 *
 * Example component CMake usage:
 *   idf_component_get_property(lwip lwip COMPONENT_LIB)
 *   target_compile_definitions(${lwip} PUBLIC
 *     ESP_IDF_LWIP_HOOK_FILENAME=\"ESP32LwIPHooks.h\")
 *   target_include_directories(${lwip} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/path/to/ZeroTierOne/osdep)
 */
#ifndef ZT_ESP32_LWIP_HOOKS_H
#define ZT_ESP32_LWIP_HOOKS_H


#ifdef __cplusplus
extern "C" {
#endif

struct netif;
struct ip4_addr;

struct netif* zt_esp32_lwip_ip4_route_src(const struct ip4_addr* src, const struct ip4_addr* dest);
struct netif* zt_esp32_lwip_ip4_route(const struct ip4_addr* dest);
const struct ip4_addr* zt_esp32_lwip_etharp_get_gw(struct netif* netifPtr, const struct ip4_addr* dest);

#ifdef __cplusplus
}
#endif

#ifdef LWIP_HOOK_IP4_ROUTE_SRC
#undef LWIP_HOOK_IP4_ROUTE_SRC
#endif
#define LWIP_HOOK_IP4_ROUTE_SRC(src, dest) zt_esp32_lwip_ip4_route_src((src), (dest))

#ifdef LWIP_HOOK_IP4_ROUTE
#undef LWIP_HOOK_IP4_ROUTE
#endif
#define LWIP_HOOK_IP4_ROUTE(dest) zt_esp32_lwip_ip4_route((dest))

#ifdef LWIP_HOOK_ETHARP_GET_GW
#undef LWIP_HOOK_ETHARP_GET_GW
#endif
#define LWIP_HOOK_ETHARP_GET_GW(netif, dest) zt_esp32_lwip_etharp_get_gw((netif), (dest))

#endif
