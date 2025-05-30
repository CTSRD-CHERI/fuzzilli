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

#if !defined(_WIN32)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "libreprl_remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#if defined(__FreeBSD__)
#include <sys/endian.h>
#include <netinet/in.h>
#endif

static uint16_t send_create_context(socket_t fd) {
    // send
    struct {
        cmd_pkt_t hdr;
    } cmd;
    cmd.hdr.opcode = REPRL_CREATE_CONTEXT;
    cmd.hdr.length = htonl(0);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_create_context: send failed\n");
        return UINT16_MAX;
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        reprl_create_context_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "send_create_context: recv hdr failed\n");
        return UINT16_MAX;
    }
    if (resp.hdr.opcode != (REPRL_CREATE_CONTEXT | RESP_MASK)) {
        fprintf(stderr, "send_create_context: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return UINT16_MAX;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_create_context_rp)) {
        fprintf(stderr, "send_create_context: invalid payload size: %u\n", payload_len);
        return UINT16_MAX;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "send_create_context: recv payload failed\n");
        return UINT16_MAX;
    }
    return ntohs(resp.payload.ctx_handle);
}

uint16_t reprl_create_context_remote(socket_t fd) {
    return send_create_context(fd);
}

static char* argv_to_whitespace_string(const char** argv) {
    size_t total_len = 0;
    size_t count = 0;
    for (const char** p = argv; *p; ++p) {
        total_len += strlen(*p) + 1; // +1 for space or null terminator
        ++count;
    }
    if (total_len == 0)
        return strdup(""); // Empty string if no args

    char* result = malloc(total_len);
    if (!result)
        return NULL;

    char* dst = result;
    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(argv[i]);
        memcpy(dst, argv[i], len);
        dst += len;
        if (i != count - 1) {
            *dst++ = ' ';
        }
    }
    *dst = '\0';
    return result;
}

static char* envp_to_semicolon_string(const char** envp) {
    size_t total_len = 0;
    size_t count = 0;

    for (const char** p = envp; *p; ++p) {
        total_len += strlen(*p) + 1; // +1 for semicolon or null terminator
        ++count;
    }

    if (total_len == 0)
        return strdup(""); // Empty string if no env

    char* result = malloc(total_len);
    if (!result)
        return NULL;

    char* dst = result;
    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(envp[i]);
        memcpy(dst, envp[i], len);
        dst += len;
        if (i != count - 1) {
            *dst++ = ';';
        }
    }
    *dst = '\0';
    return result;
}

static uint8_t send_init_context(socket_t fd, uint16_t handle, const char **argv, const char **envp, int capture_stdout, int capture_stderr) {
    struct {
        cmd_pkt_t hdr;
        reprl_init_context_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_INIT_CONTEXT;
    cmd.hdr.length = htonl(sizeof(reprl_init_context_cp));
    cmd.payload.ctx_handle = htons(handle);

    // encoding argv to a single string
    char* joined_argv = argv_to_whitespace_string(argv);
    if (!joined_argv) {
        fprintf(stderr, "argv_to_whitespace_string failed\n");
        return -1;
    }
    strncpy(cmd.payload.argv, joined_argv, sizeof(cmd.payload.argv) - 1);
    cmd.payload.argv[sizeof(cmd.payload.argv) - 1] = '\0';
    free(joined_argv);

    // encoding envp to a single string
    char* joined_envp = envp_to_semicolon_string(envp);
    if (!joined_envp) {
        fprintf(stderr, "envp_to_semicolon_string failed\n");
        return -1;
    }
    strncpy(cmd.payload.envp, joined_envp, sizeof(cmd.payload.envp) - 1);
    cmd.payload.envp[sizeof(cmd.payload.envp) - 1] = '\0';
    free(joined_envp);

    cmd.payload.capture_stdout = capture_stdout;
    cmd.payload.capture_stderr = capture_stderr;
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_init_context: send failed\n");
        return -1;
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        reprl_init_context_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "send_init_context: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (REPRL_INIT_CONTEXT | RESP_MASK)) {
        fprintf(stderr, "send_init_context: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_init_context_rp)) {
        fprintf(stderr, "send_init_context: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "send_init_context: recv payload failed\n");
        return -1;
    }
    return resp.payload.status == 0 ? 0 : -1;
}

int reprl_initialize_context_remote(socket_t fd, uint16_t handle, const char** argv, const char** envp, int capture_stdout, int capture_stderr) {
    return send_init_context(fd, handle, argv, envp, capture_stdout, capture_stderr);
}

static void send_destroy_context(socket_t fd, uint16_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_destroy_context_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_DESTROY_CONTEXT;
    cmd.hdr.length = htonl(sizeof(cmd.payload));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_destroy_context: send failed\n");
        return;
    }
    // no response expected
}

void reprl_destroy_context_remote(socket_t fd, uint16_t handle) {
    send_destroy_context(fd, handle);
}

static uint32_t send_execute(socket_t fd, uint32_t handle, const char *script, uint64_t script_size, uint64_t timeout, uint8_t fresh_instance, uint64_t *execution_time) {
    uint32_t data_size = script_size + 1; // with null byte
    uint32_t total_size = data_size + sizeof(cmd_pkt_t) + sizeof(reprl_execute_cp);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return -1;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = REPRL_EXECUTE;
    hdr->length = htonl(sizeof(reprl_execute_cp) + data_size);
    reprl_execute_cp *payload = (reprl_execute_cp *)(packet + sizeof(cmd_pkt_t));
    payload->ctx_handle = htons(handle);
    payload->fresh_instance = fresh_instance;
    payload->timeout = htonll(timeout);
    payload->script_size = htonll(script_size);
    memcpy(payload->script, script, script_size);
    payload->script[script_size] = '\0';
    if (socket_send_all_remote(fd, packet, total_size) != total_size) {
        fprintf(stderr, "send_execute: send failed\n");
        return -1;
    }
    free(packet);

    // recv
    struct {
        cmd_pkt_t hdr;
        reprl_execute_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "send_execute: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (REPRL_EXECUTE | RESP_MASK)) {
        fprintf(stderr, "send_execute: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_execute_rp)) {
        fprintf(stderr, "send_execute: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "send_init_context: recv payload failed\n");
        return -1;
    }
    // XXX this is how fuzzilli reports back the execution time
    *execution_time = ntohll(resp.payload.execution_time);
    return ntohl(resp.payload.status);
}

int reprl_execute_remote(socket_t fd, uint16_t handle, const char* script, uint64_t script_size, uint64_t timeout, uint64_t* execution_time, int fresh_instance) {
    return send_execute(fd, handle, script, script_size, timeout, fresh_instance, execution_time);
}

static char* send_fetch_fuzzout(socket_t fd, uint32_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_fetch_fuzzout_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_FUZZOUT;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_fuzzout_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_fetch_fuzzout: send failed\n");
        return NULL;
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "send_fetch_fuzzout: recv hdr failed\n");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_FUZZOUT | RESP_MASK)) {
        fprintf(stderr, "send_fetch_fuzzout: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_fuzzout_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "send_fetch_fuzzout: invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_fetch_stdout(socket_t fd, uint32_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_fetch_stdout_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_STDOUT;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_stdout_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_fetch_stdout: send failed\n");
        return NULL;
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "send_fetch_stdout: recv hdr failed\n");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_STDOUT | RESP_MASK)) {
        fprintf(stderr, "send_fetch_stdout: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_stdout_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "send_fetch_stdout: invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_fetch_stderr(socket_t fd, uint32_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_fetch_stderr_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_STDERR;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_stderr_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_fetch_stderr: send failed\n");
        return NULL;
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "send_fetch_stderr: recv hdr failed\n");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_STDERR | RESP_MASK)) {
        fprintf(stderr, "send_fetch_stderr: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_stderr_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "send_fetch_stderr: invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_get_last_error(socket_t fd, uint32_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_get_last_error_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_GET_LAST_ERROR;
    cmd.hdr.length = htonl(sizeof(reprl_get_last_error_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "send_get_last_error: send failed\n");
        return NULL;
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "send_get_last_error: recv hdr failed\n");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_GET_LAST_ERROR | RESP_MASK)) {
        fprintf(stderr, "send_get_last_error: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_get_last_error_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "send_get_last_error: invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    if (data_size == 0) {
        return NULL;
    }
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

const char* reprl_fetch_stdout_remote(socket_t fd, uint16_t handle) {
    return send_fetch_stdout(fd, handle);
}

const char* reprl_fetch_stderr_remote(socket_t fd, uint16_t handle) {
    return send_fetch_stderr(fd, handle);
}

const char* reprl_fetch_fuzzout_remote(socket_t fd, uint16_t handle) {
    return send_fetch_fuzzout(fd, handle);
}

const char* reprl_get_last_error_remote(socket_t fd, uint16_t handle) {
    return send_get_last_error(fd, handle);
}

#endif
