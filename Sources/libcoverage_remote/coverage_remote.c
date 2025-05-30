// Copyright 2025 Zhuo Ying Jiang Li
// Copyright 2019 Google LLC
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

#include "libcoverage_remote.h"
#include "remote-executor-coverage-protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

uint16_t cov_create_context_remote(socket_t fd) {
    // send
    struct {
        cmd_pkt_t hdr;
    } cmd;
    cmd.hdr.opcode = COV_CREATE_CONTEXT;
    cmd.hdr.length = htonl(0);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_create_context: send failed\n");
        return -1;
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_create_context_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_create_context: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (COV_CREATE_CONTEXT | RESP_MASK)) {
        fprintf(stderr, "cov_create_context: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_create_context_rp)) {
        fprintf(stderr, "cov_create_context: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_create_context: recv payload failed\n");
        return -1;
    }
    return ntohs(resp.payload.ctx_handle);
}

int cov_initialize_remote(socket_t fd, uint16_t handle, struct cov_context* context, pid_t pid) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_init_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_INIT;
    cmd.hdr.length = htonl(sizeof(cov_init_cp));
    cmd.payload.ctx_handle = htons(handle);
    cmd.payload.pid = htonl(pid);
    cmd.payload.context_id = htonl(context->id);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_initialize: send failed\n");
        return -1;
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_init_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_initialize: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (COV_INIT | RESP_MASK)) {
        fprintf(stderr, "cov_initialize: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_init_rp)) {
        fprintf(stderr, "cov_initialize: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_initialize: recv payload failed\n");
        return -1;
    }
    return resp.payload.status != 0;
}

void cov_finish_initialization_remote(socket_t fd, uint16_t handle, struct cov_context* context, int should_track_edges) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_finish_init_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_FINISH_INIT;
    cmd.hdr.length = htonl(sizeof(cov_finish_init_cp));
    cmd.payload.ctx_handle = htons(handle);
    cmd.payload.should_track_edges = should_track_edges;
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_finish_initialization: send failed\n");
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_finish_init_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_finish_initialization: recv hdr failed\n");
    }
    if (resp.hdr.opcode != (COV_FINISH_INIT | RESP_MASK)) {
        fprintf(stderr, "cov_finish_initialization: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_finish_init_rp)) {
        fprintf(stderr, "cov_finish_initialization: invalid payload size: %u\n", payload_len);
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_finish_initialization: recv payload failed\n");
    }
    uint32_t num_edges = ntohl(resp.payload.num_edges);
    uint32_t bitmap_size = ntohl(resp.payload.bitmap_size);
    context->num_edges = num_edges;
    context->bitmap_size = bitmap_size;
    context->should_track_edges = should_track_edges;
}

void cov_shutdown_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_shutdown_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_SHUTDOWN;
    cmd.hdr.length = htonl(sizeof(cov_shutdown_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_initialize: send failed\n");
    }
    // no reply
}

int cov_evaluate_remote(socket_t fd, uint16_t handle, struct cov_context* context, struct edge_set* new_edges) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_eval_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_EVAL;
    cmd.hdr.length = htonl(sizeof(cov_eval_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_evaluate: send failed\n");
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "cov_evaluate: recv hdr failed\n");
        return -1;
    }
    if (resp_hdr.opcode != (COV_EVAL | RESP_MASK)) {
        fprintf(stderr, "cov_evaluate: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return -1;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    cov_eval_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "cov_evaluate: invalid response payload size: %u\n", resp_len);
        return -1;
    }
    uint8_t status = resp_pkt->status;
    uint32_t found_edges = ntohl(resp_pkt->found_edges);
    uint32_t edge_count = ntohl(resp_pkt->edge_count);

    context->found_edges = found_edges;
    new_edges->count = edge_count;
    if (edge_count > 0) {
        uint32_t *edge_indices = malloc(edge_count * sizeof(uint32_t));
        memcpy(edge_indices, resp_pkt->edge_indices, edge_count * sizeof(uint32_t));
        new_edges->edge_indices = edge_indices;
    }

    return status != 0;
}

int cov_evaluate_crash_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_eval_crash_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_EVAL_CRASH;
    cmd.hdr.length = htonl(sizeof(cov_eval_crash_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_evaluate_crash: send failed\n");
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_eval_crash_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_evaluate_crash: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (COV_EVAL_CRASH | RESP_MASK)) {
        fprintf(stderr, "cov_evaluate_crash: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_eval_crash_rp)) {
        fprintf(stderr, "cov_evaluate_crash: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_evaluate_crash: recv payload failed\n");
        return -1;
    }
    return resp.payload.status != 0;
}

int cov_compare_equal_remote(socket_t fd, uint16_t handle, struct cov_context* context, uint32_t* edges, uint32_t num_edges) {
    // send
    uint32_t data_size = num_edges * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cmd_pkt_t) + sizeof(cov_compare_equal_cp);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
        return -1;
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_COMPARE_EQUAL;
    hdr->length = htonl(sizeof(cov_compare_equal_cp) + data_size);
    cov_compare_equal_cp *payload = (cov_compare_equal_cp *)(packet + sizeof(cmd_pkt_t));
    payload->ctx_handle = htons(handle);
    payload->edge_count = htonl(num_edges);
    memcpy(payload->edge_indices, edges, data_size);
    if (socket_send_all_remote(fd, packet, total_size) != total_size) {
        fprintf(stderr, "cov_compare_equal: send failed\n");
        return -1;
    }
    free(packet);

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_compare_equal_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_compare_equal: recv hdr failed\n");
        return -1;
    }
    if (resp.hdr.opcode != (COV_COMPARE_EQUAL | RESP_MASK)) {
        fprintf(stderr, "cov_compare_equal: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
        return -1;
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_compare_equal_rp)) {
        fprintf(stderr, "cov_compare_equal: invalid payload size: %u\n", payload_len);
        return -1;
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_compare_equal: recv payload failed\n");
        return -1;
    }
    return resp.payload.status != 0;
}

void cov_clear_bitmap_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_clear_bitmap_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_CLEAR_BITMAP;
    cmd.hdr.length = htonl(sizeof(cov_clear_bitmap_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_clear_bitmap: send failed\n");
    }
    // no reply
}

int cov_get_edge_counts_remote(socket_t fd, uint16_t handle, struct cov_context* context, struct edge_counts* edges) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_get_edge_counts_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_GET_EDGE_COUNTS;
    cmd.hdr.length = htonl(sizeof(cov_get_edge_counts_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_get_edge_counts: send failed\n");
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "cov_get_edge_counts: recv hdr failed\n");
        return -1;
    }
    if (resp_hdr.opcode != (COV_GET_EDGE_COUNTS | RESP_MASK)) {
        fprintf(stderr, "cov_get_edge_counts: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
        return -1;
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    cov_get_edge_counts_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "cov_get_edge_counts: invalid response payload size: %u\n", resp_len);
        return -1;
    }
    uint8_t status = resp_pkt->status;
    uint32_t edge_count = ntohl(resp_pkt->edge_count);
    uint32_t *edge_hit_count = malloc(edge_count * sizeof(uint32_t));

    uint32_t edge_sparse_count = ntohl(resp_pkt->edge_sparse_count);
    cov_sparse_edge_t *sparse_array = (cov_sparse_edge_t *)((uint8_t *)resp_pkt + sizeof(cov_get_edge_counts_rp));
    for (uint64_t i = 0; i < edge_sparse_count; i++) {
        edge_hit_count[sparse_array[i].index] = sparse_array[i].value;
    }

    edges->count = edge_count;
    edges->edge_hit_count = edge_hit_count;

    return status != 0;
}

void cov_clear_edge_data_remote(socket_t fd, uint16_t handle, struct cov_context* context, uint32_t index) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_clear_edge_data_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_CLEAR_EDGE_DATA;
    cmd.hdr.length = htonl(sizeof(cov_clear_edge_data_cp));
    cmd.payload.ctx_handle = htons(handle);
    cmd.payload.index = htonl(index);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_clear_edge_data: send failed\n");
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_clear_edge_data_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_clear_edge_data: recv hdr failed\n");
    }
    if (resp.hdr.opcode != (COV_CLEAR_EDGE_DATA | RESP_MASK)) {
        fprintf(stderr, "cov_clear_edge_data: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_clear_edge_data_rp)) {
        fprintf(stderr, "cov_clear_edge_data: invalid payload size: %u\n", payload_len);
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_clear_edge_data: recv payload failed\n");
    }
    uint32_t found_edges = ntohl(resp.payload.found_edges);
    context->found_edges = found_edges;
}

void cov_bulk_clear_edge_data_remote(socket_t fd, uint16_t handle, struct cov_context* context, const uint32_t* indices, uint32_t num_indices) {
    // send
    uint32_t data_size = num_indices * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cmd_pkt_t) + sizeof(cov_bulk_clear_edge_data_cp);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = COV_BULK_CLEAR_EDGE_DATA;
    hdr->length = htonl(sizeof(cov_bulk_clear_edge_data_cp) + data_size);
    cov_bulk_clear_edge_data_cp *payload = (cov_bulk_clear_edge_data_cp *)(packet + sizeof(cmd_pkt_t));
    payload->ctx_handle = htons(handle);
    payload->num_indices = htonl(num_indices);
    memcpy(payload->indices, indices, data_size);
    if (socket_send_all_remote(fd, packet, total_size) != total_size) {
        fprintf(stderr, "cov_bulk_clear_edge_data_remote: send failed\n");
    }
    free(packet);

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_bulk_clear_edge_data_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_bulk_clear_edge_data_remote: recv hdr failed\n");
    }
    if (resp.hdr.opcode != (COV_BULK_CLEAR_EDGE_DATA | RESP_MASK)) {
        fprintf(stderr, "cov_bulk_clear_edge_data_remote: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_bulk_clear_edge_data_rp)) {
        fprintf(stderr, "cov_bulk_clear_edge_data_remote: invalid payload size: %u\n", payload_len);
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_bulk_clear_edge_data_remote: recv payload failed");
    }
    uint32_t found_edges = ntohl(resp.payload.found_edges);
    context->found_edges = found_edges;
}

void cov_reset_state_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_reset_state_cp payload;
    } cmd;
    cmd.hdr.opcode = COV_RESET_STATE;
    cmd.hdr.length = htonl(sizeof(cov_reset_state_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_reset_state: send failed\n");
    }

    // recv
    struct {
        cmd_pkt_t hdr;
        cov_reset_state_rp payload;
    } resp;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.hdr, sizeof(resp.hdr)) != sizeof(resp.hdr)) {
        fprintf(stderr, "cov_reset_state: recv hdr failed\n");
    }
    if (resp.hdr.opcode != (COV_RESET_STATE | RESP_MASK)) {
        fprintf(stderr, "cov_reset_state: unexpected response opcode: 0x%02x\n", resp.hdr.opcode);
    }
    uint32_t payload_len = ntohl(resp.hdr.length);
    if (payload_len != sizeof(cov_reset_state_rp)) {
        fprintf(stderr, "cov_reset_state: invalid payload size: %u\n", payload_len);
    }
    if (socket_recv_all_remote(fd, (uint8_t*)&resp.payload, sizeof(resp.payload)) != sizeof(resp.payload)) {
        fprintf(stderr, "cov_reset_state: recv payload failed\n");
    }
    uint32_t found_edges = ntohl(resp.payload.found_edges);
    context->found_edges = found_edges;
}

static void cov_get_bitmap_remote(socket_t fd, uint16_t handle, struct cov_context* context, uint8_t opcode) {
    // send
    struct {
        cmd_pkt_t hdr;
        cov_get_bitmap_cp payload;
    } cmd;
    cmd.hdr.opcode = opcode;
    cmd.hdr.length = htonl(sizeof(cov_get_bitmap_cp));
    cmd.payload.ctx_handle = htons(handle);
    if (socket_send_all_remote(fd, (const uint8_t*)&cmd, sizeof(cmd)) != sizeof(cmd)) {
        fprintf(stderr, "cov_get_bitmap: send failed\n");
    }

    // recv
    cmd_pkt_t resp_hdr;
    if (socket_recv_all_remote(fd, (uint8_t*)&resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) {
        fprintf(stderr, "cov_get_bitmap: recv hdr failed\n");
    }
    if (resp_hdr.opcode != (opcode | RESP_MASK)) {
        fprintf(stderr, "cov_get_bitmap: unexpected response opcode: 0x%02x\n", resp_hdr.opcode);
    }
    uint32_t resp_len = ntohl(resp_hdr.length);
    cov_get_bitmap_rp* resp_pkt = malloc(resp_len);
    if (socket_recv_all_remote(fd, (uint8_t*)resp_pkt, resp_len) != resp_len) {
        fprintf(stderr, "cov_get_bitmap: invalid response payload size: %u\n", resp_len);
    }
    uint32_t num_zeroes = ntohl(resp_pkt->num_zeroes);
    uint8_t *bitmap = opcode == COV_GET_VIRGIN_BITS ? context->virgin_bits : context->crash_bits;
    for (uint32_t i = 0; i < num_zeroes; i++) {
        bitmap[resp_pkt->indices[i]] = 0;
    }
}

void cov_get_bitmaps_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    cov_get_bitmap_remote(fd, handle, context, COV_GET_VIRGIN_BITS);
    cov_get_bitmap_remote(fd, handle, context, COV_GET_CRASH_BITS);
}

static void cov_set_bitmap_remote(socket_t fd, uint16_t handle, struct cov_context* context, uint8_t opcode) {
    uint8_t *bitmap = opcode == COV_SET_VIRGIN_BITS ? context->virgin_bits : context->crash_bits;
    uint32_t num_zeroes = 0;
    for (uint32_t i = 0; i < context->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            num_zeroes++;
        }
    }
    uint32_t *zero_indices = malloc(num_zeroes * sizeof(uint32_t));
    uint32_t last = 0;
    // XXXR3 two loops instead of multiple reallocs
    for (uint32_t i = 0; i < context->bitmap_size; i++) {
        if (bitmap[i] == 0) {
            zero_indices[last++] = i;
        }
    }

    // send
    uint32_t data_size = num_zeroes * sizeof(uint32_t);
    uint32_t total_size = data_size + sizeof(cmd_pkt_t) + sizeof(cov_set_bitmap_cp);
    uint8_t *packet = malloc(total_size);
    if (!packet) {
        perror("malloc");
    }
    cmd_pkt_t *hdr = (cmd_pkt_t *)packet;
    hdr->opcode = opcode;
    hdr->length = htonl(sizeof(cov_set_bitmap_cp) + data_size);
    cov_set_bitmap_cp *payload = (cov_set_bitmap_cp *)(packet + sizeof(cmd_pkt_t));
    payload->ctx_handle = htons(handle);
    payload->num_zeroes = htonl(num_zeroes);
    memcpy(payload->indices, zero_indices, data_size);
    if (socket_send_all_remote(fd, packet, total_size) != total_size) {
        fprintf(stderr, "cov_set_bitmap: send failed\n");
    }
    free(packet);
    // no reply
}

void cov_set_bitmaps_remote(socket_t fd, uint16_t handle, struct cov_context* context) {
    cov_set_bitmap_remote(fd, handle, context, COV_SET_VIRGIN_BITS);
    cov_set_bitmap_remote(fd, handle, context, COV_SET_CRASH_BITS);
}
