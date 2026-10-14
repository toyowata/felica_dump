#pragma once
#include <stdint.h>

// Codes are written in the conventional human-readable order (e.g. 0x0003).
// These optional probes supplement the codes discovered from the card.
static constexpr uint16_t EXTRA_SYSTEM_CODES[] = {
    0x0003, // Japanese transit IC cards
    0xFE00, // Common area (Edy, nanaco, WAON, etc.)
    0x88B4, // FeliCa Lite-S
    0x865E, // SAPICA
    0x8592, // PASPY
    0x8B5D, // ryuto
    0x832C, // ecomyca
};

static constexpr uint16_t EXTRA_SERVICE_CODES[] = {
    0x090F, // Transit usage history
    0x008B, // Transit attribute
    0x110B, // Edy attribute
    0x1317, // Edy data
    0x558B, // nanaco ID
    0x5597, // nanaco balance
    0x680B, // WAON
    0x1CC8, // PASMO identifier candidate
    0x23CB, // Suica identifier candidate
    0xBA4B, // SAPICA
    0xB00B, // PASPY
    0x804B, // ryuto/ecomyca
};

static constexpr unsigned MAX_DISCOVERED_SYSTEMS = 32;
static constexpr unsigned MAX_DISCOVERED_SERVICES = 128;
static constexpr unsigned MAX_BLOCKS_PER_SERVICE = 256;
static constexpr unsigned POLL_INTERVAL_MS = 300;
static constexpr unsigned SEARCH_SERVICE_RETRY = 3;
