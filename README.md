# ZeroTier client for ESP32 / ESPHome (Home Assistant)

An experimental, open-source project aiming to build a native [ZeroTier](https://zerotier.com "ZeroTier about page") SD-WAN client called [ZeroTierOne](https://github.com/zerotier/zerotierone "Github public source code for ZeroTierOne") for ESP32-class devices. It utilizes ESP-IDF, lwIP, and QEMU-based testing, providing a future integration path for ESPHome and Home Assistant.
 
## Goal

The goal is to make an ESP32 device a native ZeroTier network member without requiring an external gateway, router, Raspberry Pi, or Home Assistant add-on to bridge traffic.

This is intended for use cases such as:

- remote cabins, boats, RVs, and agricultural installations,
- ESPHome devices deployed behind NAT or mobile broadband,
- sensor and control devices without public IP addresses,
- secure remote maintenance of embedded devices.

## Project status

This project is experimental and under active development.

The current architecture is called **ESP32-ZeroTier-Lite**. It is a constrained ZeroTier client design for ESP32/ESP-IDF and lwIP.

It does not try to reproduce the full Linux ZeroTierOne runtime. Instead, it focuses on the minimum runtime semantics needed for a useful embedded ZeroTier member:

- externally provisioned ZeroTier identity,
- ZeroTier node startup and network join,
- controller-provided NetworkConfig handling,
- managed IPv4 address handling,
- managed route handling for direct managed subnets,
- multicast subscription needed for ZeroTier address resolution,
- bounded dataplane frame handling,
- real overlay payload traffic through lwIP.

For the detailed design, see:

[ESP32_ZEROTIER_LITE_ARCHITECTURE.md](ESP32_ZEROTIER_LITE_ARCHITECTURE.md)

## Verified status

The ESP32-QEMU target has verified real ZeroTier dataplane operation under the intended routed no-NAT test topology.

Verified in QEMU:

- ZeroTier node startup,
- network join,
- ONLINE state,
- NetworkConfig reception,
- managed IPv4 address installation,
- managed route installation for the direct managed subnet case,
- own-IP multicast subscription for ZeroTier address resolution,
- direct peer path under the routed test topology,
- ZT ARP,
- ICMP Ping,
- UDP payload,
- TCP payload.

This means the project has passed more than a control-plane join test. Real overlay payload traffic has been validated in QEMU.

The project is still not production-ready. Remaining work includes:

- longer stability and reconnect testing,
- Wi-Fi interruption and recovery behavior,
- heap and stack behavior over long runtimes,
- hardware validation on real ESP32 targets,
- ESPHome integration,
- IPv6 support,
- gateway-route evaluation,
- production hardening and cleanup.

## Current focus

The current focus is to keep the ESP32-ZeroTier-Lite runtime small, bounded, and testable while preserving the ZeroTier semantics required for real dataplane operation.

Current engineering priorities include:

- keeping runtime state bounded for ESP32 memory limits,
- avoiding unbounded heap use in dataplane paths,
- maintaining robust NVS identity handling,
- validating managed IP and route lifecycle behavior,
- validating multicast subscription lifecycle behavior,
- repeating QEMU runtime regression tests after major build or integration changes,
- collecting evidence from both ESP32-QEMU and peer-side ZeroTier nodes.

## Non-goals for the first ESP32-ZeroTier-Lite target

The first target is intentionally limited.

Not currently primary goals:

- full Linux ZeroTierOne feature parity,
- default-route support,
- IPv6 support,
- arbitrary managed route complexity,
- persisting NetworkConfig as boot baseline state,
- large peer/path tables,
- large packet queues,
- on-device default identity generation as the normal path.

The device should rebuild ZeroTier runtime membership state from live controller configuration after boot.

## Repository layout

In the development container, the main repository is mounted at `/project`.

Important project areas:

```text
/project
├── components/zerotier_node/              ESP-IDF component integration
├── ZeroTierOne/                           ZeroTierOne source tree (v1.16)
├── ZeroTierOne/osdep/ESP32*.*             ESP32 implementation files
├── sdkconfig                              ESP-IDF project configuration
├── README.md                              Project overview
└── ESP32_ZEROTIER_LITE_ARCHITECTURE.md    Detailed ESP32-ZeroTier-Lite architecture
```

During development, the QEMU test project is kept outside this repository at:

```text
/qemu-project
```

The QEMU project is used for runtime testing of the ESP32/QEMU target and is tightly coupled to this repository during development.

## Testing expectations

Dataplane correctness requires more than ZeroTier node startup.

A successful runtime test should verify:

- node creation,
- network join,
- ONLINE state,
- NetworkConfig reception,
- managed IP installation,
- managed route installation,
- own-IP multicast subscription,
- ZT ARP,
- ICMP Ping,
- UDP payload,
- TCP payload.

ZT ARP alone is not considered sufficient proof of dataplane correctness. Real IP payload traffic must also be verified.

## Test topology notes

The intended QEMU test topology is routed and no-NAT.

Important constraints:

- QEMU underlay must use `10.11.0.0/24`.
- The Linux ZeroTier peer underlay must use `10.20.0.0/24`.
- MASQUERADE, SNAT, and DNAT must not be used for the test networks.
- The old `10.10.0.0/24` transit topology is obsolete and must not be recreated.

## Development status

This repository is not yet a finished ESPHome component or production-ready ESP32 library.

It is currently a research and development project focused on proving and hardening the ESP32-ZeroTier-Lite runtime through repeatable ESP-IDF/QEMU tests.

## License

This project includes and modifies ZeroTierOne source code. ZeroTierOne is licensed under the Mozilla Public License 2.0. See the upstream ZeroTierOne license files for details.
