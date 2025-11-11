/*
 * Standalone test program that uses the rsip-wrapper C API to start the
 * UDP listener, register a callback, send a test SIP message via
 * rsip_send_udp and then shutdown.
 *
 * Build: link with the produced rsip-wrapper library (cdylib) from the
 * crate (platform-specific). This file is intentionally simple and
 * intended for quick local testing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/rsip_wrapper.h"

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/*  standalone test program that:
Calls rsip_init(), rsip_set_event_callback()
Starts the UDP listener (rsip_start_udp_listener(15060))
Sends a sample SIP INVITE to localhost via rsip_send_udp(...)
Waits briefly and calls rsip_shutdown()

Running this 

cargo build --manifest-path Cargo.toml --release

*/

/* Simple callback invoked by Rust wrapper */
void test_rsip_cb(const char* event, const char* payload) {
    printf("[test_mod_rsip] callback event=%s payload_len=%zu\n", event ? event : "(null)", payload ? strlen(payload) : 0);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!rsip_init()) {
        fprintf(stderr, "rsip_init failed\n");
        return 1;
    }

    rsip_set_event_callback(test_rsip_cb);

    const uint16_t port = 15060;
    if (!rsip_start_udp_listener(port)) {
        fprintf(stderr, "rsip_start_udp_listener(%u) failed\n", (unsigned)port);
        rsip_shutdown();
        return 1;
    }

    /* Wait briefly for listener to start */
    SLEEP_MS(100);

    const char *dest = "127.0.0.1";
    const char *msg = "INVITE sip:test@localhost SIP/2.0\r\nVia: SIP/2.0/UDP 127.0.0.1\r\n\r\n";

    if (!rsip_send_udp(dest, port, msg)) {
        fprintf(stderr, "rsip_send_udp failed\n");
    } else {
        printf("rsip_send_udp succeeded\n");
    }

    /* Give callback some time to be invoked */
    SLEEP_MS(250);

    rsip_clear_event_callback();
    rsip_shutdown();

    printf("test_mod_rsip: done\n");
    return 0;
}
