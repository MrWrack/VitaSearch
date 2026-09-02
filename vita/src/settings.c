#include "settings.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

static int post(const char *proxy, const char *path, const char *json) {
  char url[512];
  NetBuffer out;
  snprintf(url, sizeof(url), "%s%s", proxy, path);
  int rc = net_post_json(url, json, &out);
  net_buffer_free(&out);
  return rc;
}

int settings_set_javascript(const char *proxy, const char *session, int enabled) {
  char body[256];
  if (!proxy || !session) return -1;
  snprintf(body, sizeof(body), "{\"session\":\"%s\",\"enabled\":%s}", session, enabled ? "true" : "false");
  return post(proxy, "/settings/javascript", body);
}

int settings_clear_data(const char *proxy, const char *session, const char *what) {
  char body[320];
  if (!proxy || !session || !what) return -1;
  snprintf(body, sizeof(body), "{\"session\":\"%s\",\"what\":\"%s\"}", session, what);
  return post(proxy, "/privacy/clear", body);
}
