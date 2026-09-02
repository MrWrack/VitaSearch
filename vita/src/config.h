#pragma once
#include <stddef.h>
int config_load_proxy(char *out, size_t out_size);
int config_load(char *proxy, size_t proxy_size, char *api_key, size_t key_size, char *ca_file, size_t ca_size);
