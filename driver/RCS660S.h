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

#ifndef RCS660S_H_
#define RCS660S_H_

#include <inttypes.h>
#include "hardware/uart.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

/* --------------------------------
 * Constant
 * -------------------------------- */

#define RCS660S_MAX_CARD_RESPONSE_LEN    254
#define RCS660S_MAX_RW_RESPONSE_LEN      265

#define RCS660S_DEFAULT_TIMEOUT          1000
#define RCS660S_BUFFER_SIZE              512

/* --------------------------------
 * Class Declaration
 * -------------------------------- */

class RCS660S
{
public:
    RCS660S(int txd, int rxd, uart_inst_t* ch, unsigned int baud = 115200);
    ~RCS660S();

    int initDevice(void);
    int polling(uint16_t systemCode = 0xffff);
    int cardCommand(
        const uint8_t* command,
        uint8_t commandLen,
        uint8_t response[RCS660S_MAX_CARD_RESPONSE_LEN],
        uint8_t* responseLen);
    int cardCommand(
        const uint8_t* command,
        uint8_t commandLen,
        uint8_t* response,
        uint16_t* responseLen);
    int rfOff(void);

    int push(
        const uint8_t* data,
        uint8_t dataLen);

public:
    unsigned long timeout;
    uart_inst_t* uart_ch;
    uint8_t idm[8];
    uint8_t pmm[8];

private:
    uint8_t bseq;

    uint8_t calcDCS(const uint8_t* data, uint16_t len);
    void writeSerial(const uint8_t* data, uint16_t len);
    int readSerial(uint8_t* data, uint16_t len);
    void flushSerial(void);

    int send_ccid_command(const uint8_t* command, uint16_t command_len);
    int receive_ack(void);
    int receive_ccid_response(uint8_t* response, uint16_t* response_len);
    int write_apdu(const uint8_t* data, uint32_t data_len);
    int abort_command(void);
};

#endif /* !RCS660S_H_ */

