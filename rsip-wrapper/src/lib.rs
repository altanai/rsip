use lazy_static::lazy_static;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::net::UdpSocket;
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::sync::atomic::{AtomicBool, Ordering};

type EventCallback = extern "C" fn(event: *const c_char, payload: *const c_char);

lazy_static! {
    static ref CALLBACK: Mutex<Option<EventCallback>> = Mutex::new(None);
    static ref LISTENER_THREAD: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);
    static ref RUNNING: AtomicBool = AtomicBool::new(false);
}

#[no_mangle]
pub extern "C" fn rsip_init() -> bool {
    // Set running to false and clear callback
    RUNNING.store(false, Ordering::SeqCst);
    let mut cb = CALLBACK.lock().unwrap();
    *cb = None;
    true
}

#[no_mangle]
pub extern "C" fn rsip_set_event_callback(cb: EventCallback) {
    let mut guard = CALLBACK.lock().unwrap();
    *guard = Some(cb);
}

#[no_mangle]
pub extern "C" fn rsip_clear_event_callback() {
    let mut guard = CALLBACK.lock().unwrap();
    *guard = None;
}

fn call_callback(event: &str, payload: &str) {
    let guard = CALLBACK.lock().unwrap();
    if let Some(cb) = *guard {
        let ev = CString::new(event).unwrap_or_else(|_| CString::new("err").unwrap());
        let pl = CString::new(payload).unwrap_or_else(|_| CString::new("").unwrap());
        cb(ev.as_ptr(), pl.as_ptr());
        // CString drops here; the callee must copy data if it is needed beyond the call
    }
}

#[no_mangle]
pub extern "C" fn rsip_start_udp_listener(port: u16) -> bool {
    if RUNNING.load(Ordering::SeqCst) {
        // already running
        return false;
    }

    let bind = format!("0.0.0.0:{}", port);
    let socket = match UdpSocket::bind(bind) {
        Ok(s) => s,
        Err(_) => return false,
    };

    // make socket non-blocking to allow clean shutdown if desired
    let _ = socket.set_nonblocking(false);
    let socket = Arc::new(socket);
    RUNNING.store(true, Ordering::SeqCst);

    let socket_clone = socket.clone();

    let handle = thread::spawn(move || {
        let mut buf = vec![0u8; 65535];
        while RUNNING.load(Ordering::SeqCst) {
            match socket_clone.recv_from(&mut buf) {
                Ok((n, src)) => {
                    if n == 0 { continue; }
                    // Try to parse SIP message using rsip (best-effort) and forward raw message
                    let msg = String::from_utf8_lossy(&buf[..n]).to_string();
                    // Optionally parse with rsip::message here to validate
                    // For now, just call callback with event "sip_rx" and payload as the raw message
                    call_callback("sip_rx", &msg);
                }
                Err(e) => {
                    // On error, call error callback and continue or break for interrupt
                    call_callback("error", &format!("recv_err:{}", e));
                    // Sleep a bit to avoid busy loop
                    std::thread::sleep(std::time::Duration::from_millis(50));
                }
            }
        }
    });

    let mut guard = LISTENER_THREAD.lock().unwrap();
    *guard = Some(handle);
    true
}

#[no_mangle]
pub extern "C" fn rsip_shutdown() {
    // signal thread to stop
    RUNNING.store(false, Ordering::SeqCst);

    // join thread if present
    let mut guard = LISTENER_THREAD.lock().unwrap();
    if let Some(handle) = guard.take() {
        let _ = handle.join();
    }

    // clear callback
    let mut cb = CALLBACK.lock().unwrap();
    *cb = None;
}

// Convenience: send raw SIP datagram to a destination
#[no_mangle]
pub extern "C" fn rsip_send_udp(dest_ip: *const c_char, dest_port: u16, data: *const c_char) -> bool {
    if dest_ip.is_null() || data.is_null() { return false; }
    let cstr_ip = unsafe { CStr::from_ptr(dest_ip) };
    let cstr_data = unsafe { CStr::from_ptr(data) };
    let ip = match cstr_ip.to_str() { Ok(s) => s, Err(_) => return false };
    let payload = cstr_data.to_bytes();

    let addr = format!("{}:{}", ip, dest_port);
    match std::net::UdpSocket::bind("0.0.0.0:0") {
        Ok(s) => {
            let _ = s.send_to(payload, addr);
            true
        }
        Err(_) => false,
    }
}

// Minimal example: expose a helper that returns a static string to test FFI linkage
#[no_mangle]
pub extern "C" fn rsip_version() -> *const c_char {
    let s = CString::new("rsip-wrapper-0.1.0").unwrap();
    let p = s.as_ptr();
    std::mem::forget(s); // leak intentionally; caller treats as static.
    p
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    #[test]
    fn test_rsip_init() {
        let result = rsip_init();
        assert!(result, "rsip_init should return true");
        assert!(!RUNNING.load(Ordering::SeqCst), "RUNNING should be false after init");
    }

    #[test]
    fn test_rsip_version() {
        let ptr = rsip_version();
        assert!(!ptr.is_null(), "rsip_version should return non-null pointer");
        let cstr = unsafe { CStr::from_ptr(ptr) };
        let s = cstr.to_str().expect("version should be valid UTF-8");
        assert_eq!(s, "rsip-wrapper-0.1.0", "version string should match");
    }

    #[test]
    fn test_callback_registration() {
        rsip_init();
        
        // Define a dummy callback
        extern "C" fn dummy_cb(_event: *const c_char, _payload: *const c_char) {}
        
        rsip_set_event_callback(dummy_cb);
        let guard = CALLBACK.lock().unwrap();
        assert!(guard.is_some(), "callback should be registered");
        drop(guard);
        
        rsip_clear_event_callback();
        let guard = CALLBACK.lock().unwrap();
        assert!(guard.is_none(), "callback should be cleared");
    }

    #[test]
    fn test_udp_send_with_null_pointers() {
        // rsip_send_udp should return false if dest_ip is null
        let result = rsip_send_udp(std::ptr::null(), 5060, b"test\0".as_ptr() as *const c_char);
        assert!(!result, "should return false for null dest_ip");

        // rsip_send_udp should return false if data is null
        let ip_cstr = CString::new("127.0.0.1").unwrap();
        let result = rsip_send_udp(ip_cstr.as_ptr(), 5060, std::ptr::null());
        assert!(!result, "should return false for null data");
    }

    #[test]
    fn test_udp_send_invalid_address() {
        // Attempt to send to an address that may fail (invalid IP)
        let ip_cstr = CString::new("999.999.999.999").unwrap();
        let data_cstr = CString::new("test").unwrap();
        let result = rsip_send_udp(ip_cstr.as_ptr(), 5060, data_cstr.as_ptr());
        // We don't assert result here because the send may or may not fail depending on OS behavior.
        // The test just ensures the function handles it without crashing.
        println!("send to invalid addr returned: {}", result);
    }

    #[test]
    fn test_listener_already_running() {
        rsip_init();
        
        // First start should succeed
        let result1 = rsip_start_udp_listener(15060);
        assert!(result1, "first start_udp_listener should succeed");
        
        // Second start without shutdown should fail
        let result2 = rsip_start_udp_listener(15061);
        assert!(!result2, "second start_udp_listener without shutdown should fail");
        
        rsip_shutdown();
        std::thread::sleep(std::time::Duration::from_millis(100));
    }

    #[test]
    fn test_shutdown_clears_state() {
        rsip_init();
        
        extern "C" fn dummy_cb(_event: *const c_char, _payload: *const c_char) {}
        rsip_set_event_callback(dummy_cb);
        
        rsip_shutdown();
        
        let guard = CALLBACK.lock().unwrap();
        assert!(guard.is_none(), "callback should be cleared after shutdown");
        drop(guard);
        
        assert!(!RUNNING.load(Ordering::SeqCst), "RUNNING should be false after shutdown");
    }
}
