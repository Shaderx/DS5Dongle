// Exercise the production session owner and its GAP effects without a radio.
#include "../../src/macro/bt_pairing.hpp"
#include <cassert>
#include <cstdio>

namespace {
bool connectable, discoverable;
int starts, stops;
std::uint8_t start_status;
void reset() {
    connectable = discoverable = false;
    starts = stops = 0;
    start_status = 0;
}
void test_saved_boot_rejects_unsolicited_events() {
    reset();
    bool inquiring = true;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.stack_ready();
    assert(!inquiring && starts == 0 && !connectable && !discoverable);
    assert(!session.allowed() && !session.accepts_result() && !session.accepts_connection());
    assert(!session.inquiry_complete(true, true));
    session.disconnected(true);
    assert(starts == 0 && !session.allowed() && !connectable && !discoverable);
}
void test_manual_inquiry_expires_without_retries() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    assert(!session.open(false));
    assert(session.allowed() && session.accepts_result() && inquiring);
    assert(connectable && discoverable && starts == 1);
    assert(!session.inquiry_complete(false, true));
    assert(!inquiring && !session.allowed() && !connectable && !discoverable);
    // Duplicate raw/GAP completion, late results, and disconnect cannot reopen.
    assert(!session.inquiry_complete(true, true));
    session.disconnected(true);
    assert(!session.accepts_result() && starts == 1);
}
void test_inquiry_command_rejection_closes_until_next_gesture() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    // BTstack emits COMMAND_STATUS with an error and no inquiry-complete event.
    assert(!session.inquiry_complete(false, true));
    assert(!session.allowed() && !inquiring && !connectable && !discoverable);
    assert(starts == 1 && stops == 0);
    session.open(false);
    assert(session.allowed() && inquiring && starts == 2);
}
void test_security_events_require_the_current_acl_and_open_session() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    assert(!session.accepts_security_event(0x40, 0x40));
    session.open(false);
    assert(session.accepts_security_event(0x40, 0x40));
    assert(!session.accepts_security_event(0x40, 0x41));
    session.finish(true);
    session.open(false); // A fresh pairing request must ignore the old ACL's events.
    assert(!session.accepts_security_event(0x40, 0xffff));
    assert(session.allowed() && inquiring && starts == 2);
}
void test_outgoing_failure_and_connected_disconnect_return_to_standby() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    assert(session.inquiry_complete(true, true));
    assert(!session.inquiry_complete(true, true)); // Exactly one create-ACL decision.
    assert(session.allowed() && !session.accepts_result());
    session.finish(true); // Create-ACL command, ACL connect, or authentication failure.
    assert(!session.allowed() && !inquiring && !connectable && !discoverable);
    session.disconnected(true);
    assert(starts == 1);

    session.open(false);
    assert(session.inquiry_complete(true, true));
    session.disconnected(true); // Successful link later disconnects.
    assert(starts == 2 && !session.allowed() && !connectable && !discoverable);
}
void test_incoming_connection_survives_inquiry_cancel_then_closes() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    session.incoming();
    assert(!session.inquiry_complete(false, true));
    assert(session.allowed() && session.accepts_connection());
    session.disconnected(true);
    assert(!session.allowed() && !connectable && !discoverable && starts == 1);
}
void test_switch_requires_explicit_intent_and_runs_once() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    assert(session.inquiry_complete(true, true));
    assert(session.open(true)); // Six-second gesture while connected.
    assert(!session.accepts_connection() && !session.accepts_result());
    session.disconnected(true);
    assert(session.allowed() && session.accepts_result() && inquiring && starts == 2);
    session.inquiry_complete(false, true);
    session.disconnected(true);
    assert(!session.allowed() && starts == 2);
}
void test_switch_survives_auth_failure_before_disconnect() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    assert(session.inquiry_complete(true, true));
    assert(session.open(true));
    session.finish(true); // The old ACL fails auth while its disconnect is pending.
    assert(!session.accepts_connection() && starts == 1);
    session.disconnected(true);
    assert(session.allowed() && session.accepts_connection() && inquiring && starts == 2);
    session.inquiry_complete(false, true);
    session.disconnected(true);
    assert(!session.allowed() && starts == 2);
}
void test_failure_stops_active_inquiry_and_start_rejection_closes() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(true);
    session.open(false);
    session.finish(true);
    assert(stops == 1 && !inquiring && !session.allowed());
    start_status = 1;
    session.open(false);
    assert(starts == 2 && !inquiring && !session.allowed() && !connectable && !discoverable);
}
void test_first_use_retains_automatic_start_and_reconnect() {
    reset();
    bool inquiring = false;
    dongle_macro::BluetoothPairing session(inquiring);
    session.init(false);
    assert(session.allowed() && connectable && discoverable);
    session.stack_ready();
    assert(inquiring && starts == 1);
    session.inquiry_complete(false, false);
    assert(session.allowed() && connectable && discoverable && starts == 1);
    session.incoming();
    session.disconnected(false);
    assert(session.allowed() && connectable && discoverable && starts == 1);
    // A save made during first use also gates the next disconnected session.
    session.disconnected(true);
    assert(!session.allowed() && !connectable && !discoverable);
}
}
void gap_connectable_control(std::uint8_t value) { connectable = value != 0; }
void gap_discoverable_control(std::uint8_t value) { discoverable = value != 0; }
int gap_inquiry_start(std::uint8_t duration) {
    assert(duration == 30);
    ++starts;
    return start_status;
}
int gap_inquiry_stop() { ++stops; return 0; }
int main() {
    test_saved_boot_rejects_unsolicited_events();
    test_manual_inquiry_expires_without_retries();
    test_inquiry_command_rejection_closes_until_next_gesture();
    test_security_events_require_the_current_acl_and_open_session();
    test_outgoing_failure_and_connected_disconnect_return_to_standby();
    test_incoming_connection_survives_inquiry_cancel_then_closes();
    test_switch_requires_explicit_intent_and_runs_once();
    test_switch_survives_auth_failure_before_disconnect();
    test_failure_stops_active_inquiry_and_start_rejection_closes();
    test_first_use_retains_automatic_start_and_reconnect();
    std::puts("Macro Bluetooth pairing tests passed");
}
