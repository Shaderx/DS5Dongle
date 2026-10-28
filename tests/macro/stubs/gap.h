#pragma once
#include <cstdint>
using hci_con_handle_t = std::uint16_t;
void gap_connectable_control(std::uint8_t enabled);
void gap_discoverable_control(std::uint8_t enabled);
int gap_inquiry_start(std::uint8_t duration);
int gap_inquiry_stop();
