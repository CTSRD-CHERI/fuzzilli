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

#include "remote-executor-protocol.h"
#include "reprl_layer.h"
#include "libreprl.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

static void send_reprl_create_context_resp(socket_t fd, uint16_t handle) {
    struct {
        cmd_pkt_t hdr;
        reprl_create_context_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_CREATE_CONTEXT | RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_create_context_rp));
    resp.payload = (reprl_create_context_rp){ .ctx_handle = htons(handle) };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_reprl_create_context(client_state_t *client) {
    // No command parameters
    struct reprl_context *ctx = reprl_create_context();
    if (ctx == NULL) {
        send_reprl_create_context_resp(client->client_fd, (uint16_t)-1);
        return;
    }
    uint16_t handle = insert_object(client->reprl_ctx_map, ctx); // concurrency?
    send_reprl_create_context_resp(client->client_fd, handle);
}

static char** split_whitespace_to_argv(char* input) {
    size_t count = 0;
    char* p = input;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (*p) {
            ++count;
            while (*p && !isspace((unsigned char)*p)) ++p;
        }
    }
    char** result = malloc((count + 1) * sizeof(char*));
    if (!result) return NULL;

    size_t i = 0;
    p = input;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (*p) {
            result[i++] = p;
            while (*p && !isspace((unsigned char)*p)) ++p;
            if (*p) *p++ = '\0';
        }
    }
    result[i] = NULL;
    return result;
}

static char** split_semicolons_to_envp(char *input) {
    char **envp = malloc(1 * sizeof(char *));
    if (!envp) return NULL;

    size_t envc = 0;
    char *p = strtok(input, ";");
    while (p) {
        envp = realloc(envp, sizeof(char*) * (envc + 2));
        envp[envc++] = p;
        p = strtok(NULL, ";");
    }
    envp[envc] = NULL;
    return envp;
}

static void send_reprl_init_context_resp(socket_t fd, uint8_t status) {
    struct {
        cmd_pkt_t hdr;
        reprl_init_context_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_INIT_CONTEXT | RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_init_context_rp));
    resp.payload = (reprl_init_context_rp){ .status = status };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_reprl_init_context(client_state_t *client, uint8_t* payload) {
    reprl_init_context_cp *cp = (reprl_init_context_cp *)payload;
    // extract the ctx
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        send_reprl_init_context_resp(client->client_fd, (uint8_t)-1);
        return;
    }
    // argv: split on whitespace
    char *argv_buf = strdup(cp->argv);
    char **argv = split_whitespace_to_argv(argv_buf);
    // envp: split on semicolons
    char *envp_buf = strdup(cp->envp);
    char **envp = split_semicolons_to_envp(envp_buf);
    if (!argv || !envp) {
        fprintf(stderr, "Couldn't allocate memory for argv and envp\n");
        send_reprl_init_context_resp(client->client_fd, (uint8_t)-1);
        return;
    }

    int cap_stdout = cp->capture_stdout;
    int cap_stderr = cp->capture_stderr;

    uint8_t status = (uint8_t)reprl_initialize_context(ctx, (const char**)argv, (const char**)envp, cap_stdout, cap_stderr);
    send_reprl_init_context_resp(client->client_fd, status);
}

void process_reprl_destroy_context(client_state_t *client, uint8_t *payload) {
    reprl_init_context_cp *cp = (reprl_init_context_cp *)payload;
    struct reprl_context *ctx = (struct reprl_context*)remove_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    reprl_destroy_context(ctx);
}

static void send_reprl_execute_resp(socket_t fd, int status, uint64_t execution_time) {
    struct {
        cmd_pkt_t hdr;
        reprl_execute_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_EXECUTE | RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_execute_rp));
    resp.payload = (reprl_execute_rp){ .status = htonl((uint32_t)status),
                                       .execution_time = htonll(execution_time) };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_reprl_execute(client_state_t *client, uint8_t* payload) {
    reprl_execute_cp *cp = (reprl_execute_cp *)payload;
    uint64_t script_size = ntohll(cp->script_size);
    char *script = malloc(script_size + 1); // consider NULL byte
    memcpy(script, cp->script, script_size);
    script[script_size] = '\0';
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint64_t timeout = ntohll(cp->timeout);
    uint8_t fresh_instance = cp->fresh_instance;
    uint64_t execution_time;

    int status = reprl_execute(ctx, script, script_size, timeout, &execution_time, fresh_instance);

    send_reprl_execute_resp(client->client_fd, status, execution_time);
}

static void send_reprl_fetch_fuzzout_resp(socket_t fd, const char *data) {
    uint32_t data_size = strlen(data) + 1; // plus NULL byte
    uint32_t total_size = data_size + sizeof(reprl_fetch_fuzzout_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_FUZZOUT | RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_fuzzout_rp) + data_size);

    reprl_fetch_fuzzout_rp *payload = (reprl_fetch_fuzzout_rp *)(packet + sizeof(cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_reprl_fetch_fuzzout(client_state_t *client, uint8_t* payload) {
    reprl_fetch_fuzzout_cp *cp = (reprl_fetch_fuzzout_cp *)payload;
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_fetch_fuzzout(ctx);
    send_reprl_fetch_fuzzout_resp(client->client_fd, data);
}

static void send_reprl_fetch_stdout_resp(socket_t fd, const char *data) {
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_fetch_stdout_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_STDOUT | RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_stdout_rp) + data_size);

    reprl_fetch_stdout_rp *payload = (reprl_fetch_stdout_rp *)(packet + sizeof(cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_reprl_fetch_stdout(client_state_t *client, uint8_t* payload) {
    reprl_fetch_stdout_cp *cp = (reprl_fetch_stdout_cp *)payload;
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_fetch_stdout(ctx);
    send_reprl_fetch_stdout_resp(client->client_fd, data);
}

static void send_reprl_fetch_stderr_resp(socket_t fd, const char *data) {
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_fetch_stderr_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_STDERR | RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_stderr_rp) + data_size);

    reprl_fetch_stderr_rp *payload = (reprl_fetch_stderr_rp *)(packet + sizeof(cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_reprl_fetch_stderr(client_state_t *client, uint8_t* payload) {
    reprl_fetch_stderr_cp *cp = (reprl_fetch_stderr_cp *)payload;
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_fetch_stderr(ctx);
    send_reprl_fetch_stderr_resp(client->client_fd, data);
}

static void send_reprl_get_last_error_resp(socket_t fd, const char *data) {
    if (data == NULL) {
        // there are no errors
        uint32_t total_size = sizeof(reprl_get_last_error_rp) + sizeof(cmd_pkt_t);
        uint8_t *packet = malloc(total_size);
        if (!packet) {
            perror("malloc");
            return;
        }
        cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
        hdr->opcode = REPRL_GET_LAST_ERROR | RESP_MASK;
        hdr->length = htonl(sizeof(reprl_get_last_error_rp));
        reprl_get_last_error_rp *payload = (reprl_get_last_error_rp *)(packet + sizeof(cmd_pkt_t));
        payload->data_size = 0;
        // no data
        socket_send_all_remote(fd, packet, total_size);
        free(packet);
        return;
    }
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_get_last_error_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = REPRL_GET_LAST_ERROR | RESP_MASK;
    hdr->length = htonl(sizeof(reprl_get_last_error_rp) + data_size);

    reprl_get_last_error_rp *payload = (reprl_get_last_error_rp *)(packet + sizeof(cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_reprl_get_last_error(client_state_t *client, uint8_t* payload) {
    reprl_get_last_error_cp *cp = (reprl_get_last_error_cp *)payload;
    struct reprl_context *ctx = (struct reprl_context*)get_object(client->reprl_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_get_last_error(ctx);
    send_reprl_get_last_error_resp(client->client_fd, data);
}
