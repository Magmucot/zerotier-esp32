# Potential ESP32-Lite Memory Optimizations

## Introduction

This document summarizes memory optimization candidates for the ESP32-Lite ZeroTier/QEMU port.

The main goal is to reduce static RAM, runtime heap pressure, stack risk, flash footprint, and unnecessary diagnostic overhead without breaking the currently working ZeroTier dataplane. The current implementation has already proven ZeroTier ONLINE state, managed IP assignment, direct-path dataplane, and ZT ARP / ICMP / TCP / UDP payload traffic. Memory optimization must therefore be done conservatively and with regression evidence.

The optimization candidates are divided into two groups:

- **Part A** contains the low-risk candidate patch that has already been verified. It mainly removes diagnostic/test overhead from the normal build and rebalances stack memory.
- **Part B** contains experimental future candidates. These may save more memory, but they need isolated testing before they can be merged safely.

The recommended strategy is to take the safest verified savings first, then test higher-risk candidates one at a time. Changes that affect NetworkConfig parsing, lwIP resource sizing, peer/path discovery, or burst buffering must not be merged together because that would make regressions hard to attribute.

Recommended order:

1. **Review and commit Part A first.**  
   This is the best verified risk/reward candidate. It saves about **6.5 KB DRAM/BSS** while improving `zt_task` stack margin. See **Part A: Low-risk candidate patch**.

2. **Test `s_tx_defer_q 4 -> 2` as the next separate step.**  
   This has a clear measured potential of about **3 KB BSS** and is local in scope, but it needs burst-traffic verification. See **B5. `s_tx_defer_q 4 -> 2`**.

3. **Test NetworkConfig URL field reduction `2048 -> 256` only after live config proof.**  
   This has the largest single measured potential, about **5.4 KB BSS**, but it has higher semantic risk because it may affect NetworkConfig parsing or future controller/auth/SSO behavior. See **B1. NetworkConfig URL fields `2048 -> 256`**.

4. **Run lwIP tuning only as split experiments.**  
   DHCPS, TCP/socket/PCB counts, and TCP window/mailbox tuning must be measured separately. Combined lwIP tuning is too hard to reason about. See **B3. lwIP TCP/socket/DHCPS tuning**.

5. **Measure NetworkConfig dictionary usage before reducing capacity.**  
   The proposed `4096 -> 2048` dictionary reduction was not correctly measured and must not be merged blindly. See **B2. `ZT_NETWORKCONFIG_DICT_CAPACITY_OVERRIDE 4096 -> 2048`**.

6. **Defer peer/path limit changes until peak usage is known.**  
   Peer/path limits affect direct-path stability, reconnect, relay fallback, and discovery. These should wait until we have peak peer/path data from real traffic and reconnect tests. See **B4. Peer/path limit changes**.


## Scope and baseline assumptions

This document tracks potential memory optimizations for the ESP32-Lite ZeroTier/QEMU port.

The goal is to reduce static RAM, runtime heap pressure, stack risk, flash footprint, and diagnostic overhead without breaking the currently working ZeroTier dataplane.

The current stable baseline has proven:

- ZeroTier ONLINE
- managed IP assignment
- direct-path dataplane
- ZT ARP / ICMP / TCP / UDP payload traffic
- long-running stability evidence
- no NetworkConfig persistence in NVS
- NVS limited to provisioned identity/security material

This document separates already validated Part A candidates from experimental Part B candidates.

## Summary of memory candidates


| Prio | Candidate                       | Saving           | Status       | Risk |
|-----:|---------------------------------|------------------|--------------|------|
|    1 | Part A stack/debug cleanup      | ~6564 DRAM/BSS   | Ready review | L/M  |
|    2 | NetConf URL field shrink        | ~5376 BSS        | Future step  | M/H  |
|    3 | TX defer queue 4->2             | ~3080 BSS        | Best next B  | M    |
|    4 | Heap trace/debug buffer gating  | ~2304+ BSS       | In Part A    | L    |
|    5 | lwIP TCP/socket/DHCP trimming   | ~1-5 KB guess    | Split tests  | M/H  |
|    6 | TCP/UDP test service gating     | Variable         | In Part A    | L    |
|    7 | NetConf dict capacity shrink    | Unknown, 1-2 KB? | Not measured | H    |
|    8 | Peer/path table limit tuning    | Unknown, heap    | Defer        | M/H  |


## TODO list

### Approved for review

- Review and possibly commit Part A memory rebalancing.
- Keep final Part A source state only:
  - diagnostic heap tracing gated/off in normal build
  - TCP echo gated behind test macro
  - autonomous UDP sender gated behind test macro
  - `s_rx_stack = 28672`
  - `zt_task = 73728`
  - `s_tx_defer_q = 4`
  - no Part B changes included

### Future separate steps

- Run dedicated burst test for `s_tx_defer_q 4 -> 2`.
- Run live NetworkConfig proof for URL field reduction `2048 -> 256`.
- Run split lwIP tuning experiments:
  - DHCPS only
  - TCP/socket/PCB counts only
  - TCP window/SND_BUF/mailbox only
- Measure actual NetworkConfig dictionary usage before testing `4096 -> 2048`.
- Measure peak peer/path counts before reducing peer/path limits.

### Forbidden or stale recommendations

- Do not implement NetworkConfig NVS persistence.
- Do not persist managed IPs, routes, peer tables, multicast state, or dataplane runtime state in NVS.
- Do not change canonical NVS.
- Do not add NAT/MASQUERADE/SNAT/DNAT for the test networks.
- Do not merge Part B changes into Part A.

## Current decision status

| Area                       | Status                         |
|----------------------------|--------------------------------|
| Part A memory rebalancing  | Ready for review               |
| Part B experiments         | Not ready to merge             |
| NetConf NVS persistence    | Forbidden / stale              |
| Canonical NVS changes      | Forbidden                      |
| NAT/topology changes       | Forbidden                      |
| Commits/pushes             | Only after user review         |

---

# Part A: Low-risk candidate patch

Part A is the current preferred first memory optimization set.

It is intended as one coherent candidate patch because the individual pieces work together:

- remove diagnostic/test overhead from normal build
- reclaim static RAM
- rebalance stack memory
- improve `zt_task` safety
- keep net DRAM/BSS lower than baseline

Final measured Part A result:

| Metric       | Baseline |  Final |  Delta |
|--------------|---------:|-------:|-------:|
| DRAM         |    71168 |  64604 |  -6564 |
| .bss         |    60416 |  53944 |  -6472 |
| .data        |    10752 |  10660 |    -92 |
| IRAM         |    59842 |  58606 |  -1236 |
| Flash .text  |   563702 | 554594 |  -9108 |
| Flash .rodata|   292912 | 290204 |  -2708 |

## A1. Gate/remove diagnostic heap tracing from normal build

### Goal

Disable diagnostic heap tracing and heap task tracking in normal/minimal builds.

### Purpose

Heap tracing is useful during targeted diagnostics, but it should not reserve RAM or influence runtime behavior in normal ESP32-Lite builds.

The normal build should represent the intended lightweight runtime profile. Diagnostic builds should explicitly opt in to heavier tracing.

### Expected memory effect

- Removes/gates diagnostic heap trace buffer and related task tracking overhead.
- `s_trace_buf` was identified as diagnostic-only and about 2304 bytes BSS.
- Also reduces flash/rodata/logging noise.

### Risk analysis

Risk: low.

Possible downside:

- Normal build loses always-on heap tracing visibility.
- Future debugging requires enabling a diagnostic profile.

Mitigation:

- Keep explicit macros/config switches for diagnostic builds.
- Do not delete the ability to re-enable heap tracing.
- Document the normal-vs-diagnostic build distinction.

### Decision status

Approved for review as part of Part A.

---

## A2. Correct heap tracing Kconfig symbol

### Goal

Use a valid ESP-IDF 5.3.5 Kconfig symbol for disabling heap tracing.

### Purpose

cc-step126 initially introduced an invalid symbol:

- `CONFIG_HEAP_TRACING_DISABLED`

cc-step126b corrected this to:

- `CONFIG_HEAP_TRACING_OFF=y`

This makes clean builds reproducible and removes the warning from the build.

### Expected memory effect

No new functional optimization by itself, but it ensures that the intended heap tracing-off configuration is actually represented by valid ESP-IDF config.

### Risk analysis

Risk: low.

Possible downside:

- None expected if `CONFIG_HEAP_TRACING_OFF=y` is the valid ESP-IDF choice.

Mitigation:

- Verified by clean build.
- Keep diagnostic build instructions for re-enabling heap tracing.

### Decision status

Approved for review as part of Part A.

---

## A3. Gate TCP echo test service behind test macro

### Goal

Remove TCP echo test service from normal/minimal builds unless explicitly enabled.

### Purpose

TCP echo is a regression tool, not core ESP32-Lite ZeroTier runtime behavior.

It should be available for dataplane testing, but not consume memory or runtime resources in the normal build.

### Expected memory effect

Variable.

Potential savings include:

- task stack
- static state
- code/flash
- logging strings
- runtime heap/stack pressure

Exact isolated saving was not the primary Part A measurement, but it contributes to a cleaner normal build.

### Risk analysis

Risk: low.

Possible downside:

- TCP payload regression requires a test/regression build with the macro enabled.
- A normal build alone cannot serve as the TCP echo endpoint.

Mitigation:

- Keep the TCP echo implementation available behind a clear macro.
- Document how to enable the regression profile.
- Do not remove TCP dataplane capability itself.

### Decision status

Approved for review as part of Part A.

---

## A4. Gate autonomous UDP sender behind test macro

### Goal

Remove autonomous QEMU-to-zt-alpine UDP test sender from normal/minimal builds unless explicitly enabled.

### Purpose

The autonomous UDP sender is useful for proving ESP32/QEMU -> zt-alpine payload delivery, but it is not part of the ESP32-Lite production runtime.

### Expected memory effect

Variable.

Potential savings include:

- task stack
- static state
- code/flash
- logging strings
- runtime load

### Risk analysis

Risk: low.

Possible downside:

- UDP regression from QEMU to zt-alpine requires enabling the test macro.
- Test setup becomes slightly more explicit.

Mitigation:

- Keep the sender behind a test macro.
- Preserve regression build profile.

### Decision status

Approved for review as part of Part A.

---

## A5. Reduce `s_rx_stack`

### Goal

Reduce over-reserved static RX task stack while preserving safe runtime stack margin.

### Purpose

`s_rx_stack` was one of the largest static RAM consumers. Reducing it is a direct BSS saving.

### Attempt history

Initial aggressive attempt:

- `s_rx_stack = 24576`
- Result: too aggressive; `zt_rx` margin became too low.

Final candidate:

- `s_rx_stack = 28672`

Baseline:

- `s_rx_stack = 32768`

### Expected memory effect

About 4096 bytes BSS saved from this stack reduction alone.

### Risk analysis

Risk: low/medium.

Main risk:

- Stack overflow or insufficient `zt_rx` margin under heavy traffic or future feature additions.

Evidence:

- `24576` was too low.
- `28672` passed regression with good `zt_rx` HWM.
- Final `zt_rx` HWM was reported around 9684 bytes free.

Mitigation:

- Do not reduce below 28672 without new heavy stress evidence.
- Re-check HWM after future feature additions.
- Keep stack monitoring in diagnostic builds.

### Decision status

Approved for review at `28672`.

---

## A6. Increase/rebalance `zt_task` stack only with reclaimed memory

### Goal

Improve `zt_task` stack safety while still reducing net memory usage.

### Purpose

Earlier runtime evidence showed `zt_task` margin was a real concern. Increasing `zt_task` stack improves safety, but only makes sense if other Part A savings pay for it.

Final candidate:

- `zt_task = 73728`

### Expected memory effect

This change consumes memory, but Part A still produces net savings:

- DRAM delta: `-6564`
- `.bss` delta: `-6472`

### Risk analysis

Risk: low/medium.

Possible downside:

- Increasing `zt_task` reduces available static RAM if not balanced.
- Future optimizations may be tempted to shrink it again too aggressively.

Mitigation:

- Treat `zt_task = 73728` as the current safe candidate.
- Do not shrink until there is better stack-depth evidence.
- Preserve net memory improvement from Part A.

### Decision status

Approved for review as part of Part A.

---

## Part A final status

Part A is the first candidate to review and potentially commit.

Expected final state:

- `s_rx_stack = 28672`
- `zt_task = 73728`
- `s_tx_defer_q = 4`
- heap tracing off in normal build
- TCP echo gated
- UDP sender gated
- Part B changes not included
- NVS unchanged
- `/project` clean
- `/qemu-project` contains only intended Part A diff

### Part A recommendation

Review and commit Part A first, before attempting additional Part B optimizations.

---

# Part B: Experimental candidates

Part B contains potential future optimizations.

These are not approved for merge as part of Part A.

---

## B1. NetworkConfig URL fields `2048 -> 256`

### Goal

Reduce static `NetworkConfig` / `NIL_NC` memory usage by shrinking oversized URL/auth-related fields.

### Purpose

Current URL-related fields appear oversized for the ESP32-Lite use case, especially if the target network does not use SSO/auth/controller URL paths that require long strings.

### Estimated memory effect

Measured potential from Part B:

- about 5376 bytes BSS

This is the largest individual Part B BSS opportunity.

### Risk analysis

Risk: medium/high.

Possible failures:

- Live NetworkConfig parsing may truncate or lose fields.
- Future ZeroTier Central configuration may use longer URLs.
- SSO/auth/credential fields may break.
- Failure may not look like a clean crash; it may appear as missing config, missing routes, or controller weirdness.

Why this is risky:

- It changes semantic capacity, not just unused diagnostic memory.
- It can break future configurations even if current test network passes.

### Required verification before approval

1. Measure actual live NetworkConfig URL/auth/credential field lengths.
2. Confirm current network does not use SSO/auth URL fields.
3. Build with reduced field size.
4. Verify:
   - NetworkConfig received
   - managed IP applied
   - managed route applied
   - multicast subscription works
   - direct path established
   - ZT ARP works
   - ICMP/TCP/UDP dataplane smoke passes
5. Run at least short regression.
6. Document safety margin.

### Decision status

High potential, but not approved now.

Recommended as a separate future step after Part A.

---

## B2. `ZT_NETWORKCONFIG_DICT_CAPACITY_OVERRIDE 4096 -> 2048`

### Goal

Reduce NetworkConfig dictionary/parser buffer footprint.

### Purpose

If actual serialized/parsed NetworkConfig size is comfortably below 2048, this could reduce memory without changing higher-level behavior.

### Estimated memory effect

Unknown.

Best guess:

- maybe 1-2 KB

But cc-step126 did not correctly measure this change.

### Risk analysis

Risk: high.

Possible failures:

- NetworkConfig parse failure.
- Missing managed IP.
- Missing managed route.
- Missing rules or capabilities.
- Node may appear partially functional but not actually apply config correctly.

Why this is risky:

- Too-small dictionary capacity can create subtle control-plane/config failures.
- It may be confused with controller, auth, identity, or network problems.

### Required verification before approval

1. Log actual serialized NetworkConfig size.
2. Log parsed dictionary usage.
3. Establish maximum observed size and safety margin.
4. Only then test 2048.
5. Verify live config and dataplane.
6. Keep 4096 unless 2048 has proven margin.

### Decision status

Not approved.

Needs a measurement-only step first.

---

## B3. lwIP TCP/socket/DHCPS tuning

### Goal

Reduce ESP-IDF/lwIP static and runtime memory pools.

### Purpose

ESP32-Lite likely does not need the default lwIP resource sizes intended for more general networking use.

Possible targets:

- DHCPS
- socket counts
- TCP PCB counts
- TCP send/receive buffers
- TCP window sizes
- mailboxes
- pbuf pools

### Estimated memory effect

Best guess:

- 1-5 KB

cc-step126 B3 combined several settings, so individual effects are not isolated.

### Risk analysis

Risk: medium/high.

Possible failures:

- TCP echo regression.
- Sporadic TCP timeout.
- pbuf exhaustion.
- mailbox exhaustion.
- socket allocation failure.
- issues only visible under load or burst.

Why this is risky:

- TCP currently works.
- lwIP failures can be intermittent and hard to attribute.
- Combining several lwIP changes hides which setting caused the failure.

### Required split tests

Run separate substeps:

- B3a: DHCPS disable only
- B3b: TCP/socket/PCB count reduction only
- B3c: TCP window/SND_BUF/mailbox tuning only

### Required verification

Each substep must capture:

- build-size delta
- ICMP smoke
- TCP echo regression
- UDP regression
- heap/pbuf/mailbox failure logs
- no increase in packet loss under moderate traffic
- no direct-path regression

### Decision status

Future split step.

Do not merge as one combined patch.

---

## B4. Peer/path limit changes

### Goal

Reduce ZeroTier peer/path memory.

### Purpose

ESP32-Lite should need fewer peers and paths than a full Linux ZeroTierOne client.

This may reduce runtime heap or control-block memory.

### Estimated memory effect

Unknown.

Likely more runtime heap than static BSS.

### Risk analysis

Risk: medium/high.

Possible failures:

- direct path not established
- relay fallback broken
- peer eviction during traffic
- reconnect instability
- worse roaming/NAT traversal
- intermittent dataplane loss

Why this is risky:

- Direct path stability is a core requirement.
- Peer/path discovery was historically a major failure area.
- Lower limits can appear fine in a short test but fail during reconnect or longer soak.

### Required verification before approval

1. Measure peak peer/path counts during:
   - normal traffic
   - heavy traffic
   - reconnect
   - long soak
2. Lower one limit at a time.
3. Verify direct path remains stable.
4. Verify relay fallback still works if needed.
5. Verify no peer/path eviction during traffic.
6. Run reconnect/warmup tests.

### Decision status

Defer.

Do not prioritize before B5/B1/lwIP split tests.

---

## B5. `s_tx_defer_q 4 -> 2`

### Goal

Reduce static TX defer queue memory.

### Purpose

Current TX defer queue depth may be larger than needed for ESP32-Lite. Reducing it can save a clear amount of BSS.

### Measured memory effect

Measured Part B potential:

- about 3080 bytes BSS

This is the best next Part B candidate by risk/reward.

### Risk analysis

Risk: medium.

Possible failures:

- queue full during burst
- dropped frames during ARP/ICMP/TCP/UDP overlap
- dropped frames during reconnect
- TCP instability during short TX bursts
- intermittent dataplane failures not visible in simple ping

Why this is manageable:

- It is local and measurable.
- It does not change semantic NetworkConfig parsing.
- It can be tested with targeted burst traffic.

### Required verification before approval

1. Build with `s_tx_defer_q = 2`.
2. Run burst regression, not just normal traffic.
3. Test simultaneous:
   - ZT ARP
   - ICMP
   - TCP echo
   - UDP both directions
4. Capture:
   - queue-full counters
   - TX drops
   - heap min/largest
   - stack HWM
   - direct path status
   - TCP success rate
   - UDP packet count
5. Compare against queue depth 4.

### Decision status

Best next separate Part B step.

---

# Recommended execution order

## Step 1: Review and commit Part A

Review final Part A diff and commit only if accepted.

Expected commit scope:

- diagnostic heap tracing off/gated
- valid heap tracing config symbol
- TCP echo gated
- UDP sender gated
- `s_rx_stack = 28672`
- `zt_task = 73728`
- no Part B changes

## Step 2: B5 burst-test

Run a dedicated step for:

- `s_tx_defer_q 4 -> 2`

Reason:

- best risk/reward
- about 3 KB BSS possible saving
- local and measurable
- requires burst evidence before approval

## Step 3: B1 live NetworkConfig proof

Run a dedicated step for:

- NetworkConfig URL fields `2048 -> 256`

Reason:

- largest remaining single BSS saving
- requires proof that current and expected configs do not need longer fields

## Step 4: B3 split lwIP tuning

Run separate substeps:

- DHCPS only
- socket/PCB only
- TCP window/mailbox only

Reason:

- likely useful but only if isolated

## Step 5: B2 measurement-only

Measure actual NetworkConfig dictionary usage before changing capacity.

Reason:

- current `4096 -> 2048` result was not valid
- risk is high if changed blindly

## Step 6: B4 peer/path limits

Only after peak peer/path data exists.

Reason:

- direct path and reconnect stability matter more than speculative heap savings

---

# Do not do yet

Do not merge:

- NetworkConfig URL `2048 -> 256`
- dictionary `4096 -> 2048`
- lwIP combined tuning
- peer/path limit changes
- `s_tx_defer_q 4 -> 2`

Do not change:

- canonical NVS
- NetworkConfig persistence policy
- controller config
- sdwan topology
- NAT/MASQUERADE policy

---
