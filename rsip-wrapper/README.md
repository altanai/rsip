# rsip-wrapper: transport+UA hybrid (FFI) implementation

This document describes the minimal hybrid approach implemented in this crate: a small Rust "transport & UA" wrapper around `rsip` that exposes a compact C API for integration with FreeSWITCH (or other C hosts).

![diagrm](mermaid-diagram-2025-11-11-120048.png)

## Goals

- Provide a small, safe C ABI surface so a host (FreeSWITCH module) can receive SIP messages from Rust and instruct Rust to send SIP messages.
- Keep the Rust side responsible for networking & protocol parsing. Minimize the FFI surface and make callbacks simple.

## What this prototype does

- Builds a cdylib with a C API.
- Implements a UDP listener that receives raw SIP datagrams and invokes a registered callback with event="sip_rx" and payload containing the raw SIP text.
- Exposes helper functions: init, set/clear callback, start UDP listener, send UDP datagram, shutdown, and a small version string.

## Files added

- `Cargo.toml` - crate manifest (cdylib crate-type).
- `src/lib.rs` - Rust implementation of the FFI API.
- `include/rsip_wrapper.h` - C header describing the API.
- `mod_rsip_example/mod_rsip.c` - small example program that registers a callback and listens on UDP/5060.

## Design notes and safety

- The callback has signature `void(*cb)(const char* event, const char* payload)` and is called synchronously from the Rust listener thread. The strings are only valid for the duration of the callback; the callee must copy them if it needs to persist the data.
- We use `lazy_static`-backed `Mutex` and an `AtomicBool` to store the callback, the thread handle, and a running flag.
- We intentionally keep the API small to reduce cross-language ownership complexity.
- The Rust side currently performs no full SIP transaction or dialog management — it only receives raw SIP datagrams and forwards them. `rsip` (the dependency) can be used inside the listener to parse/validate messages if you extend the implementation.

## Integration with FreeSWITCH (next steps)

1. In-process approach (advanced): write a FreeSWITCH module `mod_rsip.c` that dynamically loads the `rsip-wrapper` DLL (or links against it) and registers a callback. The module should translate events into FS session actions (create session, set remote SDP, answer, bridge). Ensure thread-safety: many FS APIs must be called from FS worker threads or using FS-provided async mechanisms.
2. Hybrid (recommended incremental): run the `rsip-wrapper` as an external process or simple native binary and communicate via network (SIP) or FSMQ/ESL. Use FreeSWITCH `sofia` profiles to talk to your process as a gateway.

## Build notes (Windows PowerShell examples)

1. Build the Rust cdylib (MSVC toolchain recommended if FreeSWITCH is built with MSVC):

   cd C:\Users\altan\Downloads\rsip\rsip-wrapper
   cargo build --release

2. The produced dynamic library will be at `target\release\rsip_wrapper.dll` (name may vary depending on platform). Use `cbindgen` or the provided header `include/rsip_wrapper.h` to include definitions in C code.

3. Example: compile the example shim (adjust to your compiler):

   # If using MSVC: cl.exe /EHsc mod_rsip.c /I..\include

   # If using gcc: gcc mod_rsip.c -I../include -o mod_rsip_example.exe -L../target/release -lrsip_wrapper

   Note: linking against the produced rsip_wrapper library on Windows may require generating an import library or loading the DLL dynamically.

## Limitations & next steps

- The prototype only handles UDP datagrams; you should add TCP/TLS/WS transports for production SIP.
- Add proper SIP transaction, dialog, and timer handling (retransmits, forking, PRACK, etc.) by implementing those layers on top of `rsip` parsing.
- For in-process modules, design a small, robust event model that allows the FS module to ask Rust to perform actions synchronously or asynchronously. Carefully manage the async runtime lifecycle (spawn a dedicated runtime thread inside Rust and do not block FS threads).
- Use `cbindgen` to generate headers automatically and include tests that validate FFI linkage.

## Contact & follow-up

I can flesh out a `mod_rsip.c` FreeSWITCH module example that calls `switch_core_session_*` APIs and maps events into FS sessions if you want to proceed with an in-process integration. I can also extend `rsip-wrapper` to parse SIP via `rsip::message` and expose higher-level events (INVITE, BYE, REGISTER) rather than raw SIP strings.
