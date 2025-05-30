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

#include "executor.h"
#include "remote-executor-protocol.h"
#include "reprl_layer.h"
#include "coverage_layer.h"

#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <arpa/inet.h>

void init_context_map(context_map *map) {
    map->capacity = INITIAL_CONTEXT_MAP_CAPACITY;
    map->objects = calloc(map->capacity, sizeof(void *));
}

void destroy_context_map(context_map *map) {
    free(map->objects);
}

static void ensure_capacity(context_map *map, int id) {
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

// XXXR3 WARNING! the returned int should fit in a uint16_t handle without overflowing
int insert_object(context_map *map, void *obj) {
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

void* get_object(context_map *map, int id) {
    if (id < 0 || id >= map->capacity) return NULL;
    return map->objects[id];
}

void* remove_object(context_map *map, int id) {
    if (id < 0 || id >= map->capacity) return NULL;
    void* removed = map->objects[id];
    map->objects[id] = NULL;
    return removed;
}

static void* client_handler(void* arg) {
    client_state_t *client = (client_state_t *)arg;
    socket_t fd = client->client_fd;
    uint8_t *buffer;

    while (1) {
        // XXX: the datatypes must match the datatypes in cmd_pkt_t
        uint8_t opcode;
        uint32_t len;
        if (socket_recv_all_remote(fd, (uint8_t*)&opcode, sizeof(opcode)) != sizeof(opcode)) break;
        if (socket_recv_all_remote(fd, (uint8_t*)&len, sizeof(len)) != sizeof(len)) break;
        len = ntohl(len);
        buffer = malloc(len);
        if (socket_recv_all_remote(fd, buffer, len) != len) break;

        switch (opcode) {
        case REPRL_CREATE_CONTEXT:
            printf("[DEBUG] REPRL_CREATE_CONTEXT\n");
            process_reprl_create_context(client);
            break;
        case REPRL_INIT_CONTEXT:
            printf("[DEBUG] REPRL_INIT_CONTEXT\n");
            process_reprl_init_context(client, buffer);
            break;
        case REPRL_DESTROY_CONTEXT:
            printf("[DEBUG] REPRL_DESTROY_CONTEXT\n");
            process_reprl_destroy_context(client, buffer);
            break;
        case REPRL_EXECUTE:
            printf("[DEBUG] REPRL_EXECUTE\n");
            process_reprl_execute(client, buffer);
            break;
        case REPRL_FETCH_FUZZOUT:
            printf("[DEBUG] REPRL_FETCH_FUZZOUT\n");
            process_reprl_fetch_fuzzout(client, buffer);
            break;
        case REPRL_FETCH_STDOUT:
            printf("[DEBUG] REPRL_FETCH_STDOUT\n");
            process_reprl_fetch_stdout(client, buffer);
            break;
        case REPRL_FETCH_STDERR:
            printf("[DEBUG] REPRL_FETCH_STDERR\n");
            process_reprl_fetch_stderr(client, buffer);
            break;
        case REPRL_GET_LAST_ERROR:
            printf("[DEBUG] REPRL_GET_LAST_ERROR\n");
            process_reprl_get_last_error(client, buffer);
            break;
        case COV_CREATE_CONTEXT:
            printf("[DEBUG] COV_CREATE_CONTEXT\n");
            process_cov_create_context(client);
            break;
        case COV_INIT:
            printf("[DEBUG] COV_INIT\n");
            process_cov_init(client, buffer);
            break;
        case COV_FINISH_INIT:
            printf("[DEBUG] COV_FINISH_INIT\n");
            process_cov_finish_init(client, buffer);
            break;
        case COV_SHUTDOWN:
            printf("[DEBUG] COV_SHUTDOWN\n");
            process_cov_shutdown(client, buffer);
            break;
        case COV_EVAL:
            printf("[DEBUG] COV_EVAL\n");
            process_cov_eval(client, buffer);
            break;
        case COV_EVAL_CRASH:
            printf("[DEBUG] COV_EVAL_CRASH\n");
            process_cov_eval_crash(client, buffer);
            break;
        case COV_COMPARE_EQUAL:
            printf("[DEBUG] COV_COMPARE_EQUAL\n");
            process_cov_compare_equal(client, buffer);
            break;
        case COV_CLEAR_BITMAP:
            printf("[DEBUG] COV_CLEAR_BITMAP\n");
            process_cov_clear_bitmap(client, buffer);
            break;
        case COV_GET_EDGE_COUNTS:
            printf("[DEBUG] COV_GET_EDGE_COUNTS\n");
            process_cov_get_edge_counts(client, buffer);
            break;
        case COV_CLEAR_EDGE_DATA:
            printf("[DEBUG] COV_CLEAR_EDGE_DATA\n");
            process_cov_clear_edge_data(client, buffer);
            break;
        case COV_RESET_STATE:
            printf("[DEBUG] COV_RESET_STATE\n");
            process_cov_reset_state(client, buffer);
            break;
        case COV_GET_VIRGIN_BITS:
            printf("[DEBUG] COV_GET_VIRGIN_BITS\n");
            process_cov_get_virgin_bits(client, buffer);
            break;
        case COV_GET_CRASH_BITS:
            printf("[DEBUG] COV_GET_CRASH_BITS\n");
            process_cov_get_crash_bits(client, buffer);
            break;
        case COV_SET_CRASH_BITS:
            printf("[DEBUG] COV_SET_CRASH_BITS\n");
            process_cov_set_crash_bits(client, buffer);
            break;
        case COV_SET_VIRGIN_BITS:
            printf("[DEBUG] COV_SET_VIRGIN_BITS\n");
            process_cov_set_virgin_bits(client, buffer);
            break;
        case COV_BULK_CLEAR_EDGE_DATA:
            printf("[DEBUG] COV_BULK_CLEAR_EDGE_DATA\n");
            process_cov_bulk_clear_edge_data(client, buffer);
            break;
        default:
            fprintf(stderr, "[-] Invalid opcode: %u\n", opcode);
        }
    }

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

    // Initialise a map to track REPRL and coverage context objects with handles
    context_map reprl_ctx_map;
    init_context_map(&reprl_ctx_map);
    context_map cov_ctx_map;
    init_context_map(&cov_ctx_map);

    socket_t listener = socket_listen_remote(ip, port);
    if (listener < 0) {
        perror("socket_listen");
        return EXIT_FAILURE;
    }

    printf("[+] Listening on %s:%d\n", ip, port);

    while (1) {
        socket_t client_fd = socket_accept_remote(listener);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        client_state_t *state = malloc(sizeof(client_state_t));
        state->client_fd = client_fd;
        state->reprl_ctx_map = &reprl_ctx_map;
        state->cov_ctx_map = &cov_ctx_map;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, state) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(state);
            continue;
        }

        pthread_detach(tid);
    }

    socket_close_remote(listener);
    destroy_context_map(&reprl_ctx_map);
    destroy_context_map(&cov_ctx_map);

    return 0;
}
