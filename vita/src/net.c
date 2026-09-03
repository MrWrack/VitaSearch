#include "net.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>

static char g_api_key[128] = "";
static char g_ca_file[256] = "";
static SceUID g_net_mutex = -1;

void net_set_ca_file(const char *path) {
  if (!path) path = "";
  strncpy(g_ca_file, path, sizeof(g_ca_file)-1);
  g_ca_file[sizeof(g_ca_file)-1] = 0;
}

void net_set_api_key(const char *key) {
  if (!key) key = "";
  strncpy(g_api_key, key, sizeof(g_api_key)-1);
  g_api_key[sizeof(g_api_key)-1] = 0;
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
  NetBuffer *buf = (NetBuffer *)userdata;
  size_t n = size * nmemb;
  unsigned char *p = realloc(buf->data, buf->size + n + 1);
  if (!p) return 0;
  buf->data = p;
  memcpy(buf->data + buf->size, ptr, n);
  buf->size += n;
  buf->data[buf->size] = 0;
  return n;
}

int net_init(void) {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return -1;
  g_net_mutex = sceKernelCreateMutex("VitaSearchNet", 0, 1, NULL);
  return g_net_mutex >= 0 ? 0 : -1;
}

void net_term(void) {
  if (g_net_mutex >= 0) { sceKernelDeleteMutex(g_net_mutex); g_net_mutex = -1; }
  curl_global_cleanup();
}

static int perform(CURL *c, NetBuffer *out) {
  if (g_net_mutex >= 0) sceKernelLockMutex(g_net_mutex, 1, NULL);
  out->data = NULL;
  out->size = 0;
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 6L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  if (g_ca_file[0]) curl_easy_setopt(c, CURLOPT_CAINFO, g_ca_file);
  CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  int ok = (rc == CURLE_OK && status >= 200 && status < 300) ? 0 : -1;
  if (g_net_mutex >= 0) sceKernelUnlockMutex(g_net_mutex, 1);
  return ok;
}

int net_get(const char *url, NetBuffer *out) {
  CURL *c = curl_easy_init();
  if (!c) return -1;
  struct curl_slist *headers = NULL;
  char auth[180];
  if (g_api_key[0]) { snprintf(auth, sizeof(auth), "X-VitaSearch-Key: %s", g_api_key); headers = curl_slist_append(headers, auth); curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers); }
  curl_easy_setopt(c, CURLOPT_URL, url);
  int rc = perform(c, out);
  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(c);
  return rc;
}

int net_post_json(const char *url, const char *json, NetBuffer *out) {
  CURL *c = curl_easy_init();
  if (!c) return -1;
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  char auth[180];
  if (g_api_key[0]) { snprintf(auth, sizeof(auth), "X-VitaSearch-Key: %s", g_api_key); headers = curl_slist_append(headers, auth); }
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, json);
  int rc = perform(c, out);
  curl_slist_free_all(headers);
  curl_easy_cleanup(c);
  return rc;
}

void net_buffer_free(NetBuffer *buf) {
  if (buf && buf->data) free(buf->data);
  if (buf) { buf->data = NULL; buf->size = 0; }
}
