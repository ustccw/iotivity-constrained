/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <config.h>
#ifdef OC_BLOCK_WISE_SET_MTU
#include "oc_blockwise.h"
#include "port/oc_log.h"
#include "util/oc_list.h"
#include "util/oc_memb.h"

OC_MEMB(oc_blockwise_request_states_s, oc_blockwise_request_state_t,
        MAX_NUM_CONCURRENT_REQUESTS);
OC_MEMB(oc_blockwise_response_states_s, oc_blockwise_response_state_t,
        MAX_NUM_CONCURRENT_REQUESTS);
OC_LIST(oc_blockwise_requests);
OC_LIST(oc_blockwise_responses);

static oc_blockwise_state_t *
oc_blockwise_init_buffer(struct oc_memb *pool, const char *href, int href_len,
                         oc_endpoint_t *endpoint, oc_method_t method)
{
  oc_blockwise_state_t *buffer = (oc_blockwise_state_t *)oc_memb_alloc(pool);
  if (buffer) {
    buffer->next_block_offset = 0;
    buffer->payload_size = 0;
    buffer->ref_count = 1;
    buffer->method = method;
    memcpy(&buffer->endpoint, endpoint, sizeof(oc_endpoint_t));
    oc_new_string(&buffer->href, href, href_len);

#ifdef OC_CLIENT
    buffer->mid = 0;
    buffer->client_cb = 0;
#endif /* OC_CLIENT */
  }
  return buffer;
}

oc_blockwise_state_t *
oc_blockwise_alloc_request_buffer(const char *href, int href_len,
                                  oc_endpoint_t *endpoint, oc_method_t method)
{
  oc_blockwise_request_state_t *buffer =
    (oc_blockwise_request_state_t *)oc_blockwise_init_buffer(
      &oc_blockwise_request_states_s, href, href_len, endpoint, method);
  if (buffer) {
    oc_ri_add_timed_event_callback_seconds(
      buffer, oc_blockwise_free_request_buffer, OC_EXCHANGE_LIFETIME);
    oc_list_add(oc_blockwise_requests, buffer);
  }
  return (oc_blockwise_state_t *)buffer;
}

oc_blockwise_state_t *
oc_blockwise_alloc_response_buffer(const char *href, int href_len,
                                   oc_endpoint_t *endpoint, oc_method_t method)
{
  oc_blockwise_response_state_t *buffer =
    (oc_blockwise_response_state_t *)oc_blockwise_init_buffer(
      &oc_blockwise_response_states_s, href, href_len, endpoint, method);
  if (buffer) {
    int i = COAP_ETAG_LEN;
    uint32_t r = oc_random_value();
    while (i > 0) {
      memcpy(buffer->etag, &r, MIN(sizeof(r), i));
      i -= sizeof(r);
      r = oc_random_value();
    }
#ifdef OC_CLIENT
    buffer->observe_seq = -1;
#endif /* OC_CLIENT */
    oc_ri_add_timed_event_callback_seconds(
      buffer, oc_blockwise_free_response_buffer, OC_EXCHANGE_LIFETIME);
    oc_list_add(oc_blockwise_responses, buffer);
  }
  return (oc_blockwise_state_t *)buffer;
}

static void
oc_blockwise_free_buffer(oc_list_t list, struct oc_memb *pool, void *data)
{
  oc_blockwise_state_t *buffer = (oc_blockwise_state_t *)data;
  oc_list_remove(list, buffer);
  oc_free_string(&buffer->href);
  oc_memb_free(pool, buffer);
}

oc_event_callback_retval_t
oc_blockwise_free_request_buffer(void *data)
{
  oc_ri_remove_timed_event_callback(data, oc_blockwise_free_request_buffer);
  oc_blockwise_free_buffer(oc_blockwise_requests,
                           &oc_blockwise_request_states_s, data);
  return DONE;
}

oc_event_callback_retval_t
oc_blockwise_free_response_buffer(void *data)
{
  oc_ri_remove_timed_event_callback(data, oc_blockwise_free_response_buffer);
  oc_blockwise_free_buffer(oc_blockwise_responses,
                           &oc_blockwise_response_states_s, data);
  return DONE;
}

#ifdef OC_CLIENT
static oc_blockwise_state_t *
oc_blockwise_find_buffer_by_mid(oc_list_t list, uint16_t mid)
{
  oc_blockwise_state_t *buffer = oc_list_head(list);
  while (buffer) {
    if (buffer->mid == mid)
      break;
    buffer = buffer->next;
  }
  return buffer;
}

oc_blockwise_state_t *
oc_blockwise_find_request_buffer_by_mid(uint16_t mid)
{
  return oc_blockwise_find_buffer_by_mid(oc_blockwise_requests, mid);
}

oc_blockwise_state_t *
oc_blockwise_find_response_buffer_by_mid(uint16_t mid)
{
  return oc_blockwise_find_buffer_by_mid(oc_blockwise_responses, mid);
}
#endif /* OC_CLIENT */

static oc_blockwise_state_t *
oc_blockwise_find_buffer(oc_list_t list, const char *href, int href_len,
                         oc_endpoint_t *endpoint, oc_method_t method)
{
  oc_blockwise_state_t *buffer = oc_list_head(list);
  while (buffer) {
    if (strncmp(href, oc_string(buffer->href), href_len) == 0 &&
        memcpy(&buffer->endpoint, endpoint, sizeof(oc_endpoint_t)) &&
        buffer->method == method)
      break;
    buffer = buffer->next;
  }
  return buffer;
}

oc_blockwise_state_t *
oc_blockwise_find_request_buffer(const char *href, int href_len,
                                 oc_endpoint_t *endpoint, oc_method_t method)
{
  return oc_blockwise_find_buffer(oc_blockwise_requests, href, href_len,
                                  endpoint, method);
}

oc_blockwise_state_t *
oc_blockwise_find_response_buffer(const char *href, int href_len,
                                  oc_endpoint_t *endpoint, oc_method_t method)
{
  return oc_blockwise_find_buffer(oc_blockwise_responses, href, href_len,
                                  endpoint, method);
}

const void *
oc_blockwise_dispatch_block(oc_blockwise_state_t *buffer, uint32_t block_offset,
                            uint16_t requested_block_size,
                            uint16_t *payload_size)
{
  if (block_offset < buffer->payload_size) {
    if (buffer->payload_size < requested_block_size)
      *payload_size = buffer->payload_size;
    else {
      *payload_size =
        MIN(requested_block_size, buffer->payload_size - block_offset);
    }
    buffer->next_block_offset = block_offset + *payload_size;
    return (const void *)&buffer->buffer[block_offset];
  }
  return NULL;
}

bool
oc_blockwise_handle_block(oc_blockwise_state_t *buffer,
                          uint32_t incoming_block_offset,
                          const uint8_t *incoming_block,
                          uint16_t incoming_block_size)
{
  if (incoming_block_offset >= OC_BLOCK_WISE_BUFFER_SIZE ||
      incoming_block_size >
        (OC_BLOCK_WISE_BUFFER_SIZE - incoming_block_offset) ||
      incoming_block_offset > buffer->next_block_offset)
    return false;

  if (buffer->next_block_offset == incoming_block_offset) {
    memcpy(&buffer->buffer[buffer->next_block_offset], incoming_block,
           incoming_block_size);

    buffer->next_block_offset += incoming_block_size;
  }

  return true;
}
#endif /* OC_BLOCK_WISE_SET_MTU */
