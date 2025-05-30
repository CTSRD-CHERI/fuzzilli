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
#include "coverage_layer.h"
#include "libcoverage.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

static void send_cov_create_context_resp(socket_t fd, uint16_t handle) {
    struct {
        cmd_pkt_t hdr;
        cov_create_context_rp payload;
    } resp;
    resp.hdr.opcode = COV_CREATE_CONTEXT | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_create_context_rp));
    resp.payload = (cov_create_context_rp){ .ctx_handle = htons(handle) };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_create_context(client_state_t *client) {
    // No command parameters
    struct cov_context *ctx = (struct cov_context*)malloc(sizeof(struct cov_context));
    if (ctx == NULL) {
        send_cov_create_context_resp(client->client_fd, (uint16_t)-1);
        return;
    }
    uint16_t handle = insert_object(client->cov_ctx_map, ctx); // concurrency?
    send_cov_create_context_resp(client->client_fd, handle);
}

// XXXR3 TODO support for WIN32
static int cov_initialize_remote(struct cov_context* context, pid_t pid) {
#if defined(_WIN32)
    char key[1024];
    _snprintf(key, sizeof(key), "shm_id_%u_%u",
              pid, context->id);
    context->hMapping =
            CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                               SHM_SIZE, key);
    if (!context->hMapping) {
        fprintf(stderr, "[LibCoverage] unable to create file mapping: %lu",
                GetLastError());
        return -1;
    }

    context->shmem =
            MapViewOfFile(context->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    if (!context->shmem) {
        CloseHandle(context->hMapping);
        context->hMapping = INVALID_HANDLE_VALUE;
        return -1;
    }
#else
    char shm_key[1024];
    // XXXR3 we add a / to follow POSIX rules
    snprintf(shm_key, 1024, "/shm_id_%d_%d", pid, context->id);

    printf("shm_key: %s\n", shm_key);
    int fd = shm_open(shm_key, O_RDWR | O_CREAT, S_IREAD | S_IWRITE);
    if (fd <= -1) {
        fprintf(stderr, "[LibCoverage] Failed to create shared memory region\n");
        return -1;
    }
    ftruncate(fd, SHM_SIZE);
    context->shmem = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
#endif
    return 0;
}

static void send_cov_init_resp(socket_t fd, uint8_t status) {
    struct {
        cmd_pkt_t hdr;
        cov_init_rp payload;
    } resp;
    resp.hdr.opcode = COV_INIT | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_init_rp));
    resp.payload = (cov_init_rp){ .status = status };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_init(client_state_t *client, uint8_t *payload) {
    cov_init_cp *cp = (cov_init_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t pid = ntohl(cp->pid);
    uint32_t id = ntohl(cp->context_id);

    ctx->id = id;
    ctx->pid = pid;
    int result = cov_initialize_remote(ctx, pid);

    send_cov_init_resp(client->client_fd, result != 0);
}

static void send_cov_finish_init(socket_t fd, uint32_t num_edges, uint32_t bitmap_size, uint8_t should_track_edges) {
    struct {
        cmd_pkt_t hdr;
        cov_finish_init_rp payload;
    } resp;
    resp.hdr.opcode = COV_FINISH_INIT | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_finish_init_rp));
    resp.payload = (cov_finish_init_rp){ .num_edges = htonl(num_edges),
                                         .bitmap_size = htonl(bitmap_size),
                                         .should_track_edges = should_track_edges };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_finish_init(client_state_t *client, uint8_t *payload) {
    cov_finish_init_cp *cp = (cov_finish_init_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint8_t should_track_edges = cp->should_track_edges;

    cov_finish_initialization(ctx, should_track_edges);

    send_cov_finish_init(client->client_fd, ctx->num_edges, ctx->bitmap_size, ctx->should_track_edges);
}

// XXXR3 TODO support for WIN32
static void cov_shutdown_remote(struct cov_context* context)
{
#if defined(_WIN32)
    (void)UnmapViewOfFile(context->shmem);
    CloseHandle(context->hMapping);
#else
    char shm_key[1024];
    snprintf(shm_key, 1024, "/shm_id_%d_%d", context->pid, context->id);
    shm_unlink(shm_key);
#endif
}

void process_cov_shutdown(client_state_t *client, uint8_t *payload) {
    cov_shutdown_cp *cp = (cov_shutdown_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    
    cov_shutdown_remote(ctx);
    // no reply
}

static void send_cov_eval(socket_t fd, uint8_t status, uint32_t found_edges, struct edge_set *edges) {
    uint32_t data_size = edges->count * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cov_eval_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_EVAL | RESP_MASK;
    hdr->length = htonl(sizeof(cov_eval_rp) + data_size);

    cov_eval_rp *payload = (cov_eval_rp *)(packet + sizeof(cmd_pkt_t));
    payload->status = status;
    payload->found_edges = htonl(found_edges);
    payload->edge_count = htonl(edges->count);

    memcpy(payload->edge_indices, edges->edge_indices, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_cov_eval(client_state_t *client, uint8_t *payload) {
    cov_eval_cp *cp = (cov_eval_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    struct edge_set new_edges;
    int status = cov_evaluate(ctx, &new_edges);

    send_cov_eval(client->client_fd, status != 0, ctx->found_edges, &new_edges);
}

static void send_cov_eval_crash(socket_t fd, uint8_t status) {
    struct {
        cmd_pkt_t hdr;
        cov_eval_crash_rp payload;
    } resp;
    resp.hdr.opcode = COV_EVAL_CRASH | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_eval_crash_rp));
    resp.payload = (cov_eval_crash_rp){ .status = status };

    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_eval_crash(client_state_t *client, uint8_t *payload) {
    cov_eval_crash_cp *cp = (cov_eval_crash_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    int status = cov_evaluate_crash(ctx);

    send_cov_eval_crash(client->client_fd, status != 0);
}

static void send_cov_compare_equal(socket_t fd, uint8_t status) {
    struct {
        cmd_pkt_t hdr;
        cov_compare_equal_rp payload;
    } resp;
    resp.hdr.opcode = COV_COMPARE_EQUAL | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_compare_equal_rp));
    resp.payload = (cov_compare_equal_rp){ .status = status };

    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_compare_equal(client_state_t *client, uint8_t *payload) {
    cov_compare_equal_cp *cp = (cov_compare_equal_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t edge_count = ntohl(cp->edge_count);
    uint32_t *edge_indices = (uint32_t *)malloc(edge_count * sizeof(uint32_t));
    memcpy(edge_indices, cp->edge_indices, edge_count * sizeof(uint32_t));

    int status = cov_compare_equal(ctx, edge_indices, edge_count);

    send_cov_compare_equal(client->client_fd, status != 0);
}

void process_cov_clear_bitmap(client_state_t *client, uint8_t *payload) {
    cov_clear_bitmap_cp *cp = (cov_clear_bitmap_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    cov_clear_bitmap(ctx);
    // no reply
}

static void send_cov_get_edge_counts(socket_t fd, uint8_t status, struct edge_counts *edges) {
    uint64_t num_sparse_entries = 0;
    cov_sparse_edge_t *sparse_array = NULL;
    for (uint64_t i = 0; i < edges->count; i++) {
        if (edges->edge_hit_count[i] != 0) {
            num_sparse_entries++;
            sparse_array = realloc(sparse_array, num_sparse_entries * sizeof(cov_sparse_edge_t));
            sparse_array[num_sparse_entries - 1].index = i;
            sparse_array[num_sparse_entries - 1].index = edges->edge_hit_count[i];
        }
    }

    uint32_t data_size = num_sparse_entries * sizeof(cov_sparse_edge_t);
    uint32_t total_size = data_size + sizeof(cov_get_edge_counts_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_GET_EDGE_COUNTS | RESP_MASK;
    hdr->length = htonl(sizeof(cov_get_edge_counts_rp) + data_size);

    cov_get_edge_counts_rp *payload = (cov_get_edge_counts_rp *)(packet + sizeof(cmd_pkt_t));
    payload->status = status;
    payload->edge_count = htonl(edges->count);

    payload->edge_sparse_count = num_sparse_entries;
    cov_sparse_edge_t *data = (cov_sparse_edge_t *)(packet + sizeof(cmd_pkt_t) + sizeof(cov_get_edge_counts_rp));
    memcpy(data, sparse_array, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_cov_get_edge_counts(client_state_t *client, uint8_t *payload) {
    cov_get_edge_counts_cp *cp = (cov_get_edge_counts_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    struct edge_counts edges;
    int status = cov_get_edge_counts(ctx, &edges);

    send_cov_get_edge_counts(client->client_fd, status != 0, &edges);
}

static void send_cov_clear_edge_data(socket_t fd, uint32_t found_edges) {
    struct {
        cmd_pkt_t hdr;
        cov_clear_edge_data_rp payload;
    } resp;
    resp.hdr.opcode = COV_CLEAR_EDGE_DATA | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_clear_edge_data_rp));
    resp.payload = (cov_clear_edge_data_rp){ .found_edges = htonl(found_edges) };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_clear_edge_data(client_state_t *client, uint8_t *payload) {
    cov_clear_edge_data_cp *cp = (cov_clear_edge_data_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t index = ntohl(cp->index);

    cov_clear_edge_data(ctx, index);

    send_cov_clear_edge_data(client->client_fd, ctx->found_edges);
}

static void send_cov_reset_state(socket_t fd, uint32_t found_edges) {
    struct {
        cmd_pkt_t hdr;
        cov_reset_state_rp payload;
    } resp;
    resp.hdr.opcode = COV_RESET_STATE | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_reset_state_rp));
    resp.payload = (cov_reset_state_rp){ .found_edges = htonl(found_edges) };
    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_reset_state(client_state_t *client, uint8_t *payload) {
    cov_reset_state_cp *cp = (cov_reset_state_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    cov_reset_state(ctx);

    send_cov_reset_state(client->client_fd, ctx->found_edges);
}

static void send_cov_get_virgin_bits(socket_t fd, uint32_t zero_count, uint32_t *zero_indices) {
    uint32_t data_size = zero_count * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cov_get_bitmap_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_GET_VIRGIN_BITS | RESP_MASK;
    hdr->length = htonl(sizeof(cov_get_bitmap_rp) + data_size);

    cov_get_bitmap_rp *payload = (cov_get_bitmap_rp *)(packet + sizeof(cmd_pkt_t));
    payload->num_zeroes = htonl(zero_count);

    memcpy(payload->indices, zero_indices, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_cov_get_virgin_bits(client_state_t *client, uint8_t *payload) {
    cov_get_bitmap_cp *cp = (cov_get_bitmap_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    uint32_t zero_count = 0;
    uint8_t *bitmap = ctx->virgin_bits;
    for (uint32_t i = 0; i < ctx->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            zero_count++;
        }
    }
    uint32_t *zero_indices = malloc(zero_count * sizeof(uint32_t));
    uint32_t last = 0;
    // XXXR3 two loops instead of multiple reallocs
    for (uint32_t i = 0; i < ctx->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            zero_indices[last++] = i;
        }
    }

    send_cov_get_virgin_bits(client->client_fd, zero_count, zero_indices);
}

static void send_cov_get_crash_bits(socket_t fd, uint32_t zero_count, uint32_t *zero_indices) {
    uint32_t data_size = zero_count * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cov_get_bitmap_rp) + sizeof(cmd_pkt_t);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_GET_CRASH_BITS | RESP_MASK;
    hdr->length = htonl(sizeof(cov_get_bitmap_rp) + data_size);

    cov_get_bitmap_rp *payload = (cov_get_bitmap_rp *)(packet + sizeof(cmd_pkt_t));
    payload->num_zeroes = htonl(zero_count);

    memcpy(payload->indices, zero_indices, data_size);

    socket_send_all_remote(fd, packet, total_size);
    free(packet);
}

void process_cov_get_crash_bits(client_state_t *client, uint8_t *payload) {
    cov_get_bitmap_cp *cp = (cov_get_bitmap_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }

    // XXXR3 TODO refactor to reuse this conversion routine
    uint32_t zero_count = 0;
    uint8_t *bitmap = ctx->crash_bits;
    for (uint32_t i = 0; i < ctx->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            zero_count++;
        }
    }
    uint32_t *zero_indices = malloc(zero_count * sizeof(uint32_t));
    uint32_t last = 0;
    // XXXR3 two loops instead of multiple reallocs
    for (uint32_t i = 0; i < ctx->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            zero_indices[last++] = i;
        }
    }

    send_cov_get_crash_bits(client->client_fd, zero_count, zero_indices);
}

void process_cov_set_crash_bits(client_state_t *client, uint8_t *payload) {
    cov_set_bitmap_cp *cp = (cov_set_bitmap_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t zero_count = ntohl(cp->num_zeroes);
    uint32_t *zero_indices = malloc(zero_count * sizeof(uint32_t));
    memcpy(zero_indices, cp->indices, zero_count * sizeof(uint32_t));

    memset(ctx->crash_bits, 0xff, ctx->bitmap_size);
    for (uint32_t i = 0; i < zero_count; i++) {
        ctx->crash_bits[zero_indices[i]] = 0;
    }

    // no reply
}

void process_cov_set_virgin_bits(client_state_t *client, uint8_t *payload) {
    cov_set_bitmap_cp *cp = (cov_set_bitmap_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t zero_count = ntohl(cp->num_zeroes);
    uint32_t *zero_indices = malloc(zero_count * sizeof(uint32_t));
    memcpy(zero_indices, cp->indices, zero_count * sizeof(uint32_t));

    memset(ctx->virgin_bits, 0xff, ctx->bitmap_size);
    for (uint32_t i = 0; i < zero_count; i++) {
        ctx->virgin_bits[zero_indices[i]] = 0;
    }

    // no reply
}

static void send_cov_bulk_clear_edge_data(socket_t fd, uint32_t found_edges) {
    struct {
        cmd_pkt_t hdr;
        cov_bulk_clear_edge_data_rp payload;
    } resp;
    resp.hdr.opcode = COV_BULK_CLEAR_EDGE_DATA | RESP_MASK;
    resp.hdr.length = htonl(sizeof(cov_bulk_clear_edge_data_rp));
    resp.payload = (cov_bulk_clear_edge_data_rp){ .found_edges = htonl(found_edges) };

    socket_send_all_remote(fd, (const uint8_t*)&resp, sizeof(resp));
}

void process_cov_bulk_clear_edge_data(client_state_t *client, uint8_t *payload) {
    cov_bulk_clear_edge_data_cp *cp = (cov_bulk_clear_edge_data_cp *)payload;
    struct cov_context *ctx = (struct cov_context*)get_object(client->cov_ctx_map, ntohs(cp->ctx_handle));
    if (ctx == NULL) {
        fprintf(stderr, "Invalid handle: no context associated to %u\n", cp->ctx_handle);
        return;
    }
    uint32_t num_indices = ntohl(cp->num_indices);
    uint32_t *indices = (uint32_t *)malloc(num_indices * sizeof(uint32_t));
    memcpy(indices, cp->indices, num_indices * sizeof(uint32_t));

    for (uint32_t i = 0; i < num_indices; i++) {
        cov_clear_edge_data(ctx, indices[i]);
    }

    send_cov_bulk_clear_edge_data(client->client_fd, ctx->found_edges);
}
