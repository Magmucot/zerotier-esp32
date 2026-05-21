# Best practice - ESP-IDF and ESP32 Memory Analysis and Debugging 

---

## Question 1

When developing code with ESP-IDF for ESP32, are there any tools that can help
analyze heap/stack requirements and the size of static variables, plus smart and
general alloc-guards for C++ that can be used to monitor new and similar? Is there
any support in QEMU for the equivalent at runtime? Look at how this is done in
various open-source projects and look for best practices. Is there anything else
to consider regarding this?

---

## Answer 1

### Static Memory Size (build-time)

`idf.py size` / `idf.py size-components` / `idf.py size-files` analyzes static
memory usage per component and source file. If static memory exceeds the limits,
the linker fails with a clear error message such as `DRAM segment data does not fit`.

Free heap size at startup is always lower than what `idf.py size` shows, because
Wi-Fi, TCP/IP, etc. allocate dynamically immediately at startup.

### Stack Analysis and Protection (runtime)

Three mechanisms exist in ESP-IDF:

**Hardware Stack Guard** (`CONFIG_ESP_SYSTEM_HW_STACK_GUARD`) is the recommended
and default-enabled method. It uses the Debug Assistant module to monitor the stack
pointer and triggers a panic immediately on overflow.

**Stack Canary Bytes** (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW`) places magic bytes at
the end of each task's stack and checks them on every context switch. Can miss
overflow if the stack pointer jumps past the canary.

`uxTaskGetStackHighWaterMark()` returns the minimum free stack space during the
task's lifetime, giving a good indication of margins.

**Stack smashing protection** (`CONFIG_COMPILER_STACK_CHECK_MODE`) is based on GCC's
`-fstack-protector*` flags and protects against buffer overflow on the stack.

### Heap Debugging (runtime)

ESP-IDF provides `heap_caps_get_free_size()`, `heap_caps_get_largest_free_block()`
and `heap_caps_get_minimum_free_size()` (low watermark since boot) for monitoring
heap state. `heap_caps_get_info()` also provides the number of active allocations.

**Heap poisoning** in "Comprehensive" mode fills allocated memory with `0xCE` and
freed memory with `0xFE`. If the app crashes on an address related to `0xCECECECE`
it has read uninitialized memory; `0xFEFEFEFE` indicates use-after-free. Performance
is noticeably affected and should only be enabled during debugging.

**Heap tracing** (`esp_heap_trace.h`) traces all malloc/free with call stack. There
is a standalone mode (buffer on chip) and host-based mode via JTAG/OpenOCD/SystemView
without size limitation.

`esp_alloc_failed_hook_t` is a callback invoked on every failed allocation, useful
as a simple alloc-guard.

### C++ `new` Guard

ESP-IDF's newlib port makes C++ `new` call `heap_caps_malloc_default()` under the
hood. This means:

- `esp_alloc_failed_hook_t` catches a failed `new` just like a failed `malloc`.
- Heap poisoning and heap trace also work for `new`/`delete`.
- To set a global `new` handler, standard C++ `std::set_new_handler()` works in
  ESP-IDF.
- Best practice in open source ESP32 projects (e.g. ESPHome, Tasmota) is to avoid
  `new` entirely in hot paths and use static allocation or `heap_caps_malloc` with
  explicit error checks.

### QEMU Support

Espressif's QEMU fork supports being built with `--enable-sanitizers` on the host
side. GDB over the JTAG emulator works for breakpoints and stepping.

However: QEMU does not reliably emulate ESP32's Hardware Stack Guard hardware
(unknown/unverified). Heap poisoning and heap tracing work normally in QEMU since
they are pure software mechanisms. GCC AddressSanitizer (ASan) does not work directly
on the Xtensa/ESP32 target.

**UBSAN** (`-fsanitize=undefined`) is available in ESP-IDF and catches e.g. signed
integer overflow and invalid shifts at runtime. It significantly increases code and
data size and most applications will not fit in RAM with UBSAN enabled for the entire
project. Recommended to enable per component.

### Other Considerations

**Heap fragmentation** is a serious problem on ESP32. The heap measurement APIs
expose `largest_free_block` making it possible to monitor fragmentation continuously.

**FreeRTOS task stacks are allocated from heap** and should use `xTaskCreateStatic()`
for critical tasks to eliminate allocation at runtime.

### Recommended Debug Setup Summary

| Phase          | Tool                                                              |
|----------------|-------------------------------------------------------------------|
| Build-time     | `idf.py size-files`                                              |
| Stack overflow | `CONFIG_ESP_SYSTEM_HW_STACK_GUARD` + `uxTaskGetStackHighWaterMark` |
| Heap corruption | Heap poisoning Comprehensive + `heap_caps_check_integrity_all()` |
| Leaks          | `heap_trace` standalone or host-based                             |
| Alloc failure  | `heap_caps_register_failed_alloc_callback()`                      |
| UB             | UBSAN per component                                               |
| QEMU           | GDB + breakpoints; heap mechanisms work normally                  |

---

## Question 2

Provide step-by-step suggestions on how to use these tools in the best way
("best practice"):

1. Before porting code to ESP32 begins, including code analysis of C/C++ code
   (or .elf) to locate and address initial "memory hogs" (huge global variables etc).
2. While coding and debugging with QEMU.
3. At runtime on real hardware (ESP32).

Are there any good tools and processes for tracing/intercepting egress/ingress
network traffic in QEMU?

---

## Answer 2

### Phase 1: Before Porting Begins (Static Analysis of Source Code / ELF)

#### Source Code Analysis

**Goal:** Find global/static variables and buffers too large for ESP32's DRAM
(~320 KB total of which ~180 KB is typically available).

```bash
grep -rn "static\|global" src/ | grep -E "\[([0-9]{4,})\]"
cppcheck --enable=all src/
```

For C++ code: specifically look for static class objects with heavy constructors
(`static MyClass obj;`) since these run in `.init_array` before `app_main` and
consume heap early.

#### ELF Analysis (after first compilation)

`elf-size-analyze` (GitHub: `jedrzejboczar/elf-size-analyze`) is a Python tool that
uses `readelf`, `nm` and `c++filt` to present symbol sizes in tree format per source
file.

```bash
# Sort global/static symbols by size
xtensa-esp32-elf-nm --size-sort --print-size -td build/myapp.elf \
  | grep -E " [bBdD] " | tail -30
```

`idf.py size-files` gives contribution per object file broken down into DRAM
(.data + .bss), IRAM and Flash. If DRAM is full the linker fails with
`DRAM segment data does not fit`.

Note that `xtensa-esp32-elf-size libcomponent.a` shows the entire archive content
while `idf.py size` only shows what is actually linked into the final binary.

**Things to look for:**
- Global arrays/buffers >1 KB in `.bss`/`.data`: candidates for dynamic allocation
  or PSRAM
- Static C++ objects with non-trivial constructors: replace with lazy init
- `const` data ending up in `.data` instead of `.rodata`: add `const` and
  `DRAM_ATTR` correctly

### Phase 2: While Coding and Debugging in QEMU

#### Memory Protection (enable in `menuconfig`)
