# ESP32 Lite ZeroTier port source list and lwIP hook integration helper.
# Include from the ESP-IDF component CMakeLists.txt that builds ZeroTierOne.
#
# Usage example:
#   include(${CMAKE_CURRENT_LIST_DIR}/../../osdep/ESP32LitePort.cmake)
#   idf_component_register(SRCS ${ZT_ESP32_LITE_PORT_SRCS} ...)
#   zt_esp32_lite_configure_component(${COMPONENT_LIB})

set(ZT_ESP32_LITE_PORT_SRCS
    osdep/EthernetTap.cpp
    osdep/ManagedRoute.cpp
    osdep/ESP32EthernetTap.cpp
    osdep/ESP32EgressTrace.cpp
    osdep/ESP32NetLink.cpp
    osdep/ESP32NetworkState.cpp
    osdep/ESP32FramePool.cpp
    osdep/ESP32LwIPGlue.cpp
    osdep/ESP32LiteDiagnostics.cpp
    osdep/ESP32LiteAppGlue.cpp
    osdep/ESP32NodeTask.cpp
    osdep/ESP32PlatformCallbacks.cpp
    osdep/ESP32LitePort.cpp
)

set(ZT_ESP32_LITE_PORT_INCLUDE_DIRS
    .
    include
    node
    osdep
)

function(zt_esp32_lite_configure_component target_name)
    set(_zt_esp32_defs
        ZT_ESP32_LITE=1
        ZT_ESP32_ALLOW_PROVISIONED_IDENTITY=1
        ZT_NETWORKCONFIG_DICT_CAPACITY_OVERRIDE=4096
        ZT_MAX_NETWORK_ROUTES=8
        ZT_MAX_ZT_ASSIGNED_ADDRESSES=4
        ZT_MAX_NETWORK_SPECIALISTS=8
        ZT_MAX_NETWORK_RULES=32
        ZT_MAX_NETWORK_CAPABILITIES=1
        ZT_MAX_NETWORK_TAGS=1
        ZT_MAX_CERTIFICATES_OF_OWNERSHIP=1
        ZT_MAX_CAPABILITY_RULES=1
        ZT_MAX_NETWORK_MULTICAST_SUBSCRIPTIONS=64
        ZT_MAX_MULTICAST_SUBSCRIPTIONS=16
        ZT_MAX_DNS_SERVERS=1
        ZT_MAX_CONFIGURABLE_PATHS=4
        ZT_RX_QUEUE_SIZE=4
        ZT_TX_QUEUE_SIZE=4
        ZT_DEFAULT_PHYSMTU=1280
        ZT_MAX_PACKET_FRAGMENTS=2
        ZT_MAX_PEER_NETWORK_PATHS=4
        ZT_LAST_IDENTITY_VERIFICATION_SIZE=256
    )
    target_compile_definitions(${target_name} PRIVATE ${_zt_esp32_defs})

    # The lwIP hook filename must be visible while the ESP-IDF lwip component is
    # compiled, not only while this ZeroTier component is compiled. Also expose
    # the include path containing ESP32LwIPHooks.h to lwIP.
    if(COMMAND idf_component_get_property)
        idf_component_get_property(_zt_lwip_lib lwip COMPONENT_LIB)
        if(_zt_lwip_lib)
            target_compile_definitions(${_zt_lwip_lib} PUBLIC
                ESP_IDF_LWIP_HOOK_FILENAME=\"osdep/ESP32LwIPHooks.h\")
            target_include_directories(${_zt_lwip_lib} PUBLIC
                /project/ZeroTierOne
                /project/ZeroTierOne/osdep)
        endif()
    endif()
endfunction()

# Backward-compatible alias used by earlier drafts.
function(zt_esp32_lite_configure_lwip_hooks target_name)
    zt_esp32_lite_configure_component(${target_name})
endfunction()
