# New-ESP32-Lite Architecture and Runtime Semantics

## 0. Document Status and How to Use This Document

New-ESP32-Lite is the working name for the ESP32-focused ZeroTier client architecture used by this project.

This document describes the intended architecture, runtime semantics, source-code organization, and current verified build-integration state for New-ESP32-Lite. It is meant to be used as an architecture document, onboarding document, and handover document for future ChatGPT, Codex, or Claude sessions.

Important distinction:

- The current build integration has been verified.
- The latest runtime behavior must still be re-verified after the current build-integration changes.
- Runtime phases described in this document should be treated as intended architecture unless explicitly marked as currently verified.
- Do not assume a runtime phase works only because it is described here.
- Use the evidence requirements in this document to verify each phase.
- This document does not replace source-code inspection. Function-level call mapping must be verified from source before making implementation changes.

Current document status:

| Area                              | Status                                      |
|-----------------------------------|---------------------------------------------|
| Architecture intent               | Drafted                                     |
| Build integration                 | Verified                                    |
| Runtime after latest build fix    | Requires re-verification                    |
| Function-level call map           | Partial; should be completed from source    |
| Source file responsibility map    | Drafted                                     |
| QEMU runtime regression           | Next recommended action                     |

This document should be updated as runtime tests produce new evidence.

## 1. Introduction

New-ESP32-Lite is the working name for the ESP32-focused ZeroTier client architecture used by this project. Its purpose is to provide a minimal, bounded, and semantically correct ZeroTier client for ESP32-class devices running ESP-IDF and lwIP.

The goal is not to reproduce the full Linux ZeroTierOne runtime. The goal is to preserve the ZeroTier semantics required for network membership, managed IP handling, managed routes, multicast-based address resolution, and real overlay dataplane traffic within the constraints of ESP32.

This document describes:

- the goals and purpose of New-ESP32-Lite,
- the design scope and non-goals,
- terminology used in this project,
- the ZeroTier semantics that must be preserved,
- the runtime call flow from boot to dataplane-ready state,
- the source files responsible for each runtime phase,
- the threading and ownership model,
- the network state model,
- the lwIP integration model,
- the dataplane frame flow,
- diagnostics and debugging expectations,
- build and component integration,
- invariants and failure modes,
- runtime test strategy,
- current verified state,
- known limitations and future work,
- added and modified files.

A central purpose of this document is to make the relationship between runtime behavior and source code explicit:

```text
ZeroTier event / runtime phase
-> required ZeroTier semantic
-> runtime call flow
-> implementation file/function
-> state updated
-> evidence required
```

This makes the document useful both as an architecture overview and as a practical debugging/navigation guide.

## 2. Goals and Purpose

The purpose of New-ESP32-Lite is to run a constrained ZeroTier client on ESP32 while preserving the runtime semantics required by the ZeroTier core.

New-ESP32-Lite must be able to:

- start a ZeroTier node using externally provisioned identity material,
- join a ZeroTier network,
- go ONLINE,
- receive and apply NetworkConfig,
- install a managed ZeroTier IP address into local runtime state,
- install relevant managed routes into lwIP-facing route state,
- subscribe to multicast groups required for ZeroTier address resolution,
- exchange virtual network frames with the ZeroTier core,
- send and receive real overlay dataplane traffic,
- remain bounded in memory, queue depth, route count, multicast subscriptions, and frame buffers.

The architecture must support the following dataplane evidence:

- ZT ARP,
- ICMP Ping,
- UDP payload,
- TCP payload.

ZT ARP alone is not considered sufficient proof of dataplane correctness. Real IP payload traffic must also be verified.

## 3. Design Scope and Non-Goals

New-ESP32-Lite is an embedded ZeroTier client architecture. It is not intended to be a full Linux-equivalent ZeroTierOne port.

The design intentionally avoids assuming that ESP32 can behave like a general-purpose Linux host with:

- a full TAP device,
- large memory budget,
- dynamic routing stack,
- persistent disk-backed runtime state,
- unbounded peer/path tables,
- unbounded queue depth,
- large packet buffers.

Initial non-goals:

- full Linux TAP behavior,
- full ZeroTier desktop/server feature parity,
- default-route support as a primary goal,
- IPv6 support in the first runtime target,
- arbitrary managed route complexity,
- large peer, route, multicast, or frame tables,
- persisting NetworkConfig, managed IPs, managed routes, controller runtime config, peer tables, multicast subscriptions, route tables, or dataplane/runtime state to NVS as boot baseline state,
- complex graceful shutdown handling as a primary correctness mechanism.

The device must tolerate abrupt power loss and rebuild runtime state after boot from provisioned identity and live ZeroTier network configuration.

## 4. Terminology

The following terms are used consistently in this project:

| Term             | Meaning                                                                 |
|------------------|-------------------------------------------------------------------------|
| New-ESP32-Lite   | Working name for the ESP32-focused minimal ZeroTier client architecture |
| ZeroTier core    | Upstream ZeroTierOne node, network, peer, and packet logic              |
| ESP32-Lite port  | ESP32-specific osdep/platform layer for New-ESP32-Lite                  |
| NetworkConfig    | Controller-provided ZeroTier network configuration                      |
| managed IP       | ZeroTier-assigned IP address for the ESP32 node                         |
| managed route    | Route provided by ZeroTier NetworkConfig                                |
| overlay          | ZeroTier virtual network                                                |
| underlay         | Physical or emulated IP network used to transport ZeroTier packets      |
| ZT ARP           | ARP inside the ZeroTier virtual overlay                                 |
| ICMP Ping        | Real ICMP Echo Request/Reply payload                                    |
| UDP payload      | Real UDP application/test payload                                       |
| TCP payload      | Real TCP application/test payload                                       |
| lwIP             | ESP-IDF TCP/IP stack                                                    |
| QEMU             | ESP32 emulator used for repeatable runtime testing                      |
| canonical NVS    | Known-good ESP32-QEMU NVS image containing provisioned identity         |

## 5. ZeroTier Semantics on ESP32

New-ESP32-Lite must preserve the ZeroTier semantics that matter for network membership and dataplane correctness.

The following concepts are central.

### 5.1 Node Identity

The ESP32 node uses provisioned ZeroTier identity material.

Identity generation and expensive local proof-of-work validation are not part of the normal ESP32 runtime path.

Boot-persistent NVS state is strictly limited to provisioned identity/security material:

- node identity or provisioned identity material,
- provisioned proof-of-work or validation metadata required by the provisioned identity model,
- minimal metadata required to validate that the provisioned identity belongs to this device or image.

NetworkConfig, managed IPs, managed routes, controller runtime config, peer tables, multicast subscriptions, route tables, and dataplane/runtime state must not be persisted to NVS as boot baseline state.

All ZeroTier runtime membership state must be rebuilt from live controller NetworkConfig after boot.

Any future cache of controller/runtime data must be treated as a separate optional feature, not as New-ESP32-Lite boot baseline state, and must not be implemented without explicit approval.

### 5.2 Network Membership

The node must join the configured ZeroTier network and reach ONLINE state before dataplane behavior can be considered valid.

ONLINE alone is not enough to prove dataplane readiness. It only proves that the node reached a ZeroTier control-plane state where further configuration can proceed.

### 5.3 NetworkConfig

NetworkConfig is the authoritative source for managed IPs, routes, rules, and membership state.

New-ESP32-Lite must translate NetworkConfig into bounded ESP32 runtime state.

Runtime readiness requires that NetworkConfig effects are reflected in:

- managed IP state,
- managed route state,
- multicast subscription state,
- lwIP-facing state,
- diagnostics-visible state.

### 5.4 Managed IP

A managed IP is not complete merely because an address is stored.

For ESP32 runtime correctness, managed IP installation requires:

- local runtime IP state,
- lwIP netif address state,
- own-IP multicast subscription for address resolution,
- diagnostic visibility.

Invariant:

A managed IP without its own-IP multicast address-resolution subscription is incomplete runtime state.

### 5.5 Managed Route

A managed route must be represented in ESP32 route state and exposed to lwIP decisions where needed.

ESP32 does not install routes using Linux commands. Route behavior is implemented through ESP32-specific route tables and lwIP hooks.

Current route scope:

- direct managed subnet routes are in scope,
- default-route behavior is not a primary New-ESP32-Lite target,
- gateway-routes via a ZeroTier peer require separate design,
- IPv6 route handling is future work.

### 5.6 Multicast Subscription

ZeroTier address resolution depends on multicast group membership.

The node must subscribe to the correct address-resolution multicast groups for its own managed IPs. Without this, other peers may be unable to resolve the ESP32 node's virtual MAC, causing ZT ARP and IP payload traffic to fail.

### 5.7 Virtual Ethernet Frames

ZeroTier dataplane traffic is Ethernet-like at the virtual network layer.

New-ESP32-Lite must bridge this virtual frame flow to and from lwIP in a bounded way.

Frame handling must avoid:

- unbounded heap allocation,
- unbounded queues,
- unclear ownership,
- hidden cross-thread mutation.

## 6. Runtime Call Flow

This section describes the expected runtime progression from boot to dataplane-ready state.

The call flow is not only a conceptual sequence. Each phase must be traceable to concrete source files and functions.

The source maps currently identify primary files and responsibilities. Function-level call mapping should be completed as a later documentation pass using source inspection rather than memory.

### 6.1 Boot and Platform Initialization

Purpose:

Initialize ESP-IDF, lwIP, platform glue, and New-ESP32-Lite runtime structures.

Expected behavior:

- initialize ESP32 platform glue,
- initialize lwIP-facing network interface state,
- prepare bounded runtime tables,
- load provisioned identity material,
- prepare ZeroTier node creation.

Primary source areas:

- `ZeroTierOne/osdep/ESP32LiteAppGlue.*`
- `ZeroTierOne/osdep/ESP32LitePort.*`
- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`
- `ZeroTierOne/osdep/ESP32NetworkState.*`
- QEMU/application glue in `/qemu-project/main`

State involved:

- platform initialization state,
- ZeroTier node pointer,
- network ID,
- lwIP netif pointer,
- ESP32 runtime network state.

Evidence:

- boot log markers,
- node creation log markers,
- successful transition to join/start sequence.

Current status:

- Build integration is verified.
- Runtime boot with the latest build must be re-verified.

### 6.2 Node Creation and Join

Purpose:

Create the ZeroTier node and join the configured network.

Expected behavior:

- create the ZeroTier node,
- bind ESP32 platform callbacks,
- start the ZeroTier node task,
- request network join.

Primary source areas:

- `ZeroTierOne/osdep/ESP32LiteAppGlue.*`
- `ZeroTierOne/osdep/ESP32NodeTask.*`
- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`
- `ZeroTierOne/include/ZeroTierOne.h`

State involved:

- node lifecycle state,
- node task state,
- pending background work deadlines,
- network ID.

Evidence:

- `ZT_Node_new` succeeds,
- `ZT_Node_join` succeeds,
- node task is running,
- ZeroTier background processing occurs.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.3 Background Processing

Purpose:

Run ZeroTier core background work from a controlled FreeRTOS task.

Expected behavior:

- process ZeroTier timers and background tasks,
- dispatch pending network events,
- avoid unbounded work in application context,
- keep synchronization rules explicit.

Primary source areas:

- `ZeroTierOne/osdep/ESP32NodeTask.*`

Threading rules:

- ZeroTier background work is owned by the node task.
- Shared state must be single-writer or explicitly synchronized.
- Logging must not occur inside critical sections.
- Queue depth and pending work must remain bounded.

Evidence:

- node task starts,
- background task deadlines advance,
- no task watchdog or stack instability,
- no crash from logging inside critical sections.

Current status:

- Build-level constructor initialization issue has been fixed.
- Runtime stability must be re-verified.

### 6.4 ONLINE State

Purpose:

Confirm that the ZeroTier node has reached ONLINE state and can communicate with the network controller.

Expected behavior:

- receive ONLINE event,
- continue NetworkConfig request/receive flow,
- make node state visible to diagnostics.

Primary source areas:

- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`
- `ZeroTierOne/osdep/ESP32LiteDiagnostics.*`
- `ZeroTierOne/osdep/ESP32NodeTask.*`

State involved:

- node online flag,
- network membership state,
- diagnostics state.

Evidence:

- ONLINE log marker,
- controller communication,
- subsequent NetworkConfig reception.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.5 NetworkConfig Reception and Application

Purpose:

Translate ZeroTier NetworkConfig into ESP32 runtime network state.

Expected behavior:

- receive NetworkConfig from ZeroTier core,
- parse managed IPs,
- parse managed routes,
- update bounded state tables,
- trigger lwIP and multicast lifecycle updates.

Primary source areas:

- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`
- `ZeroTierOne/osdep/ESP32NetworkState.*`
- `ZeroTierOne/osdep/ESP32LiteDiagnostics.*`

State involved:

- active network ID,
- managed IP table,
- managed route table,
- config generation/state version.

Evidence:

- NetworkConfig log markers,
- managed IP appears in state,
- managed route appears in state,
- no incorrect eviction/removal,
- diagnostic dump matches expected config.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.6 Managed IP Installation

Purpose:

Install the ZeroTier-managed IP into ESP32 runtime state and lwIP.

ZeroTier semantic:

A managed IP is not fully installed until the ESP32 runtime state, lwIP netif state, and own-IP multicast subscription state are consistent.

Expected behavior:

- add or update managed IP entry,
- update lwIP netif address state,
- mark IP active,
- prepare address-resolution multicast subscription.

Primary source areas:

- `ZeroTierOne/osdep/ESP32NetworkState.*`
- `ZeroTierOne/osdep/ESP32LwIPGlue.*`
- `ZeroTierOne/osdep/ESP32NetLink.*`
- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`

State updated:

- managed IP table,
- lwIP netif address,
- multicast subscription flag,
- generation counter.

Evidence:

- managed IP add/update marker,
- GDB-readable IP table entry,
- lwIP netif has expected address,
- no unexpected IP eviction,
- successful ZT ARP and ICMP payload.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.7 Managed Route Installation

Purpose:

Install ZeroTier managed routes into ESP32 runtime state and expose them to lwIP routing decisions.

Expected behavior:

- add direct managed subnet route,
- store route in bounded route table,
- expose route decision through ESP32NetLink/lwIP hook path,
- mark route active and installed.

Primary source areas:

- `ZeroTierOne/osdep/ManagedRoute.cpp`
- `ZeroTierOne/osdep/ESP32NetLink.*`
- `ZeroTierOne/osdep/ESP32NetworkState.*`
- `ZeroTierOne/osdep/ESP32LwIPHooks.h`

State updated:

- route table,
- lwIP route-facing state,
- route generation counter.

Evidence:

- route add/update marker,
- GDB-readable route entry,
- route marked active,
- route marked in-lwIP or equivalent,
- successful IP payload over managed subnet.

Current scope:

- direct managed subnet route support is in scope,
- default route is not a primary target,
- gateway routes via ZeroTier peer require separate design,
- IPv6 is future work.

Current status:

- ESP_PLATFORM build support in `ManagedRoute.cpp` is fixed.
- Runtime route behavior must be re-verified.

### 6.8 Multicast Subscription Lifecycle

Purpose:

Maintain multicast subscriptions required by ZeroTier address resolution.

ZeroTier semantic:

A node with a managed IP must subscribe to the multicast group used for address resolution for that IP. Without this, other peers may not be able to resolve the node's virtual MAC and dataplane traffic can fail.

Expected behavior:

- derive own-IP address-resolution multicast group,
- subscribe after managed IP installation,
- avoid duplicate subscription state,
- update subscription on managed IP change,
- eventually remove or replace stale subscriptions when IP lifecycle requires it.

Primary source areas:

- `ZeroTierOne/osdep/ESP32NetworkState.*`
- `ZeroTierOne/osdep/ESP32PlatformCallbacks.*`
- `ZeroTierOne/osdep/ESP32LiteAppGlue.*`
- ZeroTier multicast subscription API via `ZT_Node_multicastSubscribe`

State updated:

- multicast subscription table or flag,
- managed IP entry subscription state.

Evidence:

- own-IP multicast subscribe marker,
- expected multicast ADI/group,
- peer can resolve ESP32 managed IP,
- ZT ARP succeeds from another ZeroTier member,
- ICMP payload succeeds after ARP.

Invariant:

A managed IP without matching own-IP address-resolution multicast subscription is incomplete runtime state.

Current status:

- This behavior has been critical in earlier debugging.
- It must be re-verified after the latest build integration changes.

### 6.9 ZeroTier-to-lwIP Frame Delivery

Purpose:

Deliver virtual network frames received from ZeroTier into ESP32/lwIP.

Expected behavior:

- receive ZeroTier virtual network frame,
- validate frame size and ownership,
- allocate or reference bounded frame buffer,
- inject frame into lwIP/netif path,
- avoid unbounded heap use.

Primary source areas:

- `ZeroTierOne/osdep/ESP32EthernetTap.*`
- `ZeroTierOne/osdep/ESP32FramePool.*`
- `ZeroTierOne/osdep/ESP32LwIPGlue.*`
- `ZeroTierOne/osdep/ESP32EgressTrace.*`

Traffic examples:

- ZT ARP request to ESP32,
- ICMP Echo Request to ESP32 managed IP,
- TCP payload to ESP32 listener.

Evidence:

- RX frame markers,
- lwIP receives frame,
- ESP32 application receives IP payload,
- TCP echo or server response succeeds.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.10 lwIP-to-ZeroTier Frame Delivery

Purpose:

Transmit frames generated by lwIP or ESP32 application code into the ZeroTier overlay.

Expected behavior:

- lwIP produces Ethernet/IP frame for ZeroTier netif,
- ESP32-Lite egress path receives frame,
- route/hook decision chooses ZeroTier path,
- frame is passed to ZeroTier core,
- ZeroTier sends overlay traffic to peer.

Primary source areas:

- `ZeroTierOne/osdep/ESP32LwIPGlue.*`
- `ZeroTierOne/osdep/ESP32EthernetTap.*`
- `ZeroTierOne/osdep/ESP32FramePool.*`
- `ZeroTierOne/osdep/ESP32NetLink.*`
- `ZeroTierOne/osdep/ESP32EgressTrace.*`

Traffic examples:

- UDP payload from ESP32-QEMU to zt-alpine,
- ARP reply,
- ICMP Echo Reply,
- TCP response.

Evidence:

- egress-frame trace marker,
- route decision trace,
- peer receives payload,
- tcpdump/pcap or listener output on peer side.

Current status:

- Requires runtime re-verification after latest build integration changes.

### 6.11 Diagnostics and Trace Hooks

Purpose:

Provide enough observability to debug ESP32 runtime behavior without relying on guesses.

Expected behavior:

- expose route decision state,
- expose managed IP state,
- expose route table state,
- expose egress frame markers,
- support GDB inspection where practical,
- avoid excessive logging in hot paths.

Primary source areas:

- `ZeroTierOne/osdep/ESP32LiteDiagnostics.*`
- `ZeroTierOne/osdep/ESP32EgressTrace.*`
- route debug code in component port files,
- GDB-callable debug helpers where enabled.

Evidence:

- route decision dumps,
- egress-frame trace,
- state table dumps,
- bounded logs suitable for QEMU testing.

Current status:

- Diagnostic strategy is part of the architecture.
- Function-level diagnostic mapping should be completed from source inspection.

## 7. Runtime Call Flow to Source Map

This section maps runtime phases to primary implementation files. It is intended as a navigation guide.

| Runtime phase                    | Primary files                                                                   | Purpose                                                          |
|----------------------------------|---------------------------------------------------------------------------------|------------------------------------------------------------------|
| Boot/platform initialization     | `ESP32LiteAppGlue.*`, `ESP32LitePort.*`, `ESP32PlatformCallbacks.*`             | Initialize ESP32-Lite platform layer and prepare ZeroTier runtime |
| Node creation/join               | `ESP32LiteAppGlue.*`, `ESP32NodeTask.*`, `ZeroTierOne.h`                        | Create ZeroTier node, join network, start node task              |
| Background processing            | `ESP32NodeTask.*`                                                               | Run ZeroTier background work in FreeRTOS context                 |
| ONLINE handling                  | `ESP32PlatformCallbacks.*`, `ESP32LiteDiagnostics.*`                            | Handle node/network state changes                                |
| NetworkConfig application        | `ESP32PlatformCallbacks.*`, `ESP32NetworkState.*`                               | Translate controller config into bounded ESP32 state             |
| Managed IP lifecycle             | `ESP32NetworkState.*`, `ESP32LwIPGlue.*`, `ESP32NetLink.*`                      | Install, update, or remove managed IP state                      |
| Managed route lifecycle          | `ManagedRoute.cpp`, `ESP32NetLink.*`, `ESP32LwIPHooks.h`                        | Represent managed routes and expose decisions to lwIP            |
| Multicast subscription lifecycle | `ESP32NetworkState.*`, `ESP32PlatformCallbacks.*`, `ESP32LiteAppGlue.*`         | Subscribe to address-resolution multicast groups                 |
| ZeroTier-to-lwIP frames          | `ESP32EthernetTap.*`, `ESP32FramePool.*`, `ESP32LwIPGlue.*`                     | Deliver overlay frames into lwIP                                 |
| lwIP-to-ZeroTier frames          | `ESP32LwIPGlue.*`, `ESP32EthernetTap.*`, `ESP32NetLink.*`, `ESP32EgressTrace.*` | Send lwIP-generated frames into ZeroTier                         |
| Diagnostics/tracing              | `ESP32LiteDiagnostics.*`, `ESP32EgressTrace.*`                                  | Make runtime state and dataplane decisions observable            |
| Build integration                | `ESP32LitePort.cmake`, `components/zerotier_node/CMakeLists.txt`                | Integrate ESP32-Lite sources and compile/link configuration      |

## 8. Threading and Ownership Model

New-ESP32-Lite must define ownership of runtime state explicitly.

The default ownership model is:

| Area                             | Owner                                    | Notes                                                |
|----------------------------------|------------------------------------------|------------------------------------------------------|
| ZeroTier background processing   | `ESP32NodeTask`                          | Runs in a dedicated FreeRTOS task                    |
| NetworkConfig application        | ZeroTier callback / node task context    | Must update bounded runtime state safely             |
| Managed IP table                 | ESP32 network state layer                | Prefer single-writer update model                    |
| Route table                      | ESP32 network state / netlink layer      | Must be safe for lwIP hook reads                     |
| lwIP netif state                 | lwIP-facing glue                         | Must respect ESP-IDF/lwIP threading rules            |
| Diagnostics                      | Read-only or explicitly synchronized     | Must not perturb runtime behavior                    |
| Frame pool                       | Frame pool owner with bounded allocation | No unbounded heap growth in dataplane hot path       |

Rules:

- Do not log inside critical sections.
- Do not perform blocking operations inside critical sections.
- Keep critical sections short.
- Avoid unbounded queue growth.
- Avoid dynamic allocation in hot dataplane paths unless bounded by a pool.
- Clearly document any cross-thread state access.
- GDB/debug reads should be treated as observational and should not mutate runtime state unless explicitly designed.

## 9. Network State Model

`ESP32NetworkState` is the central bounded runtime state layer for New-ESP32-Lite.

It should represent:

- active network ID,
- managed IP entries,
- managed route entries,
- multicast subscription status,
- route installation status,
- generation counters,
- diagnostic snapshots.

The state model should allow the system to answer:

- Which managed IPs are active?
- Which routes are active?
- Which routes have been exposed to lwIP?
- Which managed IPs have multicast subscriptions?
- Has the config changed since the last diagnostic read?
- Is the node dataplane-ready?

## 10. lwIP Integration

lwIP integration is implemented through ESP32-specific glue and hook files.

Primary files:

- `ESP32LwIPGlue.*`
- `ESP32LwIPHooks.h`
- `ESP32NetLink.*`
- `ESP32LitePort.cmake`

Important behavior:

- configure lwIP to include the ESP32-Lite hook file,
- expose route/gateway decisions through C-callable hook functions,
- connect ZeroTier virtual interface behavior to lwIP netif behavior,
- keep lwIP hook logic minimal and deterministic.

Important link requirement:

`zt_esp32_lwip_etharp_get_gw` is referenced by lwIP C code but implemented in the ZeroTier ESP32-Lite component archive. The build must force archive extraction for this symbol.

The component integration uses:

```cmake
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--undefined=zt_esp32_lwip_etharp_get_gw"
)
```

## 11. Dataplane Frame Flow

New-ESP32-Lite has two dataplane directions.

### 11.1 Overlay to ESP32/lwIP

ZeroTier core receives frames from the overlay and delivers them through the ESP32 virtual Ethernet path.

Flow:

```text
ZeroTier overlay
-> ZeroTier core
-> ESP32EthernetTap
-> ESP32FramePool
-> ESP32LwIPGlue
-> lwIP netif/input
-> ESP32 application or protocol stack
```

### 11.2 ESP32/lwIP to Overlay

lwIP or ESP32 application code generates traffic toward a ZeroTier-managed peer.

Flow:

```text
ESP32 application/lwIP
-> lwIP netif/output
-> ESP32LwIPGlue
-> ESP32NetLink route/hook decision
-> ESP32EthernetTap
-> ZeroTier core
-> ZeroTier overlay
-> peer
```

Both directions must be verified with real payload traffic.

## 12. Diagnostics and Debugging

Diagnostics are part of the architecture, not an afterthought.

The port must make the following observable:

- node online state,
- NetworkConfig receipt,
- managed IP table,
- route table,
- multicast subscription state,
- route decision output,
- egress frame markers,
- RX/TX frame counters,
- heap/stack pressure where relevant.

QEMU/GDB diagnostics should be used carefully:

- use batch GDB where appropriate,
- run GDB inferior calls from `zt_task`, not IDLE0,
- avoid relying on QEMU-egress-only evidence for payload correctness,
- collect peer-side evidence where possible.

## 13. Build and Component Integration

New-ESP32-Lite is integrated into ESP-IDF through the `zerotier_node` component.

Primary files:

- `components/zerotier_node/CMakeLists.txt`
- `ZeroTierOne/osdep/ESP32LitePort.cmake`
- `/qemu-project/CMakeLists.txt`

The component CMake file includes `ESP32LitePort.cmake`, prefixes source paths relative to `/project/ZeroTierOne`, and appends them to the component source list.

`zt_esp32_lite_configure_component(${COMPONENT_LIB})` applies ESP32-Lite compile definitions, include directories, and lwIP hook integration.

The QEMU project separately propagates ccache into the bootloader ExternalProject by adding `-DCCACHE_ENABLE=1` to `EXTRA_CMAKE_ARGS`.

Risk:

The `/qemu-project/CMakeLists.txt` ccache change is not protected by the `/project` git history. If `/qemu-project` is reset or recreated, bootloader ccache propagation may be lost unless this change is preserved separately.

## 14. Source File Responsibility Map

### 14.1 ESP32LiteAppGlue

Files:

- `ESP32LiteAppGlue.cpp`
- `ESP32LiteAppGlue.hpp`

Role:

Application-facing glue for creating, starting, and coordinating the ESP32-Lite ZeroTier runtime.

Used by runtime phases:

- boot/platform initialization,
- node creation and join,
- multicast subscription coordination,
- QEMU/application integration.

### 14.2 ESP32NodeTask

Files:

- `ESP32NodeTask.cpp`
- `ESP32NodeTask.hpp`

Role:

Owns the FreeRTOS task that drives ZeroTier background processing.

Used by runtime phases:

- node creation and join,
- background processing,
- NetworkConfig event dispatch.

Important constraints:

- queue handling must remain bounded,
- do not log inside critical sections,
- initialization order must match header member order.

### 14.3 ESP32PlatformCallbacks

Files:

- `ESP32PlatformCallbacks.cpp`
- `ESP32PlatformCallbacks.hpp`

Role:

Receives ZeroTier platform callbacks and translates them into ESP32-Lite runtime updates.

Used by runtime phases:

- ONLINE handling,
- NetworkConfig application,
- managed IP lifecycle,
- route lifecycle,
- multicast lifecycle.

### 14.4 ESP32NetworkState

Files:

- `ESP32NetworkState.cpp`
- `ESP32NetworkState.hpp`

Role:

Central bounded runtime state for managed IPs, routes, multicast subscription flags, and diagnostics.

Used by runtime phases:

- NetworkConfig application,
- managed IP lifecycle,
- route lifecycle,
- multicast lifecycle,
- diagnostics.

### 14.5 ESP32NetLink

Files:

- `ESP32NetLink.cpp`
- `ESP32NetLink.hpp`

Role:

ESP32-specific route and netif decision layer. Provides route lookup and lwIP hook support.

Used by runtime phases:

- route installation,
- lwIP route decisions,
- lwIP-to-ZeroTier egress.

Important exported symbol:

- `zt_esp32_lwip_etharp_get_gw`

### 14.6 ESP32LwIPGlue

Files:

- `ESP32LwIPGlue.cpp`
- `ESP32LwIPGlue.hpp`

Role:

Connects ESP32-Lite virtual interface behavior to lwIP.

Used by runtime phases:

- managed IP installation,
- ZeroTier-to-lwIP frame delivery,
- lwIP-to-ZeroTier frame delivery.

### 14.7 ESP32LwIPHooks

Files:

- `ESP32LwIPHooks.h`

Role:

C hook interface used by lwIP to call into ESP32-Lite route/gateway logic.

Used by runtime phases:

- route decisions,
- lwIP ETHARP gateway lookup.

### 14.8 ESP32EthernetTap

Files:

- `ESP32EthernetTap.cpp`
- `ESP32EthernetTap.hpp`

Role:

Virtual Ethernet/TAP-like boundary between ZeroTier core and ESP32/lwIP.

Used by runtime phases:

- ZeroTier-to-lwIP frame delivery,
- lwIP-to-ZeroTier frame delivery.

### 14.9 ESP32FramePool

Files:

- `ESP32FramePool.cpp`
- `ESP32FramePool.hpp`

Role:

Bounded frame allocation and ownership model for dataplane frames.

Used by runtime phases:

- RX frame handling,
- TX frame handling,
- dataplane hot paths.

### 14.10 ESP32EgressTrace

Files:

- `ESP32EgressTrace.cpp`
- `ESP32EgressTrace.hpp`

Role:

Tracing of egress frame decisions and dataplane output.

Used by runtime phases:

- lwIP-to-ZeroTier frame delivery,
- diagnostics,
- payload regression tests.

### 14.11 ESP32LiteDiagnostics

Files:

- `ESP32LiteDiagnostics.cpp`
- `ESP32LiteDiagnostics.hpp`

Role:

Diagnostic reporting for runtime state, route decisions, and test observability.

Used by runtime phases:

- ONLINE/config visibility,
- route decision debug,
- GDB/test evidence.

### 14.12 ESP32LitePort

Files:

- `ESP32LitePort.cpp`
- `ESP32LitePort.hpp`
- `ESP32LitePort.cmake`
- `ESP32LitePort.sdkconfig.defaults`
- `ESP32LitePortKconfig.fragment`

Role:

Port-level configuration and integration boundary for ESP32-Lite.

Used by runtime phases:

- build integration,
- compile definitions,
- lwIP hook integration,
- bounded port configuration.

### 14.13 ManagedRoute.cpp

Role:

ZeroTier managed route integration point. ESP_PLATFORM support is required so ESP32 route management is handled by ESP32-specific code rather than falling through to unsupported platform behavior.

Used by runtime phases:

- managed route lifecycle,
- route installation/removal.

### 14.14 EthernetTap.cpp

Role:

Existing ZeroTier OS-dependent TAP abstraction modified to recognize or route ESP32-specific TAP behavior.

Used by runtime phases:

- virtual network frame flow,
- ESP32 EthernetTap integration.

## 15. Invariants

The following invariants should hold during correct runtime operation:

- A managed IP is incomplete unless its own-IP multicast address-resolution subscription is active.
- A managed route from NetworkConfig is incomplete unless represented in ESP32 route state.
- A route exposed to lwIP must be visible in diagnostics.
- Boot-persistent NVS state must contain only provisioned identity/security material: node identity or provisioned identity material, provisioned proof-of-work or validation metadata, and minimal identity-validation metadata.
- NetworkConfig, managed IPs, managed routes, controller runtime config, peer tables, multicast subscriptions, route tables, and dataplane/runtime state must not be persisted to NVS as boot baseline state.
- All ZeroTier runtime membership state must be rebuilt from live controller NetworkConfig after boot.
- ZT ARP success alone is not proof of IP dataplane success.
- ICMP, UDP, and TCP payload tests are required for dataplane validation.
- QEMU underlay must use `10.11.0.0/24`.
- zt-alpine underlay must use `10.20.0.20/24`.
- MASQUERADE, SNAT, and DNAT must not be used for the test networks.
- The old `10.10.0.0/24` transit topology is obsolete and must not be referenced or recreated.
- Frame handling must remain bounded.
- Route, IP, multicast, and queue tables must have explicit limits.
- No logging or blocking work should occur inside critical sections.
- QEMU-specific test glue must not leak into generic ESP32-Lite architecture without explicit review.

## 16. Failure Modes

Important failure modes to detect and classify are described below.

### 16.1 NetworkConfig Missing

The node may be ONLINE but not configured. Dataplane is not ready.

### 16.2 Managed IP Missing

NetworkConfig was not applied correctly, or IP lifecycle logic failed.

### 16.3 Multicast Subscription Missing

Peers may fail to resolve ESP32 managed IP. ZT ARP may fail from other peers.

### 16.4 Route Missing

ESP32 may have an IP but cannot route overlay payload correctly.

### 16.5 lwIP Hook Failure

lwIP may not select the ZeroTier path or may fail ETHARP gateway lookup.

### 16.6 Frame Pool Exhaustion

Dataplane may drop packets under load.

### 16.7 Heap or Stack Pressure

The node may boot and pass short tests but fail during longer operation.

### 16.8 Underlay Path Failure

ZeroTier may relay or fail direct path. Runtime tests must distinguish overlay success via relay from intended clean direct-path topology where relevant.

### 16.9 NAT Contamination

Tests using MASQUERADE, SNAT, or DNAT for test networks are invalid for proving the intended routed no-NAT topology.

## 17. Runtime Test Strategy

Runtime validation should proceed in layers.

### 17.1 Build Validation

Required evidence:

- `idf.py reconfigure`
- `idf.py build`
- bootloader generated,
- app binary generated,
- partition size acceptable.

### 17.2 Boot Validation

Required evidence:

- QEMU boots,
- canonical NVS identity loads from `/project/nvs/esp32-qemu-d526b558a5-nvs.bin`,
- ZeroTier node is created,
- node joins configured network.

### 17.3 Control-Plane Validation

Required evidence:

- node reaches ONLINE,
- NetworkConfig received,
- managed IP installed,
- route installed,
- multicast subscription active.

### 17.4 Dataplane Validation

Required evidence:

- ZT ARP succeeds,
- ICMP Ping succeeds,
- TCP payload succeeds,
- UDP payload succeeds,
- evidence collected from both ESP32-QEMU and peer side where possible.

### 17.5 Topology Validation

Required evidence:

- QEMU underlay is `10.11.0.0/24`,
- zt-alpine underlay is `10.20.0.20/24`,
- no MASQUERADE, SNAT, or DNAT for test networks,
- old `10.10.0.0/24` topology is not used,
- peer path evidence collected when relevant.

## 18. Current Verified State and Reverification Needed

The current New-ESP32-Lite build integration has been verified with:

- commit `a6ea313`: ESP32-Lite cumulative port patch,
- commit `3bdb53e`: ESP32-Lite QEMU build integration fix,
- successful `idf.py build`,
- generated QEMU firmware binary,
- app size `0xe0b40`,
- 12% free app partition space.

The `/qemu-project/CMakeLists.txt` ccache bootloader propagation change is verified but is outside the `/project` git repository.

Current verified items:

| Item                                           | Status   |
|------------------------------------------------|----------|
| ESP32-Lite source patch applied                | Verified |
| Build integration commit exists                | Verified |
| `idf.py build` completes                       | Verified |
| `zt_esp32_lwip_etharp_get_gw` link issue fixed | Verified |
| Main build ccache enabled                      | Verified |
| Bootloader ccache propagation configured       | Verified |
| `/project` tracked working tree after commit   | Verified |

Runtime items requiring re-verification:

| Runtime item                   | Status                   |
|--------------------------------|--------------------------|
| QEMU boot with current firmware | Requires re-verification |
| Canonical NVS identity load     | Requires re-verification |
| ZeroTier node creation          | Requires re-verification |
| ZeroTier join                   | Requires re-verification |
| ONLINE state                    | Requires re-verification |
| NetworkConfig reception         | Requires re-verification |
| Managed IP installation         | Requires re-verification |
| Managed route installation      | Requires re-verification |
| Own-IP multicast subscription   | Requires re-verification |
| ZT ARP                          | Requires re-verification |
| ICMP Ping                       | Requires re-verification |
| UDP payload                     | Requires re-verification |
| TCP payload                     | Requires re-verification |
| Clean routed no-NAT topology    | Requires re-verification |

## 19. Current Next Action

The next recommended action is to run a QEMU runtime smoke test using the current build and canonical NVS.

Canonical NVS:

- `/project/nvs/esp32-qemu-d526b558a5-nvs.bin`

Purpose:

Verify that the now-building New-ESP32-Lite firmware still works at runtime under the intended clean routed no-NAT topology.

Required runtime checks:

- QEMU boots with the current firmware,
- canonical NVS identity is used,
- ZeroTier node is created,
- node joins network `db64858fed0b2566`,
- node reaches ONLINE,
- NetworkConfig is received,
- managed IP `172.25.204.197/16` is installed,
- route `172.25.0.0/16` is installed,
- own-IP multicast subscription is active,
- ZT ARP passes,
- ICMP Ping passes,
- TCP payload passes,
- UDP payload passes.

Required topology checks:

- QEMU underlay is `10.11.0.0/24`,
- zt-alpine underlay is `10.20.0.20/24`,
- no MASQUERADE, SNAT, or DNAT is used for test networks,
- old `10.10.0.0/24` topology is not referenced or recreated.

If QEMU crashes:

- stop immediately,
- perform deep RCA,
- do not continue with retries that hide the crash cause.

## 20. Known Limitations and Future Work

Known limitations:

- IPv6 is not yet implemented.
- Default-route support is not a primary New-ESP32-Lite goal.
- Gateway-routes through a ZeroTier peer require separate design.
- Multicast lifecycle must be robust across managed IP changes.
- Long-duration reconnect and stability testing is still required.
- Heap minimum must be monitored carefully as features are added.
- NetworkConfig persistence is not treated as required boot baseline state.
- The current architecture prioritizes runtime correctness over full feature parity.

Future work:

- function-level call map from source inspection,
- robust reconnect behavior,
- Wi-Fi interruption recovery,
- long-duration heap/stack stability tests,
- bounded peer/path table tuning,
- IPv6 design,
- gateway route evaluation,
- ESPHome integration boundary,
- Home Assistant use-case validation,
- upstream issue/PR preparation for any generic ZeroTierOne fixes.

## 21. Added and Modified Files

### 21.1 Added Files

The following files were added for the New-ESP32-Lite port:

| File                                                 | Primary role                                      |
|------------------------------------------------------|---------------------------------------------------|
| `ZeroTierOne/osdep/ESP32EgressTrace.cpp`             | Egress tracing implementation                     |
| `ZeroTierOne/osdep/ESP32EgressTrace.hpp`             | Egress tracing interface                          |
| `ZeroTierOne/osdep/ESP32EthernetTap.cpp`             | ESP32 virtual Ethernet/TAP implementation         |
| `ZeroTierOne/osdep/ESP32EthernetTap.hpp`             | ESP32 virtual Ethernet/TAP interface              |
| `ZeroTierOne/osdep/ESP32FramePool.cpp`               | Bounded dataplane frame pool implementation       |
| `ZeroTierOne/osdep/ESP32FramePool.hpp`               | Bounded dataplane frame pool interface            |
| `ZeroTierOne/osdep/ESP32LiteAppGlue.cpp`             | Application-facing ESP32-Lite glue implementation |
| `ZeroTierOne/osdep/ESP32LiteAppGlue.hpp`             | Application-facing ESP32-Lite glue interface      |
| `ZeroTierOne/osdep/ESP32LiteDiagnostics.cpp`         | Diagnostics implementation                        |
| `ZeroTierOne/osdep/ESP32LiteDiagnostics.hpp`         | Diagnostics interface                             |
| `ZeroTierOne/osdep/ESP32LitePort.cmake`              | ESP32-Lite CMake integration                      |
| `ZeroTierOne/osdep/ESP32LitePort.cpp`                | ESP32-Lite port implementation                    |
| `ZeroTierOne/osdep/ESP32LitePort.hpp`                | ESP32-Lite port interface                         |
| `ZeroTierOne/osdep/ESP32LitePort.sdkconfig.defaults` | ESP32-Lite sdkconfig defaults                     |
| `ZeroTierOne/osdep/ESP32LitePortKconfig.fragment`    | ESP32-Lite Kconfig fragment                       |
| `ZeroTierOne/osdep/ESP32LiteQemuAppGlue.example.cpp` | QEMU application glue example                     |
| `ZeroTierOne/osdep/ESP32LwIPGlue.cpp`                | lwIP glue implementation                          |
| `ZeroTierOne/osdep/ESP32LwIPGlue.hpp`                | lwIP glue interface                               |
| `ZeroTierOne/osdep/ESP32LwIPHooks.h`                 | lwIP hook definitions                             |
| `ZeroTierOne/osdep/ESP32NetLink.cpp`                 | ESP32 route/netif decision implementation         |
| `ZeroTierOne/osdep/ESP32NetLink.hpp`                 | ESP32 route/netif decision interface              |
| `ZeroTierOne/osdep/ESP32NetworkState.cpp`            | Runtime network state implementation             |
| `ZeroTierOne/osdep/ESP32NetworkState.hpp`            | Runtime network state interface                   |
| `ZeroTierOne/osdep/ESP32NodeTask.cpp`                | FreeRTOS ZeroTier node task implementation        |
| `ZeroTierOne/osdep/ESP32NodeTask.hpp`                | FreeRTOS ZeroTier node task interface             |
| `ZeroTierOne/osdep/ESP32PlatformCallbacks.cpp`       | ESP32 platform callback implementation            |
| `ZeroTierOne/osdep/ESP32PlatformCallbacks.hpp`       | ESP32 platform callback interface                 |

### 21.2 Modified Files

The following existing files were modified for the New-ESP32-Lite port and build integration:

| File                                      | Reason for modification                                  |
|-------------------------------------------|----------------------------------------------------------|
| `ZeroTierOne/include/ZeroTierOne.h`       | Public API or port-facing declarations                   |
| `ZeroTierOne/osdep/EthernetTap.cpp`       | ESP32-specific virtual Ethernet/TAP behavior             |
| `ZeroTierOne/osdep/ManagedRoute.cpp`      | ESP_PLATFORM managed route support                       |
| `ZeroTierOne/osdep/ESP32LitePort.cmake`   | lwIP hook include paths and component configuration      |
| `ZeroTierOne/osdep/ESP32NodeTask.cpp`     | FreeRTOS member initialization order/build fix           |
| `components/zerotier_node/CMakeLists.txt` | ESP32-Lite source integration and link-symbol extraction |
| `/qemu-project/CMakeLists.txt`            | Bootloader ccache propagation outside the `/project` repo |
