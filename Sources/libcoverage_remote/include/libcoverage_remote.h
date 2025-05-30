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

#ifndef LIBCOVERAGE_REMOTE_H
#define LIBCOVERAGE_REMOTE_H

#include <stdint.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <Windows.h>
#endif

#include "libsocket_remote.h"
#include "remote-executor-coverage-protocol.h"
#include "libcoverage.h"

uint16_t cov_create_context_remote(socket_t fd);

int cov_initialize_remote(socket_t fd, uint16_t handle, struct cov_context*, pid_t pid);
void cov_finish_initialization_remote(socket_t fd, uint16_t handle, struct cov_context*, int should_track_edges);
void cov_shutdown_remote(socket_t fd, uint16_t handle, struct cov_context*);

int cov_evaluate_remote(socket_t fd, uint16_t handle, struct cov_context* context, struct edge_set* new_edges);
int cov_evaluate_crash_remote(socket_t fd, uint16_t handle, struct cov_context*);

int cov_compare_equal_remote(socket_t fd, uint16_t handle, struct cov_context*, uint32_t* edges, uint32_t num_edges);

void cov_clear_bitmap_remote(socket_t fd, uint16_t handle, struct cov_context*);

int cov_get_edge_counts_remote(socket_t fd, uint16_t handle, struct cov_context* context, struct edge_counts* edges);
void cov_clear_edge_data_remote(socket_t fd, uint16_t handle, struct cov_context* context, uint32_t index);
void cov_bulk_clear_edge_data_remote(socket_t fd, uint16_t handle, struct cov_context* context, const uint32_t* indices, uint32_t num_indices);
void cov_reset_state_remote(socket_t fd, uint16_t handle, struct cov_context* context);

void cov_get_bitmaps_remote(socket_t fd, uint16_t handle, struct cov_context* context);
void cov_set_bitmaps_remote(socket_t fd, uint16_t handle, struct cov_context* context);

#endif
