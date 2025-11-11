// Example C shim showing how a FreeSWITCH module could interact with the rsip-wrapper
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/rsip_wrapper.h"

// callback invoked by Rust
void rsip_cb(const char* event, const char* payload) {
    printf("rsip_cb: event=%s payload_len=%zu\n", event, strlen(payload));
    // In a real FreeSWITCH module, map events to FS APIs here, e.g. create session,
    // set remote SDP, or answer/bridge calls.
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!rsip_init()) {
        fprintf(stderr, "rsip_init failed\n");
        return 1;
    }

    rsip_set_event_callback(rsip_cb);

    if (!rsip_start_udp_listener(5060)) {
        fprintf(stderr, "rsip_start_udp_listener failed\n");
        return 1;
    }

    printf("Listening on UDP/5060. Press Enter to shutdown...\n");
    getchar();

    rsip_shutdown();
    return 0;
}
