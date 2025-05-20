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

#include "libsocket.h"
#include "remote-executor-protocol.h"

#include <sys/endian.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t ntohll(uint64_t value) {
    return be64toh(value);
}

uint64_t htonll(uint64_t value) {
    return htobe64(value);
}

static uint16_t send_create_context(socket_t fd) {
    // send
    struct {
        reprl_cmd_pkt_t hdr;
    } cmd;
    cmd.hdr.opcode = REPRL_CREATE_CONTEXT;
    cmd.hdr.length = htonl(0);
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_create_context: send failed");
        return UINT16_MAX;
    }

    // recv
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_create_context_rp payload;
    } resp;
    if (socket_recv_all(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        perror("send_create_context: recv hdr failed");
        return UINT16_MAX;
    }
    if (resp.hdr.opcode != (REPRL_CREATE_CONTEXT | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return UINT16_MAX;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_create_context_rp)) {
        fprintf(stderr, "Invalid payload size: %u\n", payload_len);
        return UINT16_MAX;
    }
    if (socket_recv_all(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        perror("send_create_context: recv payload failed");
        return UINT16_MAX;
    }
    return ntohs(resp.payload.ctx_handle);
}

static void send_destroy_context(socket_t fd, uint16_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_destroy_context_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_DESTROY_CONTEXT;
    cmd.hdr.length = htonl(sizeof(cmd.payload));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all(fd, (uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_destroy_context: send failed");
        return;
    }
    // no response expected
}

static char* argv_to_whitespace_string(char** argv) {
    size_t total_len = 0;
    size_t count = 0;
    for (char** p = argv; *p; ++p) {
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

static char* envp_to_semicolon_string(char** envp) {
    size_t total_len = 0;
    size_t count = 0;

    for (char** p = envp; *p; ++p) {
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

uint8_t send_init_context(socket_t fd, uint16_t handle, const char **argv, const char **envp, int capture_stdout, int capture_stderr) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_init_context_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_INIT_CONTEXT;
    cmd.hdr.length = htonl(sizeof(reprl_init_context_cp));
    cmd.payload.ctx_handle = htons(handle);

    // encoding argv to a single string
    char* joined_argv = argv_to_whitespace_string(argv);
    if (!joined_argv) {
        perror("argv_to_whitespace_string failed");
        return -1;
    }
    strncpy(cmd.payload.argv, joined_argv, sizeof(cmd.payload.argv) - 1);
    cmd.payload.argv[sizeof(cmd.payload.argv) - 1] = '\0';
    free(joined_argv);

    // encoding envp to a single string
    char* joined_envp = envp_to_semicolon_string(envp);
    if (!joined_envp) {
        perror("envp_to_semicolon_string failed");
        return -1;
    }
    strncpy(cmd.payload.envp, joined_envp, sizeof(cmd.payload.envp) - 1);
    cmd.payload.envp[sizeof(cmd.payload.envp) - 1] = '\0';
    free(joined_envp);

    cmd.payload.capture_stdout = capture_stdout;
    cmd.payload.capture_stderr = capture_stderr;
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_init_context: send failed");
        return -1;
    }

    // recv
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_init_context_rp payload;
    } resp;
    if (socket_recv_all(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        perror("send_init_context: recv hdr failed");
        return -1;
    }
    if (resp.hdr.opcode != (REPRL_INIT_CONTEXT | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_init_context_rp)) {
        fprintf(stderr, "Invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        perror("send_init_context: recv payload failed");
        return -1;
    }
    return resp.payload.status;
}

static uint32_t send_execute(socket_t fd, uint32_t handle, char *script, uint64_t script_size, uint64_t timeout, uint8_t fresh_instance, uint64_t *execution_time) {
    uint32_t data_size = script_size + 1; // with null byte
    uint32_t total_size = data_size + sizeof(reprl_cmd_pkt_t) + sizeof(reprl_execute_cp);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc failed");
        return -1;
    }
    reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
    hdr->opcode = REPRL_EXECUTE;
    hdr->length = htonl(sizeof(reprl_execute_cp) + data_size);
    reprl_execute_cp *payload = (reprl_execute_cp *)(packet + sizeof(reprl_cmd_pkt_t));
    payload->ctx_handle = htons(handle);
    payload->fresh_instance = fresh_instance;
    payload->timeout = htonll(timeout);
    payload->script_size = htonll(script_size);
    memcpy(payload->script, script, script_size);
    payload->script[script_size] = '\0';
    if (socket_send_all(fd, packet, total_size) != total_size) {
        perror("send_execute: send failed");
        return -1;
    }
    free(packet);

    // recv
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_execute_rp payload;
    } resp;
    if (socket_recv_all(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        perror("send_execute: recv hdr failed");
        return -1;
    }
    if (resp.hdr.opcode != (REPRL_EXECUTE | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(reprl_execute_rp)) {
        fprintf(stderr, "Invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        perror("send_init_context: recv payload failed");
        return -1;
    }
    // XXX this is how fuzzilli reports back the execution time
    *execution_time = resp.payload.execution_time;
    return resp.payload.status;
}

static char* send_fetch_fuzzout(socket_t fd, uint32_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_fetch_fuzzout_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_FUZZOUT;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_fuzzout_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_fetch_fuzzout: send failed");
        return NULL;
    }

    // recv
    reprl_cmd_pkt_t resp_hdr;
    if (socket_recv_all(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        perror("send_fetch_fuzzout: recv hdr failed");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_FUZZOUT | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_fuzzout_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "Invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_fetch_stdout(socket_t fd, uint32_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_fetch_stdout_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_STDOUT;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_stdout_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_fetch_stdout: send failed");
        return NULL;
    }

    // recv
    reprl_cmd_pkt_t resp_hdr;
    if (socket_recv_all(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        perror("send_fetch_stdout: recv hdr failed");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_STDOUT | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_stdout_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "Invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_fetch_stderr(socket_t fd, uint32_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_fetch_stderr_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_FETCH_STDERR;
    cmd.hdr.length = htonl(sizeof(reprl_fetch_stderr_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_fetch_stderr: send failed");
        return NULL;
    }

    // recv
    reprl_cmd_pkt_t resp_hdr;
    if (socket_recv_all(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        perror("send_fetch_stderr: recv hdr failed");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_FETCH_STDERR | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_fetch_stderr_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "Invalid response payload size: %u\n", resp_len);
        return NULL;
    }
    uint32_t data_size = ntohl(resp_pkt->data_size);
    char* data = malloc(data_size);
    memcpy(data, resp_pkt->data, data_size);
    return data;
}

static char* send_get_last_error(socket_t fd, uint32_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_get_last_error_cp payload;
    } cmd;
    cmd.hdr.opcode = REPRL_GET_LAST_ERROR;
    cmd.hdr.length = htonl(sizeof(reprl_get_last_error_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("send_get_last_error: send failed");
        return NULL;
    }

    // recv
    reprl_cmd_pkt_t resp_hdr;
    if (socket_recv_all(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        perror("send_get_last_error: recv hdr failed");
        return NULL;
    }
    if (resp_hdr.opcode != (REPRL_GET_LAST_ERROR | REPRL_RESP_MASK)) {
        fprintf(stderr, "Unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return NULL;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    reprl_get_last_error_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "Invalid response payload size: %u\n", resp_len);
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

static void parse_ip_port(const char *arg, char *ip, size_t ip_len, uint16_t *port) {
    const char *colon = strchr(arg, ':');
    if (!colon) {
        fprintf(stderr, "Invalid format. Expected ip:port\n");
        exit(EXIT_FAILURE);
    }

    size_t ip_part_len = colon - arg;
    if (ip_part_len >= ip_len) {
        fprintf(stderr, "IP too long\n");
        exit(EXIT_FAILURE);
    }

    strncpy(ip, arg, ip_part_len);
    ip[ip_part_len] = '\0';
    *port = (uint16_t)atoi(colon + 1);
    if (*port == 0) {
        fprintf(stderr, "Invalid port\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    char ip[64] = {0};
    uint16_t port = 0;
    const char *js_shell_path = NULL;
    
    if (argc < 5) {
        fprintf(stderr, "usage: %s --connect ip:port --js-shell-path /path/to/js-shell\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--connect") == 0) {
            parse_ip_port(argv[i + 1], ip, sizeof(ip), &port);
            i++; // skip next arg
        } else if (strcmp(argv[i], "--js-shell-path") == 0) {
            js_shell_path = argv[i + 1];
            i++;
        }
    }

    if (!*ip || !port || !js_shell_path) {
        fprintf(stderr, "Missing required arguments.\n");
        exit(EXIT_FAILURE);
    }

    // create context
    socket_t fd = socket_connect(ip, port);
    uint16_t handle = send_create_context(fd);
    printf("Handle = %u\n", handle);
    if (handle == UINT16_MAX) {
        printf("[x] Invalid handle\n");
        return -1;
    }

    // init context
    char *jsShellArgs[] = { (char*)js_shell_path, NULL };
    char *jsShellEnv[] = { NULL };
    uint8_t status = send_init_context(fd, handle, jsShellArgs, jsShellEnv, 1, 1);
    if (status != 0) {
        printf("[x] Init context failed\n");
        return -1;
    }

    // execute script
    char *script = "throw 42";
    uint64_t script_size = strlen(script);
    uint64_t execution_time;
    uint32_t execution_status = send_execute(fd, handle, script, script_size, 1000000, 0, &execution_time);
    if (execution_status == (uint32_t)-1) {
        printf("[x] Failed execution\n");
        return -1;
    }

    // fetch fuzzout
    char* fuzzout = send_fetch_fuzzout(fd, handle);
    if (fuzzout == NULL) {
        printf("[x] Failed fetching fuzzout\n");
        return -1;
    }
    printf("fuzzout: %s\n", fuzzout);
    // fetch stdout
    char *stdout_out = send_fetch_stdout(fd, handle);
    if (stdout_out == NULL) {
        printf("[x] Failed fetching stdout\n");
        return -1;
    }
    printf("stdout: %s\n", stdout_out);

    // fetch stderr
    char *stderr_out = send_fetch_stderr(fd, handle);
    if (stderr_out == NULL) {
        printf("[x] Failed fetching stderr\n");
        return -1;
    }
    printf("stderr: %s\n", stderr_out);

    // get last error 
    char *last_error = send_get_last_error(fd, handle);
    printf("last_error: %s\n", last_error);

    free(fuzzout);
    free(stdout_out);
    free(stderr_out);
    free(last_error);

    // destroy context
    send_destroy_context(fd, handle);

    return 0;
}