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

#ifndef COVERAGE_LAYER_H
#define COVERAGE_LAYER_H

#include "executor.h"

#include <stddef.h>
#include <stdint.h>

void process_cov_create_context(client_state_t *client);
void process_cov_init(client_state_t *client, uint8_t *payload);
void process_cov_finish_init(client_state_t *client, uint8_t *payload);
void process_cov_shutdown(client_state_t *client, uint8_t *payload);
void process_cov_eval(client_state_t *client, uint8_t *payload);
void process_cov_eval_crash(client_state_t *client, uint8_t *payload);
void process_cov_compare_equal(client_state_t *client, uint8_t *payload);
void process_cov_clear_bitmap(client_state_t *client, uint8_t *payload);
void process_cov_get_edge_counts(client_state_t *client, uint8_t *payload);
void process_cov_clear_edge_data(client_state_t *client, uint8_t *payload);
void process_cov_reset_state(client_state_t *client, uint8_t *payload);
void process_cov_get_virgin_bits(client_state_t *client, uint8_t *payload);
void process_cov_get_crash_bits(client_state_t *client, uint8_t *payload);
void process_cov_set_crash_bits(client_state_t *client, uint8_t *payload);
void process_cov_set_virgin_bits(client_state_t *client, uint8_t *payload);
void process_cov_bulk_clear_edge_data(client_state_t *client, uint8_t *payload);

#endif
