/*
 * FeliCa read-only dumper for Raspberry Pi Pico and RC-S660/S.
 * The RC-S660/S driver is adapted from toyowata/felica-reader-rcs660s-pico.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "config.h"
#include "driver/RCS660S.h"

static RCS660S reader(RCS660S_TX_PIN, RCS660S_RX_PIN, uart0);
static uint8_t response[RCS660S_MAX_CARD_RESPONSE_LEN];

static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static void print_hex(const uint8_t *bytes, unsigned count) {
    for (unsigned i = 0; i < count; ++i) printf("%02X%s", bytes[i], i + 1 == count ? "" : " ");
}

static bool command(const uint8_t *request, uint8_t request_len,
                    uint8_t expected, uint8_t *response_len) {
    if (!reader.cardCommand(request, request_len, response, response_len) ||
        *response_len < 9 || response[0] != expected ||
        memcmp(response + 1, reader.idm, 8) != 0) return false;
    return true;
}

static bool poll(uint16_t system) {
    // The reference driver's Polling API expects the two System Code bytes swapped.
    return reader.polling(swap16(system)) != 0;
}

static bool contains(const uint16_t *values, unsigned count, uint16_t value) {
    for (unsigned i = 0; i < count; ++i) if (values[i] == value) return true;
    return false;
}

static bool request_system_codes(uint16_t *systems, unsigned *count) {
    uint8_t request[9] = {0x0C};
    memcpy(request + 1, reader.idm, 8);
    uint8_t len = 0;
    if (!command(request, sizeof(request), 0x0D, &len) || len < 10 ||
        len != 10u + 2u * response[9]) return false;
    for (unsigned i = 0; i < response[9] && *count < MAX_DISCOVERED_SYSTEMS; ++i) {
        // System Code responses use the same MSB-first order as Polling requests.
        uint16_t code = (uint16_t)((response[10 + i * 2] << 8) | response[11 + i * 2]);
        if (!contains(systems, *count, code)) systems[(*count)++] = code;
    }
    return true;
}

static bool search_service(unsigned index, uint16_t *code, bool *is_area, bool *end) {
    uint8_t request[11] = {0x0A};
    memcpy(request + 1, reader.idm, 8);
    request[9] = (uint8_t)index;
    request[10] = (uint8_t)(index >> 8);
    uint8_t len = 0;
    if (!command(request, sizeof(request), 0x0B, &len) || (len != 11 && len != 13)) return false;
    *code = (uint16_t)(response[9] | (response[10] << 8));
    *is_area = len == 13;
    *end = *code == 0xFFFF;
    return true;
}

static bool request_service(uint16_t code, uint16_t *version) {
    uint8_t request[12] = {0x02};
    memcpy(request + 1, reader.idm, 8);
    request[9] = 1;
    request[10] = (uint8_t)code;
    request[11] = (uint8_t)(code >> 8);
    uint8_t len = 0;
    if (!command(request, sizeof(request), 0x03, &len) || len != 12 || response[9] != 1)
        return false;
    *version = (uint16_t)(response[10] | (response[11] << 8));
    return *version != 0xFFFF;
}

enum class ReadResult { Data, End, Error };

static ReadResult read_block(uint16_t service, unsigned block, uint8_t data[16],
                             uint8_t *status1, uint8_t *status2) {
    uint8_t request[15] = {0x06};
    memcpy(request + 1, reader.idm, 8);
    request[9] = 1;
    request[10] = (uint8_t)service;
    request[11] = (uint8_t)(service >> 8);
    request[12] = 1;
    // Two-byte Block List Element, service-list index 0.
    request[13] = 0x80;
    request[14] = (uint8_t)block;
    uint8_t len = 0;
    if (!command(request, sizeof(request), 0x07, &len) || len < 11) return ReadResult::Error;
    *status1 = response[9];
    *status2 = response[10];
    if (*status1 || *status2) return ReadResult::End;
    if (len != 28 || response[11] != 1) return ReadResult::Error;
    memcpy(data, response + 12, 16);
    return ReadResult::Data;
}

static const char *hint(uint16_t system, uint16_t service) {
    if (system == 0x0003) {
        switch (service) {
            case 0x23CB: return "Suica候補";
            case 0x1CC8: return "PASMO候補";
            case 0x208B: return "Kitaca候補";
            case 0x1E8B: return "TOICA候補";
            case 0x21C8: return "SUGOCA候補";
            case 0x1F48: return "nimoca候補";
            case 0x2448: return "はやかけん候補";
            case 0x9888: return "manaca候補";
            case 0x1B88: return "PiTaPa候補";
            case 0x2888: return "Suica+候補";
            case 0x090A: return "Welcome Suica候補";
            case 0x090F: return "交通系ICカード候補";
            default: break;
        }
    }
    if (system == 0xFE00) {
        if (service == 0x110B || service == 0x1317) return "楽天Edy候補";
        if (service == 0x558B || service == 0x5597) return "nanaco候補";
        if (service == 0x680B || service == 0x6817) return "WAON候補";
    }
    if (system == 0x88B4) return "FeliCa Lite-S候補";
    if (system == 0x5E86 && service == 0xBA4B) return "SAPICA候補";
    if (system == 0x9285 && service == 0xB00B) return "PASPY候補";
    if (system == 0x5D8B && service == 0x804B) return "りゅーと候補";
    if (system == 0x2C83 && service == 0x804B) return "ecomyca候補";
    return nullptr;
}

static const char *service_type(uint16_t code) {
    switch (code & 0x3F) {
        case 0x08: return "Random Access R/W (Protected)";
        case 0x09: return "Random Access R/W";
        case 0x0A: return "Random Access Read only (Protected)";
        case 0x0B: return "Random Access Read only";
        case 0x0C: return "Cyclic Access R/W (Protected)";
        case 0x0D: return "Cyclic Access R/W";
        case 0x0E: return "Cyclic Access Read only (Protected)";
        case 0x0F: return "Cyclic Access Read only";
        case 0x10: return "Purse (Direct) (Protected)";
        case 0x11: return "Purse (Direct)";
        case 0x12: return "Purse (Cashback/decrement) (Protected)";
        case 0x13: return "Purse (Cashback/decrement)";
        case 0x14: return "Purse (Decrement) (Protected)";
        case 0x15: return "Purse (Decrement)";
        case 0x16: return "Purse (Read only) (Protected)";
        case 0x17: return "Purse (Read only)";
        default: return "Unknown";
    }
}

static bool readable_without_auth(uint16_t code) {
    const unsigned type = code & 0x3F;
    return type >= 0x08 && type <= 0x17 && (type & 1);
}

static void dump_service(uint16_t service) {
    unsigned blocks = 0;
    for (; blocks < MAX_BLOCKS_PER_SERVICE; ++blocks) {
        uint8_t data[16], s1 = 0, s2 = 0;
        ReadResult result = read_block(service, blocks, data, &s1, &s2);
        if (result == ReadResult::End) {
            break;
        }
        if (result == ReadResult::Error) {
            printf("# Read error at %04X:%04X (card removed or communication error)\n", service, blocks);
            break;
        }
        printf("%04X:%04X ", service, blocks);
        print_hex(data, 16);
        printf(" \n");
    }
    if (blocks == MAX_BLOCKS_PER_SERVICE) printf("# Block limit reached: %u\n", MAX_BLOCKS_PER_SERVICE);
}

static bool dump_system(uint16_t system) {
    if (!poll(system)) return false;
    printf("\n# System code: %04X\n# IDm: ", system);
    print_hex(reader.idm, 8);
    printf(" \n");

    uint16_t services[MAX_DISCOVERED_SERVICES] = {};
    unsigned count = 0;
    struct Area { uint16_t first, last; } areas[MAX_DISCOVERED_SERVICES] = {};
    unsigned area_count = 0;
    bool search_ok = false;
    bool complete = false;
    for (unsigned index = 0; index < MAX_DISCOVERED_SERVICES; ++index) {
        uint16_t code = 0;
        bool area = false, end = false;
        if (!search_service(index, &code, &area, &end)) break;
        search_ok = true;
        if (end) { complete = true; break; }
        if (area) {
            uint16_t last = (uint16_t)(response[11] | (response[12] << 8));
            areas[area_count++] = {code, last};
        } else if (!contains(services, count, code)) {
            services[count++] = code;
        }
    }
    unsigned discovered_services = count;
    for (uint16_t code : EXTRA_SERVICE_CODES) {
        if (count >= MAX_DISCOVERED_SERVICES) break;
        if (!contains(services, count, code)) {
            uint16_t version = 0;
            if (request_service(code, &version)) services[count++] = code;
        }
    }
    printf("# Number of area = %u\n", area_count);
    for (unsigned i = 0; i < area_count; ++i)
        printf("# Area: %04X - %04X\n", areas[i].first, areas[i].last);
    printf("# Number of service code = %u\n", count);
    for (unsigned i = 0; i < count; ++i) {
        printf("# Service code = %04X : %s\n", services[i], service_type(services[i]));
        if (readable_without_auth(services[i])) dump_service(services[i]);
    }
    if (!search_ok) printf("# Search Service Code unsupported or failed; only probed services are shown.\n");
    else if (!complete) printf("# Service enumeration stopped after %u entries.\n", discovered_services + area_count);
    const char *card_hint = nullptr;
    for (unsigned i = 0; i < count; ++i) {
        const char *candidate = hint(system, services[i]);
        if (candidate && (!card_hint || strstr(card_hint, "交通系ICカード候補"))) card_hint = candidate;
    }
    if (card_hint) printf("# Card type: %s\n", card_hint);
    return true;
}

static void dump_card() {
    uint16_t systems[MAX_DISCOVERED_SYSTEMS] = {};
    unsigned count = 0;
    printf("\n# IDm: ");
    print_hex(reader.idm, 8);
    printf(" \n# PMm: ");
    print_hex(reader.pmm, 8);
    printf(" \n");

    bool listed = request_system_codes(systems, &count);
    printf("# Number of System = %u\n", count);
    if (!listed) {
        // A wildcard Polling succeeds even on cards without Request System Code.
        // The exact System Code is unknown in this case.
        printf("# Request System Code unsupported or failed; probing configured codes.\n");
    }
    for (uint16_t code : EXTRA_SYSTEM_CODES) {
        if (count >= MAX_DISCOVERED_SYSTEMS) break;
        if (!contains(systems, count, code)) systems[count++] = code;
    }
    unsigned found = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (dump_system(systems[i])) ++found;
    }
    if (!found) printf("# No System Code identified. Add candidates to config.h.\n");
    printf("\n# End of dump\n");
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("RC-S660/S FeliCa dumper\n");
    reader.timeout = 400;
    while (!reader.initDevice()) {
        printf("RC-S660/S 初期化失敗。接続を確認して再試行します。\n");
        sleep_ms(1000);
    }
    printf("カードをかざしてください。\n");
    bool was_present = false;
    unsigned misses = 0;
    while (true) {
        if (poll(0xFFFF)) {
            misses = 0;
            if (!was_present) {
                was_present = true;
                dump_card();
                printf("カードを離してください。\n");
            }
        } else if (++misses >= 3) {
            was_present = false;
        }
        sleep_ms(POLL_INTERVAL_MS);
    }
}
