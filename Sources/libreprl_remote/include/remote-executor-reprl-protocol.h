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

#ifndef REMOTE_EXECUTOR_REPRL_PROTOCOL_H
#define REMOTE_EXECUTOR_REPRL_PROTOCOL_H

#include <stdint.h>

/* Remote executor command packet */
#define RESP_MASK 0x80
// [1 byte opcode][4 bytes length][n bytes payload]
typedef struct {
    uint8_t opcode;
    uint32_t length;
} __attribute__ ((packed)) cmd_pkt_t;

#define REPRL_CREATE_CONTEXT 0x1
/* No command parameters */
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_create_context_rp;

#define REPRL_INIT_CONTEXT 0x2
// TODO: review these
#define REPRL_MAX_ARGV_SIZE 0x800
#define REPRL_MAX_ENVP_SIZE 0x800
typedef struct {
    uint16_t ctx_handle;
    char argv[REPRL_MAX_ARGV_SIZE]; // a string of argv separated by a whitespace
    char envp[REPRL_MAX_ENVP_SIZE]; // a string of envvars separated by ;, eg: K1=V1;K2=V2
    uint8_t capture_stdout;
    uint8_t capture_stderr;
} __attribute__ ((packed)) reprl_init_context_cp;
typedef struct {
    uint8_t status; // 0x0 for success
} __attribute__ ((packed)) reprl_init_context_rp;

#define REPRL_DESTROY_CONTEXT 0x3
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_destroy_context_cp;
/* No return */

#define REPRL_EXECUTE 0x4
typedef struct {
    uint16_t ctx_handle;
    uint64_t script_size;
    uint64_t timeout;
    uint8_t fresh_instance;
    char script[]; // char[script_size]
} __attribute__ ((packed)) reprl_execute_cp;
typedef struct {
    uint32_t status; // [ 00000000 | did_timeout | exit_code | terminating_signal ]
    uint64_t execution_time;
} __attribute__ ((packed)) reprl_execute_rp;

#define REPRL_FETCH_FUZZOUT 0x5
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_fetch_fuzzout_cp;
typedef struct {
    uint32_t data_size;
    uint8_t data[]; // char[data_size]
} __attribute__ ((packed)) reprl_fetch_fuzzout_rp;

#define REPRL_FETCH_STDOUT 0x6
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_fetch_stdout_cp;
typedef struct {
    uint32_t data_size;
    uint8_t data[]; // char[data_size]
} __attribute__ ((packed)) reprl_fetch_stdout_rp;

#define REPRL_FETCH_STDERR 0x7
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_fetch_stderr_cp;
typedef struct {
    uint32_t data_size;
    uint8_t data[]; // char[data_size]
} __attribute__ ((packed)) reprl_fetch_stderr_rp;

#define REPRL_GET_LAST_ERROR 0x8
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) reprl_get_last_error_cp;
typedef struct {
    uint32_t data_size;
    uint8_t data[]; // char[data_size]
} __attribute__ ((packed)) reprl_get_last_error_rp;

#endif
