/*
 * Example FreeSWITCH module skeleton showing how to marshal callbacks from
 * the Rust 
sip-wrapper onto FreeSWITCH worker threads.
 *
 * This is a self-contained illustration (not a complete module). It demonstrates:
 * - registering a C callback with the Rust wrapper
 * - copying event payloads onto a FreeSWITCH queue
 * - running a FreeSWITCH worker thread that pops queued events and
 *   processes them inside the FS context (e.g. creating events, logging,
 *   creating sessions).
 *
 * Adapt paths/includes and function names to match your FreeSWITCH build
 * environment (headers and link flags). The important parts are the queue
 * and worker pattern.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FreeSWITCH public headers (adjust include path as needed in real module) */
#include <switch.h>

#include "../include/rsip_wrapper.h"

/* Simple event container copied from the Rust thread into FS land. */
typedef struct rsip_event_item {
    char *event;   /* event type/name */
    char *payload; /* event payload (SIP text or JSON) */
} rsip_event_item_t;

/* Module-global queue and worker thread */
static switch_queue_t *rsip_event_queue = NULL;
static switch_thread_t *rsip_worker_thread = NULL;
static switch_memory_pool_t *rsip_module_pool = NULL;

/* Worker thread: pull events from queue and handle them inside FreeSWITCH 
Extending behavior: Inside rsip_worker_run you can:
create sessions with switch_core_session_* APIs (if you need to create a call),
lookup or route to dialplans,
forward SIP payloads into a session or record them,
or translate the SIP payload into FS events that other modules handle.
*/
static void *rsip_worker_run(switch_thread_t *thread, void *obj)
{
    rsip_event_item_t *item = NULL;

    while (switch_queue_trypop(rsip_event_queue, (void **)&item) == SWITCH_STATUS_SUCCESS) {
        if (!item) {
            /* NULL is used as a shutdown sentinel */
            break;
        }

        /* Example: create and fire a custom FS event. This is safe to call
         * from a worker thread. Adjust event type and headers as needed. */
        switch_event_t *event = NULL;
        if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "rsip-event", item->event);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "rsip-payload", item->payload);
            /* Optionally set a subclass so listeners can filter: */
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "subclass", "rsip::sip_rx");

            /* Fire the event into FreeSWITCH's event system */
            switch_event_fire(&event);
        }

        /* Example: log the event (use FS logging) */
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[mod_rsip] handled event=%s payload_len=%zu\\n",
                          item->event, item->payload ? strlen(item->payload) : 0);

        /* Free the item memory */
        free(item->event);
        free(item->payload);
        free(item);
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "[mod_rsip] worker exiting\\n");
    return NULL;
}

/* Rust -> C callback. This runs on the Rust listener thread. Keep it minimal and
 * non-blocking: copy data and push to a FreeSWITCH queue for deferred processing. */
void rsip_cb(const char* event, const char* payload)
{
    if (!rsip_event_queue) {
        /* Queue not ready; drop the event (or buffer elsewhere). */
        return;
    }

    rsip_event_item_t *it = malloc(sizeof(*it));
    if (!it) {
        return;
    }

#ifdef _MSC_VER
    it->event = event ? _strdup(event) : NULL;    /* _strdup on Windows */
    it->payload = payload ? _strdup(payload) : NULL;
#else
    it->event = event ? strdup(event) : NULL;
    it->payload = payload ? strdup(payload) : NULL;
#endif

    if (!it->event && !it->payload) {
        free(it);
        return;
    }

    /* Try to push to the queue without blocking the Rust thread. If push fails,
     * free the item and drop the event to avoid blocking. */
    if (switch_queue_trypush(rsip_event_queue, it) != SWITCH_STATUS_SUCCESS) {
        free(it->event);
        free(it->payload);
        free(it);
    }
}

/* Module load/init routine (skeleton). In a real FreeSWITCH module this would
 * be called from the module's load function. */
static switch_status_t mod_rsip_init(switch_memory_pool_t *pool)
{
    switch_threadattr_t *thd_attr = NULL;

    rsip_module_pool = pool;

    /* Create a queue sized for expected burst rate. The pool passed here
     * associates queue allocations with the module memory pool. */
    if (switch_queue_create(&rsip_event_queue, 1024, rsip_module_pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[mod_rsip] switch_queue_create failed\\n");
        return SWITCH_STATUS_FALSE;
    }

    /* Launch a dedicated worker thread to process queued events */
    switch_threadattr_create(&thd_attr, rsip_module_pool);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    if (switch_thread_create(&rsip_worker_thread, thd_attr, rsip_worker_run, NULL, rsip_module_pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[mod_rsip] switch_thread_create failed\\n");
        switch_queue_term(rsip_event_queue);
        rsip_event_queue = NULL;
        return SWITCH_STATUS_FALSE;
    }

    /* Register callback with Rust wrapper. Keep Rust callbacks simple and non-blocking. */
    if (!rsip_init()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[mod_rsip] rsip_init failed\\n");
        return SWITCH_STATUS_FALSE;
    }

    rsip_set_event_callback(rsip_cb);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[mod_rsip] initialized\\n");
    return SWITCH_STATUS_SUCCESS;
}

/* Module shutdown/teardown routine (skeleton). */
static void mod_rsip_shutdown(void)
{
    /* Unregister callback so Rust no longer invokes rsip_cb */
    rsip_clear_event_callback();

    /* Signal worker thread to exit by pushing a NULL sentinel */
    if (rsip_event_queue) {
        switch_queue_push(rsip_event_queue, NULL);
    }

    /* Optionally wait a short time here for worker to exit, or rely on FS to
     * clean up thread pools assigned to the module pool on unload. */

    /* Shutdown the Rust side */
    rsip_shutdown();

    /* Terminate the queue explicitly */
    if (rsip_event_queue) {
        switch_queue_term(rsip_event_queue);
        rsip_event_queue = NULL;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "[mod_rsip] shutdown complete\\n");
}

/* Simple main to demonstrate running outside of the FS module loader. This
 * mirrors the earlier example but shows how init/shutdown hooks are used. */
int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    if (mod_rsip_init(NULL) != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "mod_rsip_init failed\\n");
        return 1;
    }

    /* Example: start the Rust UDP listener on port 5060 */
    if (!rsip_start_udp_listener(5060)) {
        fprintf(stderr, "rsip_start_udp_listener failed\\n");
        mod_rsip_shutdown();
        return 1;
    }

    printf("mod_rsip example running. Press Enter to shutdown...\\n");
    getchar();

    mod_rsip_shutdown();
    return 0;
}
