/*
 * RC-S660/S library for Raspberry Pi Pico
 * API-compatible with RCS620S class library
 *
 * Copyright (c) 2026 Toyomasa Watarai
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: Parts of this implementation were generated or assisted by
 * Google Antigravity / AI tools and verified/modified by the author.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "RCS660S.h"

/* ------------------------
 * public
 * ------------------------ */

RCS660S::RCS660S(int txd, int rxd, uart_inst_t* ch, unsigned int baud) {
    this->timeout = RCS660S_DEFAULT_TIMEOUT;
    this->uart_ch = ch;
    this->bseq = 0;
    memset(this->idm, 0, sizeof(this->idm));
    memset(this->pmm, 0, sizeof(this->pmm));

    uart_init(this->uart_ch, baud);
    gpio_set_function(txd, UART_FUNCSEL_NUM(this->uart_ch, txd));
    gpio_set_function(rxd, UART_FUNCSEL_NUM(this->uart_ch, rxd));
    gpio_pull_up(txd);
    gpio_pull_up(rxd);
}

RCS660S::~RCS660S() {
}

int RCS660S::initDevice(void) {
    uint8_t buf[RCS660S_BUFFER_SIZE];
    uint16_t buf_len = 0;

    flushSerial();

    // 1. Wakeup: Single rising edge
    uint8_t wake = 0x01;
    writeSerial(&wake, 1);
    sleep_ms(30);
    flushSerial();

    // 2. CCID Abort
    abort_command();
    sleep_ms(10);

    // 3. End Transparent Session (if any was active)
    if (write_apdu((const uint8_t*)"\xFF\xC2\x00\x00\x02\x82\x00", 7)) {
        receive_ccid_response(buf, &buf_len);
    }
    sleep_ms(10);

    // 4. Start Transparent Session
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x00\x02\x81\x00", 7)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // 5. Switch Protocol to FeliCa (Standard Type 0x03, Layer 0x00)
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x02\x04\x8F\x02\x03\x00", 9)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // 6. Transparent Exchange: Transmission and Reception Flag
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x01\x04\x90\x02\x00\x1C", 9)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // 7. Transparent Exchange: Bit Framing
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x01\x03\x91\x01\x00", 8)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // 8. Manage Session: Set Parameters
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x00\x06\xFF\x6E\x03\x05\x01\x89", 11)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // 9. Manage Session: Turn On RF Field
    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x00\x02\x84\x00\x00", 8)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    return 1;
}

int RCS660S::polling(uint16_t systemCode) {
    uint8_t command[] = {
        0xFF, // CLA
        0xC2, // INS
        0x00, // P1
        0x01, // P2 (Transparent Exchange)
        0x0F, // Lc
        // Timer Data Object
        0x5F, 0x46,             // Tag: Timer
        0x04,                   // Length
        0x60, 0xEA, 0x00, 0x00, // Timer Value (0x0000EA60 us = 60ms)
        // Transceive Data Object
        0x95,       // Tag: Transceive
        0x06, 0x06, // Length
        0x00,       // FeliCa Polling Command (0x00)
        (uint8_t)((systemCode >> 8) & 0xFF),
        (uint8_t)((systemCode >> 0) & 0xFF),
        0x00, 0x00  // RequestCode=0x00, TimeSlot=0x00
    };

    uint8_t buf[RCS660S_BUFFER_SIZE];
    uint16_t buf_len = 0;

    if (!write_apdu(command, sizeof(command))) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }

    // Validate response length and headers
    if (buf_len != 44 || buf[0] != 0x83 || buf[7] != 0x02) {
        return 0;
    }

    if (memcmp(buf + 10, "\xC0\x03\x00\x90\x00", 5) != 0) {
        return 0;
    }

    memcpy(this->idm, buf + 26, 8);
    memcpy(this->pmm, buf + 34, 8);

    return 1;
}

int RCS660S::cardCommand(
    const uint8_t* command,
    uint8_t commandLen,
    uint8_t response[RCS660S_MAX_CARD_RESPONSE_LEN],
    uint8_t* responseLen)
{
    uint16_t rlen = 0;
    int ret = cardCommand(command, commandLen, response, RCS660S_MAX_CARD_RESPONSE_LEN, &rlen);
    if (!ret) {
        return 0;
    }
    if (responseLen != nullptr) {
        *responseLen = (uint8_t)rlen;
    }
    return 1;
}

int RCS660S::cardCommand(
    const uint8_t* command,
    uint8_t commandLen,
    uint8_t* response,
    uint16_t responseSize,
    uint16_t* responseLen)
{
    if (command == nullptr || response == nullptr || responseLen == nullptr) {
        return 0;
    }
    if (commandLen == 0) {
        return 0;
    }

    uint8_t buf[RCS660S_BUFFER_SIZE];
    uint16_t buf_len = 0;

    // カードから応答がない場合 (SW 64 01) はリトライする
    // buf はレスポンスで上書きされるため、送信ごとにAPDUを組み立て直す
    for (int attempt = 0; ; attempt++) {
        // Build command APDU
        buf[0] = 0xFF; // CLA
        buf[1] = 0xC2; // INS
        buf[2] = 0x00; // P1
        buf[3] = 0x01; // P2 (Transparent Exchange)
        buf[4] = (uint8_t)(commandLen + 10); // Lc

        // Timer Data Object
        buf[5] = 0x5F;
        buf[6] = 0x46;
        buf[7] = 0x04;
        uint32_t timeout_us = this->timeout * 1000;
        if (timeout_us == 0) timeout_us = 60000;
        buf[8]  = (uint8_t)(timeout_us & 0xFF);
        buf[9]  = (uint8_t)((timeout_us >> 8) & 0xFF);
        buf[10] = (uint8_t)((timeout_us >> 16) & 0xFF);
        buf[11] = (uint8_t)((timeout_us >> 24) & 0xFF);

        // Transceive Data Object (FeliCa frame: LEN + command, LEN includes itself)
        buf[12] = 0x95;
        buf[13] = (uint8_t)(commandLen + 1);
        buf[14] = (uint8_t)(commandLen + 1);
        memcpy(buf + 15, command, commandLen);

        uint32_t apdu_len = 15 + commandLen;

        // 前の応答から次のコマンドまでの間隔を確保する
        sleep_us(RCS660S_COMMAND_INTERVAL_US);

        if (!write_apdu(buf, apdu_len)) {
            return 0;
        }
        if (!receive_ccid_response(buf, &buf_len)) {
            return 0;
        }

        bool no_response = (buf_len >= 15 && buf[0] == 0x83 &&
                            memcmp(buf + 10, "\xC0\x03\x02\x64\x01", 5) == 0);
        if (!no_response || attempt >= RCS660S_CARD_COMMAND_RETRY) {
            break;
        }
    }

    if (buf_len < 25 || buf[0] != 0x83 || buf[7] != 0x02) {
        return 0;
    }

    if (memcmp(buf + 10, "\xC0\x03\x00\x90\x00", 5) != 0) {
        return 0;
    }

    if (buf[23] == 0) {
        return 0;
    }
    uint16_t card_res_len = (uint16_t)(buf[23] - 1);
    if ((uint32_t)(25 + card_res_len) > buf_len) {
        return 0;
    }
    if (card_res_len > responseSize) {
        return 0;
    }

    memcpy(response, buf + 25, card_res_len);
    *responseLen = card_res_len;

    return 1;
}

int RCS660S::rfOff(void) {
    uint8_t buf[RCS660S_BUFFER_SIZE];
    uint16_t buf_len = 0;

    if (!write_apdu((const uint8_t*)"\xFF\xC2\x00\x00\x02\x83\x00\x00", 8)) {
        return 0;
    }
    if (!receive_ccid_response(buf, &buf_len)) {
        return 0;
    }
    return 1;
}

int RCS660S::push(const uint8_t* data, uint8_t dataLen) {
    int ret;
    uint8_t buf[RCS660S_MAX_CARD_RESPONSE_LEN];
    uint8_t responseLen;

    if (dataLen > 224) {
        return 0;
    }

    /* Push */
    buf[0] = 0xb0;
    memcpy(buf + 1, this->idm, 8);
    buf[9] = dataLen;
    memcpy(buf + 10, data, dataLen);

    ret = cardCommand(buf, 10 + dataLen, buf, &responseLen);
    if (!ret || (responseLen != 10) || (buf[0] != 0xb1) ||
        (memcmp(buf + 1, this->idm, 8) != 0) || (buf[9] != dataLen)) {
        return 0;
    }

    buf[0] = 0xa4;
    memcpy(buf + 1, this->idm, 8);
    buf[9] = 0x00;

    ret = cardCommand(buf, 10, buf, &responseLen);
    if (!ret || (responseLen != 10) || (buf[0] != 0xa5) ||
        (memcmp(buf + 1, this->idm, 8) != 0) || (buf[9] != 0x00)) {
        return 0;
    }

    sleep_ms(1000);

    return 1;
}

/* ------------------------
 * private
 * ------------------------ */

uint8_t RCS660S::calcDCS(const uint8_t* data, uint16_t len) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)-(sum & 0xff);
}

void RCS660S::writeSerial(const uint8_t* data, uint16_t len) {
    uart_write_blocking(this->uart_ch, data, len);
}

int RCS660S::readSerial(uint8_t* data, uint16_t len) {
    uint16_t nread = 0;
    absolute_time_t timeout_time = make_timeout_time_ms(this->timeout + RCS660S_UART_TIMEOUT_MARGIN);

    while (nread < len) {
        if (uart_is_readable(this->uart_ch)) {
            data[nread++] = uart_getc(this->uart_ch);
        } else {
            if (time_reached(timeout_time)) {
                return 0;
            }
            sleep_us(100);
        }
    }
    return 1;
}

void RCS660S::flushSerial(void) {
    while (uart_is_readable(this->uart_ch)) {
        uart_getc(this->uart_ch);
    }
}

int RCS660S::receive_ack(void) {
    uint8_t ack[7];
    if (!readSerial(ack, 7)) {
        return 0;
    }
    if (memcmp(ack, "\x00\x00\xff\x00\x00\xff\x00", 7) != 0) {
        return 0;
    }
    return 1;
}

int RCS660S::send_ccid_command(const uint8_t* command, uint16_t command_len) {
    if (command == nullptr || command_len == 0 || command_len > (RCS660S_BUFFER_SIZE - 8)) {
        return 0;
    }
    
    flushSerial();
    uint8_t dcs = calcDCS(command, command_len);
    uint8_t buf[RCS660S_BUFFER_SIZE];

    buf[0] = 0x00; // Preamble
    buf[1] = 0x00; // Start code MSB
    buf[2] = 0xFF; // Start code LSB
    buf[3] = (uint8_t)((command_len >> 8) & 0xFF); // Length MSB
    buf[4] = (uint8_t)(command_len & 0xFF);        // Length LSB
    buf[5] = (uint8_t)-(buf[3] + buf[4]);          // Length checksum
    memcpy(buf + 6, command, command_len);         // CCID command data
    buf[6 + command_len] = dcs;                    // DCS
    buf[6 + command_len + 1] = 0x00;               // Postamble

    writeSerial(buf, 6 + command_len + 2);
    return 1;
}

int RCS660S::receive_ccid_response(uint8_t* response, uint16_t* response_len) {
    if (response == nullptr || response_len == nullptr) {
        return 0;
    }

    // Read 6-byte header: 00 00 FF LEN_H LEN_L LCS
    uint8_t header[6];
    if (!readSerial(header, 6)) {
        return 0;
    }

    if (memcmp(header, "\x00\x00\xff", 3) != 0) {
        return 0;
    }

    if (((header[3] + header[4] + header[5]) & 0xFF) != 0) {
        return 0;
    }

    uint16_t pd_len = ((uint16_t)header[3] << 8) | header[4];
    if (pd_len > (RCS660S_BUFFER_SIZE - 8)) {
        return 0;
    }

    // Read payload + DCS + postamble
    uint8_t buf[RCS660S_BUFFER_SIZE];
    uint16_t need_len = pd_len + 2;
    if (!readSerial(buf, need_len)) {
        return 0;
    }

    uint8_t dcs = calcDCS(buf, pd_len);
    if (buf[pd_len] != dcs || buf[pd_len + 1] != 0x00) {
        return 0;
    }

    memcpy(response, buf, pd_len);
    *response_len = pd_len;
    return 1;
}

int RCS660S::write_apdu(const uint8_t* data, uint32_t data_len) {
    if (data == nullptr || data_len == 0 || (10 + data_len) > RCS660S_BUFFER_SIZE) {
        return 0;
    }
    flushSerial();

    uint8_t ccid[RCS660S_BUFFER_SIZE];
    ccid[0] = 0x6B; // PC_to_RDR_Escape
    ccid[1] = (uint8_t)(data_len & 0xFF);
    ccid[2] = (uint8_t)((data_len >> 8) & 0xFF);
    ccid[3] = (uint8_t)((data_len >> 16) & 0xFF);
    ccid[4] = (uint8_t)((data_len >> 24) & 0xFF);
    ccid[5] = 0x00; // bSlot
    ccid[6] = ++this->bseq; // bSeq
    ccid[7] = 0x00; // RFU
    ccid[8] = 0x00; // RFU
    ccid[9] = 0x00; // RFU
    memcpy(ccid + 10, data, data_len);

    if (!send_ccid_command(ccid, 10 + data_len)) {
        return 0;
    }
    if (!receive_ack()) {
        return 0;
    }

    return 1;
}

int RCS660S::abort_command(void) {
    uint8_t abort[10] = {
        0x72, // PC_to_RDR_Abort
        0x00, 0x00, 0x00, 0x00, // Length = 0
        0x00, // bSlot
        (uint8_t)(++this->bseq),
        0x00, 0x00, 0x00 // RFU
    };
    uint8_t response_buf[RCS660S_BUFFER_SIZE];
    uint16_t response_len = 0;

    if (!send_ccid_command(abort, sizeof(abort))) return 0;
    if (!receive_ack()) return 0;
    if (!receive_ccid_response(response_buf, &response_len)) return 0;
    return 1;
}

