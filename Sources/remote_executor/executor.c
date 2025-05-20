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

#include "libreprl.h"
#include "libsocket.h"
#include "remote-executor-protocol.h"

#include <cstdint>
#include <sys/endian.h>
#include <netinet/in.h>

#include <ctype.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_CLIENTS 16
#define INITIAL_REPRL_CONTEXT_MAP_CAPACITY 16

uint64_t ntohll(uint64_t value) {
    return be64toh(value);
}

uint64_t htonll(uint64_t value) {
    return htobe64(value);
}

// How do we keep track of the REPRL context?
// In Fuzzilli/Execution, the context is passed to these C functions as an
// OpaquePointer, so we can convert it into a descriptor number that is
// referenced in command packets.
// We can assume that one executor, defined by (host, port), can keep track of
// multiple REPRL contexts.

typedef struct {
    struct reprl_context **objects;
    size_t capacity;
} reprl_context_map;

static void init_context_map(reprl_context_map *map) {
    map->capacity = INITIAL_REPRL_CONTEXT_MAP_CAPACITY;
    map->objects = calloc(map->capacity, sizeof(struct reprl_context *));
}

static void ensure_capacity(reprl_context_map *map, int id) {
    if (id >= map->capacity) {
        size_t new_capacity = map->capacity;
        while (new_capacity <= id) {
            new_capacity *= 2;
        }
        map->objects = realloc(map->objects, new_capacity * sizeof(struct reprl_context *));
        for (size_t i = map->capacity; i < new_capacity; ++i) {
            map->objects[i] = NULL;
        }
        map->capacity = new_capacity;
    }
}

static int insert_object(reprl_context_map *map, void *obj) {
    for (int i = 0; i < map->capacity; ++i) {
        if (map->objects[i] == NULL) {
            map->objects[i] = obj;
            return i;
        }
    }
    ensure_capacity(map, map->capacity);
    map->objects[map->capacity / 2] = obj;
    return map->capacity / 2;
}

static struct reprl_context *get_object(reprl_context_map *map, int id) {
    if (id < 0 || id >= map->capacity) return NULL;
    return map->objects[id];
}

static struct reprl_context* remove_object(reprl_context_map *map, int id) {
    if (id < 0 || id >= map->capacity) return NULL;
    struct reprl_context* removed = map->objects[id];
    map->objects[id] = NULL;
    return removed;
}

static void destroy_context_map(reprl_context_map *map) {
    free(map->objects);
}

// Client state object, containing arguments for each
// remote executor thread handler
typedef struct {
    socket_t client_fd;
    reprl_context_map* map;
} client_state_t;

///// packet handlers

static void send_reprl_create_context_resp(socket_t fd, uint16_t handle) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_create_context_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_CREATE_CONTEXT | REPRL_RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_create_context_rp));
    resp.payload = (reprl_create_context_rp){ .ctx_handle = htons(handle) };
    socket_send_all(fd, (const uint8_t*)&resp, sizeof(resp));
}

static void process_reprl_create_context(client_state_t *client) {
    // No command parameters
    struct reprl_context *ctx = reprl_create_context();
    if (ctx == NULL) {
        send_reprl_create_context_resp(client->client_fd, (uint16_t)-1);
        return;
    }
    uint16_t handle = insert_object(client->map, ctx); // concurrency?
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
        reprl_cmd_pkt_t hdr;
        reprl_init_context_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_INIT_CONTEXT | REPRL_RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_init_context_rp));
    resp.payload = (reprl_init_context_rp){ .status = status };
    socket_send_all(fd, (const uint8_t*)&resp, sizeof(resp));
}

static void process_reprl_init_context(client_state_t *client, uint8_t* payload) {
    reprl_init_context_cp *cp = (reprl_init_context_cp *)payload;
    // extract the ctx
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
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

static void process_reprl_destroy_context(client_state_t *client, uint8_t *payload) {
    reprl_init_context_cp *cp = (reprl_init_context_cp *)payload;
    struct reprl_context *ctx = remove_object(client->map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    reprl_destroy_context(ctx);
}

static void send_reprl_execute_resp(socket_t fd, int status, uint64_t execution_time) {
    struct {
        reprl_cmd_pkt_t hdr;
        reprl_execute_rp payload;
    } resp;
    resp.hdr.opcode = REPRL_EXECUTE | REPRL_RESP_MASK;
    resp.hdr.length = htonl(sizeof(reprl_execute_rp));
    resp.payload = (reprl_execute_rp){ .status = htonl((uint32_t)status),
                                       .execution_time = htonll(execution_time) };
    socket_send_all(fd, (const uint8_t*)&resp, sizeof(resp));
}

static void process_reprl_execute(client_state_t *client, uint8_t* payload) {
    reprl_execute_cp *cp = (reprl_execute_cp *)payload;
    uint64_t script_size = ntohll(cp->script_size);
    char *script = malloc(script_size + 1); // consider NULL byte
    memcpy(script, cp->script, script_size);
    script[script_size] = '\0';
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
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
    uint32_t total_size = data_size + sizeof(reprl_fetch_fuzzout_rp) + sizeof(reprl_cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc failed");
        return;
    }
    reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_FUZZOUT | REPRL_RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_fuzzout_rp) + data_size);

    reprl_fetch_fuzzout_rp *payload = (reprl_fetch_fuzzout_rp *)(packet + sizeof(reprl_cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all(fd, packet, total_size);
    free(packet);
}

static void process_reprl_fetch_fuzzout(client_state_t *client, uint8_t* payload) {
    reprl_fetch_fuzzout_cp *cp = (reprl_fetch_fuzzout_cp *)payload;
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_fetch_fuzzout(ctx);
    send_reprl_fetch_fuzzout_resp(client->client_fd, data);
}

static void send_reprl_fetch_stdout_resp(socket_t fd, const char *data) {
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_fetch_stdout_rp) + sizeof(reprl_cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc failed");
        return;
    }
    reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_STDOUT | REPRL_RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_stdout_rp) + data_size);

    reprl_fetch_stdout_rp *payload = (reprl_fetch_stdout_rp *)(packet + sizeof(reprl_cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all(fd, packet, total_size);
    free(packet);
}

static void process_reprl_fetch_stdout(client_state_t *client, uint8_t* payload) {
    reprl_fetch_stdout_cp *cp = (reprl_fetch_stdout_cp *)payload;
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_fetch_stdout(ctx);
    send_reprl_fetch_stdout_resp(client->client_fd, data);
}

static void send_reprl_fetch_stderr_resp(socket_t fd, const char *data) {
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_fetch_stderr_rp) + sizeof(reprl_cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc failed");
        return;
    }
    reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
    hdr->opcode = REPRL_FETCH_STDERR | REPRL_RESP_MASK;
    hdr->length = htonl(sizeof(reprl_fetch_stderr_rp) + data_size);

    reprl_fetch_stderr_rp *payload = (reprl_fetch_stderr_rp *)(packet + sizeof(reprl_cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all(fd, packet, total_size);
    free(packet);
}

static void process_reprl_fetch_stderr(client_state_t *client, uint8_t* payload) {
    reprl_fetch_stderr_cp *cp = (reprl_fetch_stderr_cp *)payload;
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
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
        uint32_t total_size = sizeof(reprl_get_last_error_rp) + sizeof(reprl_cmd_pkt_t);
        uint8_t *packet = malloc(total_size);
        reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
        hdr->opcode = REPRL_GET_LAST_ERROR | REPRL_RESP_MASK;
        hdr->length = htonl(sizeof(reprl_get_last_error_rp));
        reprl_get_last_error_rp *payload = (reprl_get_last_error_rp *)(packet + sizeof(reprl_cmd_pkt_t));
        payload->data_size = 0;
        // no data
        socket_send_all(fd, packet, total_size);
        free(packet);
        return;
    }
    uint32_t data_size = strlen(data) + 1;
    uint32_t total_size = data_size + sizeof(reprl_get_last_error_rp) + sizeof(reprl_cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc failed");
        return;
    }
    reprl_cmd_pkt_t *hdr = (reprl_cmd_pkt_t *)packet;
    hdr->opcode = REPRL_GET_LAST_ERROR | REPRL_RESP_MASK;
    hdr->length = htonl(sizeof(reprl_get_last_error_rp) + data_size);

    reprl_get_last_error_rp *payload = (reprl_get_last_error_rp *)(packet + sizeof(reprl_cmd_pkt_t));
    payload->data_size = htonl(data_size);
    memcpy(payload->data, data, data_size);

    socket_send_all(fd, packet, total_size);
    free(packet);
}

static void process_reprl_get_last_error(client_state_t *client, uint8_t* payload) {
    reprl_get_last_error_cp *cp = (reprl_get_last_error_cp *)payload;
    struct reprl_context *ctx = get_object(client->map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    const char *data = reprl_get_last_error(ctx);
    send_reprl_get_last_error_resp(client->client_fd, data);
}

static void* client_handler(void* arg) {
    client_state_t *client = (client_state_t *)arg;
    socket_t fd = client->client_fd;
    uint8_t *buffer;

    while (1) {
        // XXX: the datatypes must match the datatypes in reprl_cmd_pkt_t
        uint8_t opcode;
        uint32_t len;
        if (socket_recv_all(fd, (uint8_t*)&opcode, sizeof(opcode)) != sizeof(opcode)) break;
        if (socket_recv_all(fd, (uint8_t*)&len, sizeof(len)) != sizeof(len)) break;
        len = ntohl(len);
        buffer = malloc(len);
        if (socket_recv_all(fd, buffer, len) != len) break;

        switch (opcode) {
        case REPRL_CREATE_CONTEXT:
            printf("[DEBUG] created context\n");
            process_reprl_create_context(client);
            break;
        case REPRL_INIT_CONTEXT:
            printf("[DEBUG] init context\n");
            process_reprl_init_context(client, buffer);
            break;
        case REPRL_DESTROY_CONTEXT:
            printf("[DEBUG] destroy context\n");
            process_reprl_destroy_context(client, buffer);
            break;
        case REPRL_EXECUTE:
            printf("[DEBUG] execute command\n");
            process_reprl_execute(client, buffer);
            break;
        case REPRL_FETCH_FUZZOUT:
            printf("[DEBUG] fetch fuzz\n");
            process_reprl_fetch_fuzzout(client, buffer);
            break;
        case REPRL_FETCH_STDOUT:
            printf("[DEBUG] fetch stdout\n");
            process_reprl_fetch_stdout(client, buffer);
            break;
        case REPRL_FETCH_STDERR:
            printf("[DEBUG] fetch stderr\n");
            process_reprl_fetch_stderr(client, buffer);
            break;
        case REPRL_GET_LAST_ERROR:
            printf("[DEBUG] get last error\n");
            process_reprl_get_last_error(client, buffer);
            break;
        default:
            fprintf(stderr, "[-] Invalid opcode: %u\n", opcode);
        }
    }

    printf("[-] Client disconnected\n");
    close(fd);
    free(client);
    return NULL;
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

// usage: ./executor ip:port
// for example, ./executor 0.0.0.0:1234
int main(int argc, char *argv[]) {
    char ip[64];
    uint16_t port;
    if (argc < 2) {
        printf("usage: ./executor ip:port");
        exit(EXIT_FAILURE);
    }
    parse_ip_port(argv[1], ip, sizeof(ip), &port);

    // Initialise a map to track REPRL context objects with handles
    reprl_context_map map;
    init_context_map(&map);

    // Listen for commands in a loop
    socket_t listener = socket_listen(ip, port);
    if (listener < 0) {
        perror("socket_listen");
        return EXIT_FAILURE;
    }

    printf("Listening on %s:%d\n", ip, port);

    while (1) {
        socket_t client_fd = socket_accept(listener);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        client_state_t *state = malloc(sizeof(client_state_t));
        state->client_fd = client_fd;
        state->map = &map;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, state) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(state);
            continue;
        }

        pthread_detach(tid);
    }

    socket_close(listener);
    destroy_context_map(&map);

    return 0;
}