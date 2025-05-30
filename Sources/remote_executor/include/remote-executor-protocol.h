// Copyright 2025 Zhuo Ying Jiang Li
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef REMOTE_EXECUTOR_PROTOCOL_H
#define REMOTE_EXECUTOR_PROTOCOL_H

#include <stdint.h>

/* Remote executor command packet */
#define RESP_MASK 0x80
// [1 byte opcode][4 bytes length][n bytes payload]
typedef struct {
    uint8_t opcode;
    uint32_t length;
} __attribute__ ((packed)) cmd_pkt_t;

#include "remote-executor-reprl-protocol.h"
#include "remote-executor-coverage-protocol.h"

#endif
