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

#ifndef REMOTE_EXECUTOR_COVERAGE_PROTOCOL_H
#define REMOTE_EXECUTOR_COVERAGE_PROTOCOL_H

#include <stdint.h>

#define COV_CREATE_CONTEXT 0x10
/* No command parameters */
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) cov_create_context_rp;

#define COV_INIT 0x11
typedef struct {
    uint16_t ctx_handle;
    uint32_t pid;
    uint32_t context_id;
} __attribute__ ((packed)) cov_init_cp;

typedef struct {
    uint8_t status; // 0x0 for success
} __attribute__ ((packed)) cov_init_rp;

#define COV_FINISH_INIT 0x12
typedef struct {
    uint16_t ctx_handle;
    uint8_t should_track_edges;
} __attribute__ ((packed)) cov_finish_init_cp;

typedef struct {
    uint32_t num_edges;
    uint32_t bitmap_size;
    uint8_t should_track_edges; // XXXR3 this will be the same as the corresponding cp payload
} __attribute__ ((packed)) cov_finish_init_rp;

#define COV_SHUTDOWN 0x13
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) cov_shutdown_cp;
/* No reply */

#define COV_EVAL 0x14
typedef struct {
    uint16_t ctx_handle;
} __attribute__ ((packed)) cov_eval_cp;

typedef struct {
    uint8_t status;
    uint32_t found_edges;
    uint32_t edge_count;
    uint32_t edge_indices[];  // uint32_t[edge_count]
} __attribute__ ((packed)) cov_eval_rp;

#define COV_EVAL_CRASH 0x15
typedef struct {
    uint16_t ctx_handle;
} __attribute__((packed)) cov_eval_crash_cp;

typedef struct {
    uint8_t status;
} __attribute__((packed)) cov_eval_crash_rp;

#define COV_COMPARE_EQUAL 0x16
typedef struct {
    uint16_t ctx_handle;
    uint32_t edge_count;
    uint32_t edge_indices[];  // uint32_t[edge_count]
} __attribute__((packed)) cov_compare_equal_cp;

typedef struct {
    uint8_t status;
} __attribute__((packed)) cov_compare_equal_rp;

#define COV_CLEAR_BITMAP 0x17
typedef struct {
    uint16_t ctx_handle;
} __attribute__((packed)) cov_clear_bitmap_cp;
/* No reply */

#define COV_GET_EDGE_COUNTS 0x18
typedef struct {
    uint16_t ctx_handle;
} __attribute__((packed)) cov_get_edge_counts_cp;

typedef struct {
    uint8_t status;
    uint32_t edge_count;
    uint32_t edge_sparse_count;
    // followed by edge_sparse_count entries of:
    // struct {
    //     uint32_t index;
    //     uint32_t value;
    // } edges[];
} __attribute__((packed)) cov_get_edge_counts_rp;

typedef struct {
    uint32_t index;
    uint32_t value;
} __attribute__((packed)) cov_sparse_edge_t;

#define COV_CLEAR_EDGE_DATA 0x19
typedef struct {
    uint16_t ctx_handle;
    uint32_t index;
} __attribute__((packed)) cov_clear_edge_data_cp;

typedef struct {
    uint32_t found_edges;
} __attribute__((packed)) cov_clear_edge_data_rp;

#define COV_RESET_STATE 0x1a
typedef struct {
    uint16_t ctx_handle;
} __attribute__((packed)) cov_reset_state_cp;

typedef struct {
    uint32_t found_edges;
} __attribute__((packed)) cov_reset_state_rp;

#define COV_GET_VIRGIN_BITS 0x1b
#define COV_GET_CRASH_BITS 0x1c
typedef struct {
    uint16_t ctx_handle;
} __attribute__((packed)) cov_get_bitmap_cp;

// sparse array of bits that are set to 0x0
typedef struct {
    uint32_t num_zeroes;
    uint32_t indices[]; // uint32_t[num_zeroes]
} __attribute__((packed)) cov_get_bitmap_rp;

#define COV_SET_VIRGIN_BITS 0x1d
#define COV_SET_CRASH_BITS 0x1e
typedef struct {
    uint16_t ctx_handle;
    uint32_t num_zeroes;
    uint32_t indices[]; // uint32_t[num_zeroes]
} __attribute__((packed)) cov_set_bitmap_cp;
/* No reply */

#define COV_BULK_CLEAR_EDGE_DATA 0x1f
typedef struct {
    uint16_t ctx_handle;
    uint32_t num_indices;
    uint32_t indices[];  // uint32_t[num_indices]
} __attribute__((packed)) cov_bulk_clear_edge_data_cp;

typedef struct {
    uint32_t found_edges;
} __attribute__((packed)) cov_bulk_clear_edge_data_rp;

#endif
