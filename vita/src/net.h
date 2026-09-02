#pragma once
#include <stddef.h>

typedef struct {
  unsigned char *data;
  size_t size;
} NetBuffer;

int net_init(void);
void net_set_api_key(const char *key);
void net_set_ca_file(const char *path);
void net_term(void);
int net_get(const char *url, NetBuffer *out);
int net_post_json(const char *url, const char *json, NetBuffer *out);
void net_buffer_free(NetBuffer *buf);
