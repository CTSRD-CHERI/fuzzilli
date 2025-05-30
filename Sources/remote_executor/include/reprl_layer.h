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

#ifndef REPRL_LAYER_H
#define REPRL_LAYER_H

#include "executor.h"

#include <stddef.h>
#include <stdint.h>

void process_reprl_create_context(client_state_t *client);
void process_reprl_init_context(client_state_t *client, uint8_t* payload);
void process_reprl_destroy_context(client_state_t *client, uint8_t *payload);
void process_reprl_execute(client_state_t *client, uint8_t* payload);
void process_reprl_fetch_fuzzout(client_state_t *client, uint8_t* payload);
void process_reprl_fetch_stdout(client_state_t *client, uint8_t* payload);
void process_reprl_fetch_stderr(client_state_t *client, uint8_t* payload);
void process_reprl_get_last_error(client_state_t *client, uint8_t* payload);

#endif
