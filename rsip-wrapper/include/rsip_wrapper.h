#ifndef RSIP_WRAPPER_H
#define RSIP_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize internal structures. Call before other APIs.
bool rsip_init(void);

// Set a callback to receive events from the Rust side. The callback is called
// synchronously from the Rust listener thread. The strings are valid only for
// the duration of the callback and will be freed after the call returns.
void rsip_set_event_callback(void (*cb)(const char* event, const char* payload));
void rsip_clear_event_callback(void);

// Start a UDP listener on the given port. Received datagrams trigger the
// registered callback with event="sip_rx" and payload being the raw SIP text.
bool rsip_start_udp_listener(uint16_t port);

// Send a raw UDP datagram to dest_ip:dest_port with data being a C string.
bool rsip_send_udp(const char* dest_ip, uint16_t dest_port, const char* data);

// Shutdown listener and clean up.
void rsip_shutdown(void);

// Return an informational static string (leaked pointer) for testing linkage.
const char* rsip_version(void);

#ifdef __cplusplus
}
#endif

#endif // RSIP_WRAPPER_H
