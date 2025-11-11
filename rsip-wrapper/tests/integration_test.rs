// Integration test for rsip-wrapper FFI API
// Tests real FFI linking and basic functionality

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use std::net::UdpSocket;

// FFI declarations (would normally be in a generated header)
extern "C" {
    fn rsip_init() -> bool;
    fn rsip_set_event_callback(cb: extern "C" fn(event: *const c_char, payload: *const c_char));
    fn rsip_clear_event_callback();
    fn rsip_start_udp_listener(port: u16) -> bool;
    fn rsip_send_udp(dest_ip: *const c_char, dest_port: u16, data: *const c_char) -> bool;
    fn rsip_shutdown();
    fn rsip_version() -> *const c_char;
}

#[test]
fn test_ffi_version_linkage() {
    unsafe {
        let ptr = rsip_version();
        assert!(!ptr.is_null(), "version pointer should not be null");
        let cstr = CStr::from_ptr(ptr);
        let version = cstr.to_str().expect("version should be UTF-8");
        assert!(!version.is_empty(), "version should not be empty");
        println!("Linked version: {}", version);
    }
}

#[test]
fn test_ffi_init_and_shutdown() {
    unsafe {
        let init_result = rsip_init();
        assert!(init_result, "rsip_init should succeed");
        
        rsip_shutdown();
        // Shutdown should not crash
    }
}

#[test]
fn test_ffi_callback_registration() {
    extern "C" fn test_callback(event: *const c_char, payload: *const c_char) {
        println!("callback invoked: event={:?}, payload_ptr={:?}", event, payload);
    }

    unsafe {
        rsip_init();
        rsip_set_event_callback(test_callback);
        thread::sleep(Duration::from_millis(50));
        rsip_clear_event_callback();
        rsip_shutdown();
    }
}

#[test]
fn test_ffi_send_udp() {
    unsafe {
        rsip_init();

        let dest_ip = CString::new("127.0.0.1").expect("dest_ip should be valid");
        let data = CString::new("INVITE sip:user@example.com SIP/2.0\r\n").expect("data should be valid");

        let result = rsip_send_udp(dest_ip.as_ptr(), 5060, data.as_ptr());
        println!("rsip_send_udp returned: {}", result);
        // We expect this to succeed (at least attempt the send)

        rsip_shutdown();
    }
}

#[test]
fn test_ffi_listener_lifecycle() {
    unsafe {
        rsip_init();

        // Register a callback to count events
        let event_count = Arc::new(AtomicBool::new(false));
        let event_count_clone = event_count.clone();

        extern "C" fn capture_callback(event: *const c_char, payload: *const c_char) {
            unsafe {
                let ev = CStr::from_ptr(event).to_str().unwrap_or("");
                let pl = CStr::from_ptr(payload).to_str().unwrap_or("");
                println!("capture_callback: event={}, payload_len={}", ev, pl.len());
            }
        }

        rsip_set_event_callback(capture_callback);

        // Start listener on a high port to avoid conflicts
        let listener_result = rsip_start_udp_listener(15060);
        assert!(listener_result, "rsip_start_udp_listener should succeed");
        println!("Listener started on port 15060");

        // Give listener time to start
        thread::sleep(Duration::from_millis(100));

        // Send a test SIP message to ourselves
        let test_message = "INVITE sip:test@localhost SIP/2.0\r\nVia: SIP/2.0/UDP 127.0.0.1\r\n\r\n";
        match UdpSocket::bind("0.0.0.0:0") {
            Ok(client_socket) => {
                match client_socket.send_to(test_message.as_bytes(), "127.0.0.1:15060") {
                    Ok(n) => println!("Sent {} bytes to listener", n),
                    Err(e) => println!("Send failed: {}", e),
                }
            }
            Err(e) => println!("Failed to bind client socket: {}", e),
        }

        // Give callback time to be invoked
        thread::sleep(Duration::from_millis(200));

        rsip_shutdown();
        println!("Listener shutdown complete");
    }
}

#[test]
fn test_ffi_multiple_lifecycle() {
    unsafe {
        for i in 0..3 {
            println!("Iteration {}", i);
            rsip_init();
            rsip_shutdown();
            thread::sleep(Duration::from_millis(50));
        }
    }
}
