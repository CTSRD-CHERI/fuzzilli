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

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "libsocket_remote.h"

#include <endian.h>
#include <stdint.h>
#include <stddef.h>

#define INITIAL_CONTEXT_MAP_CAPACITY 16

typedef struct {
    void **objects;
    size_t capacity;
} context_map;

void init_context_map(context_map *map);
void destroy_context_map(context_map *map);
int insert_object(context_map *map, void *obj);
void* get_object(context_map *map, int id);
void* remove_object(context_map *map, int id);

// Client state object, containing arguments for each
// remote executor thread handler
typedef struct {
    socket_t client_fd;
    context_map* reprl_ctx_map;
    context_map* cov_ctx_map;
} client_state_t;

#endif
