cmake_minimum_required(VERSION 3.16)

file(GLOB ZT_ALL_SRCS /project/ZeroTierOne/node/*.cpp)
list(FILTER ZT_ALL_SRCS EXCLUDE REGEX ".*(AES_aesni|AES_armcrypto|Bond|Metrics|Trace)\.cpp$")
list(APPEND ZT_ALL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_mutex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_utils_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_metrics_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_bond_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_trace_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_storage_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_socket_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_tap_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_osutils_esp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/port/zt_route_debug_esp.cpp
)

# ESP32-Lite ZeroTier port draft sources.
# The helper file's source list is relative to /project/ZeroTierOne, so prefix it
# before passing it to idf_component_register from this component directory.
include(/project/ZeroTierOne/osdep/ESP32LitePort.cmake)
set(ZT_ESP32_LITE_PORT_SRCS_ABS)
foreach(_zt_lite_src ${ZT_ESP32_LITE_PORT_SRCS})
    list(APPEND ZT_ESP32_LITE_PORT_SRCS_ABS /project/ZeroTierOne/${_zt_lite_src})
endforeach()
list(APPEND ZT_ALL_SRCS ${ZT_ESP32_LITE_PORT_SRCS_ABS})


idf_component_register(
    SRCS ${ZT_ALL_SRCS}
    INCLUDE_DIRS
        /project/ZeroTierOne/node
        /project/ZeroTierOne/osdep
        /project/ZeroTierOne/service
        port
        port/stubs
        /project/ZeroTierOne/include
        include
    REQUIRES
        freertos
        lwip
        nvs_flash
        esp_timer
    PRIV_REQUIRES
        esp_system
)

zt_esp32_lite_configure_component(${COMPONENT_LIB})

# lwIP's ETHARP hook references this C symbol from libzerotier_node.a.
# Force archive extraction during final app link.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--undefined=zt_esp32_lwip_etharp_get_gw"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Og
    -g3
    -fno-inline
    -fno-optimize-sibling-calls
    -fno-omit-frame-pointer
    -fno-rtti
    -fexceptions
    -DZT_ESP32_ALLOW_PROVISIONED_IDENTITY=1
    -DZT_MAX_NETWORK_RULES=32
    -DZT_MAX_NETWORK_CAPABILITIES=1
    -DZT_MAX_NETWORK_TAGS=1
    -DZT_MAX_CERTIFICATES_OF_OWNERSHIP=1
    -DZT_MAX_CAPABILITY_RULES=1
    -DZT_RX_QUEUE_SIZE=4
    -DZT_TX_QUEUE_SIZE=4
    -DZT_DEFAULT_PHYSMTU=1280
    -DZT_MAX_PACKET_FRAGMENTS=2
    -DZT_MAX_PEER_NETWORK_PATHS=4
    -DZT_LAST_IDENTITY_VERIFICATION_SIZE=256
    -DZT_IDENTITY_GEN_MEMORY_OVERRIDE=65536
    -DZT_NETWORKCONFIG_DICT_CAPACITY_OVERRIDE=4096
    -DZT_ESP32_STEP52_NETCONF_APPLY_TRACE=0
    -DZT_MAX_ZT_ASSIGNED_ADDRESSES=4
    -Wno-unused-variable
    -Wno-unused-function
    -DZT_NO_BOND
    -DZT_NO_METRICS
    -DZT_NO_PEER_METRICS
    -DZT_NO_TYPE_PUNNING
    -DZT_NO_UNALIGNED_ACCESS
    -include thread
    -DZT_NO_TRACE
    -Wno-error=return-type
    -Wno-error=maybe-uninitialized
    -Wno-error=stringop-overflow
    -Wno-error=array-bounds
    -Wno-error=restrict
    -Wno-volatile
    -Wno-deprecated
)

# Keep GDB-callable debug symbols in final ELF even if not directly referenced in C.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--undefined=zt_debug_dumpManagedRouteDecision_ipv4"
    "-Wl,--undefined=zt_debug_enableEgressTrace_ipv4"
    "-Wl,--undefined=zt_debug_trigger_arp_request_ipv4"
    "-Wl,--undefined=zt_debug_trigger_icmp_echo_ipv4"
    "-Wl,--undefined=zt_debug_schedule_icmp_ping_ipv4"
    "-Wl,--undefined=zt_debug_schedule_udp_payload_ipv4"
    "-Wl,--undefined=zt_debug_schedule_underlay_icmp_ping_ipv4"
    "-Wl,--undefined=zt_debug_schedule_overlay_icmp_ping_ipv4"
)
