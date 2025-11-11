# Testing Guide for rsip-wrapper

This document describes the unit and integration tests available for the `rsip-wrapper` crate, and how to run them locally.

## Test Structure

### Unit Tests (in `src/lib.rs`)

The unit tests cover the core FFI API and internal state management:

- `test_rsip_init()` — Verifies `rsip_init()` initializes state correctly.
- `test_rsip_version()` — Tests the `rsip_version()` helper function returns the correct version string.
- `test_callback_registration()` — Validates callback registration, clearing, and state.
- `test_udp_send_with_null_pointers()` — Ensures `rsip_send_udp()` rejects null pointers safely.
- `test_udp_send_invalid_address()` — Tests behavior with invalid IP addresses.
- `test_listener_already_running()` — Verifies that starting a listener twice fails (prevents races).
- `test_shutdown_clears_state()` — Confirms `rsip_shutdown()` cleanly resets all state.

### Integration Tests (in `tests/integration_test.rs`)

The integration tests validate the complete FFI linkage and runtime behavior:

- `test_ffi_version_linkage()` — Confirms the library links correctly and exports the version symbol.
- `test_ffi_init_and_shutdown()` — Tests FFI init/shutdown lifecycle across the C boundary.
- `test_ffi_callback_registration()` — Validates callback registration from C side.
- `test_ffi_send_udp()` — Tests the `rsip_send_udp()` FFI function with a real UDP send.
- `test_ffi_listener_lifecycle()` — Starts a listener on port 15060, sends a test SIP message, and verifies the listener receives it and invokes the callback.
- `test_ffi_multiple_lifecycle()` — Stress-tests multiple init/shutdown cycles to ensure no resource leaks.

## Running Tests Locally

### Prerequisites

- Rust toolchain (install from https://rustup.rs/)
- On Windows, MSVC or GNU toolchain (MSVC recommended if you're building against MSVC libraries)

### Build the crate

```powershell
cd C:\Users\altan\Downloads\rsip\rsip-wrapper
cargo build
```

This produces `target\debug\rsip_wrapper.dll` (or `.a` / `.so` depending on your platform).

### Run unit tests

```powershell
cargo test --lib
```

Example output:
```
running 7 tests
test tests::test_rsip_init ... ok
test tests::test_rsip_version ... ok
test tests::test_callback_registration ... ok
test tests::test_udp_send_with_null_pointers ... ok
test tests::test_udp_send_invalid_address ... ok
test tests::test_listener_already_running ... ok
test tests::test_shutdown_clears_state ... ok

test result: ok. 7 passed
```

### Run integration tests

```powershell
cargo test --test integration_test
```

This runs the FFI linkage tests. Note: the integration tests use `extern "C"` to declare the FFI functions, so Cargo must link against the compiled cdylib. Rust's `cargo test` automatically links the library for integration tests.

Example output (with some listener/network delays):
```
running 6 tests
test test_ffi_version_linkage ... ok
test test_ffi_init_and_shutdown ... ok
test test_ffi_callback_registration ... ok
test test_ffi_send_udp ... ok
test test_ffi_listener_lifecycle ... ok (may take a few hundred milliseconds)
test test_ffi_multiple_lifecycle ... ok

test result: ok. 6 passed
```

### Run all tests

```powershell
cargo test
```

This runs both unit and integration tests in sequence.

### Run with output

To see println! output from tests (useful for debugging):

```powershell
cargo test -- --nocapture
```

### Run a specific test

```powershell
cargo test test_ffi_listener_lifecycle -- --nocapture
```

## Test Expectations

### What the tests validate

1. **API correctness**: init, set/clear callback, send, shutdown behave as documented.
2. **Thread safety**: the listener can be started and stopped cleanly; multiple cycles don't leak state.
3. **FFI safety**: null pointer checks, CString conversions, and callback invocations don't crash.
4. **UDP transport**: datagrams are sent and received correctly; callbacks are invoked when data arrives.

### Known limitations

- Tests use localhost (127.0.0.1) and high ports (15060+) to avoid conflicts with running services.
- The listener test (`test_ffi_listener_lifecycle`) sends a raw SIP-like string; the current implementation does not parse it with `rsip`, only forwards it to the callback.
- On slow systems or under high load, timing-sensitive tests may occasionally flake. Increase sleep durations in the test if needed.

## Next Steps for Production Testing

1. **Extend rsip parsing**: add tests that verify SIP message parsing with `rsip::message` inside the listener.
2. **Add transport variants**: test TCP, TLS, and WebSocket transports.
3. **Add transaction tests**: verify that retransmit timers, INVITE/ACK flow, and dialog state are handled correctly.
4. **Add benchmarks**: measure throughput and latency with high-volume SIP message injection.
5. **Add C/FFI tests**: write C or C++ tests that link the library dynamically and test from that side (good for validating compatibility with FreeSWITCH modules).

## Troubleshooting

### `cargo test` fails with "cannot find library"

Ensure the crate is built first:
```powershell
cargo build
cargo test
```

### `test_ffi_listener_lifecycle` times out or hangs

This may happen if port 15060 is already in use. Try:
- Changing the port number in the test.
- Checking if another service is listening: `netstat -an | findstr 15060`

### Tests panic with "thread 'test-...' panicked"

Check the panic message carefully. Common issues:
- Null pointer access in FFI functions.
- CString validation failure (non-UTF8 strings).
- Listener thread not starting (port already in use).

## Continuous Integration

For CI/CD pipelines (GitHub Actions, Azure Pipelines, etc.), add a step:

```yaml
- name: Run tests
  run: |
    cd rsip-wrapper
    cargo test --lib
    cargo test --test integration_test
```

This will catch regressions early.

