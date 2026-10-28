#pragma once
#include "gap.h"

namespace dongle_macro {
// Owns the macro build's connection session. A saved macro needs one deliberate
// pairing gesture per session; transport failures cannot open another session.
class BluetoothPairing {
public:
    explicit BluetoothPairing(bool& inquiring) : inquiring_(inquiring) {}

    void init(bool saved) {
        allowed_ = !saved;
        connecting_ = switch_pending_ = false;
        inquiring_ = false;
        scans(allowed_);
    }
    bool allowed() const { return allowed_; }
    bool accepts_connection() const { return allowed_ && !switch_pending_; }
    bool accepts_result() const { return allowed_ && !connecting_ && !switch_pending_; }
    bool accepts_security_event(hci_con_handle_t handle, hci_con_handle_t active_handle) const {
        return allowed_ && handle == active_handle;
    }
    void stack_ready() {
        if (allowed_) start_inquiry();
    }
    // Return true when the caller must disconnect the current ACL first.
    bool open(bool connected) {
        allowed_ = true;
        switch_pending_ = connected;
        if (!connected) {
            if (!connecting_) start_inquiry();
        }
        return connected;
    }
    void incoming() { connecting_ = true; }
    // Both GAP and raw HCI completion events can describe the same inquiry.
    // Latch the connection once, and do not end an incoming ACL negotiation.
    bool inquiry_complete(bool found, bool saved) {
        inquiring_ = false;
        if (!allowed_ || connecting_ || switch_pending_) return false;
        if (found) { connecting_ = true; return true; }
        finish(saved);
        return false;
    }
    void finish(bool saved) {
        connecting_ = false;
        // An old ACL can fail authentication after the user requests a switch.
        // Its disconnect must still start the explicitly requested inquiry.
        if (!saved || switch_pending_) return;
        allowed_ = false;
        connecting_ = switch_pending_ = false;
        if (inquiring_) gap_inquiry_stop();
        inquiring_ = false;
        scans(false);
    }
    void disconnected(bool saved) {
        connecting_ = false;
        if (switch_pending_) {
            switch_pending_ = false;
            start_inquiry();
        } else {
            finish(saved);
            if (!saved) scans(true);
        }
    }
private:
    static void scans(bool enabled) {
        gap_connectable_control(enabled);
        gap_discoverable_control(enabled);
    }
    void start_inquiry() {
        scans(true);
        if (!inquiring_) {
            // Set first: BTstack can deliver completion while servicing a call.
            inquiring_ = true;
            if (gap_inquiry_start(30) != 0) {
                allowed_ = inquiring_ = false;
                scans(false);
            }
        }
    }
    bool& inquiring_;
    bool allowed_ = false;
    bool connecting_ = false;
    bool switch_pending_ = false;
};
}
