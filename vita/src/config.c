#include "config.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>
#include <stdio.h>

#define CFG_DIR "ux0:data/vitasearch"
#define CFG_FILE CFG_DIR "/config.txt"

int config_load_proxy(char *out, size_t out_size) {
  if (!out || out_size < 8) return -1;
  sceIoMkdir(CFG_DIR, 0777);
  int fd = sceIoOpen(CFG_FILE, SCE_O_RDONLY, 0);
  if (fd < 0) {
    const char *def = "http://192.168.1.50:8080\n\n\n";
    fd = sceIoOpen(CFG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, def, strlen(def)); sceIoClose(fd); }
    strncpy(out, "http://192.168.1.50:8080", out_size - 1);
    out[out_size - 1] = 0;
    return 0;
  }

  /* RC41: only load line 1 as the proxy URL. RC40 accidentally kept
     the API-key/CA lines in this buffer, so a restart could corrupt proxy. */
  char buf[768];
  int n = sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (n <= 0) return -1;
  buf[n] = 0;

  char *eol = strpbrk(buf, "\r\n");
  if (eol) *eol = 0;
  char *begin = buf;
  while (*begin == ' ' || *begin == '\t') begin++;
  char *end = begin + strlen(begin);
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;

  if (strncmp(begin, "http://", 7) != 0 && strncmp(begin, "https://", 8) != 0) {
    /* Never accept an API key or random text as a proxy address. */
    strncpy(out, "http://192.168.1.50:8080", out_size - 1);
    out[out_size - 1] = 0;
    return -2;
  }

  strncpy(out, begin, out_size - 1);
  out[out_size - 1] = 0;

  /* RC35 migration from the old HTTPS prototype default. */
  if (!strcmp(out, "https://192.168.1.50:8443")) {
    strncpy(out, "http://192.168.1.50:8080", out_size - 1);
    out[out_size - 1] = 0;
  }
  return 0;
}

int config_load(char *proxy, size_t proxy_size, char *api_key, size_t key_size, char *ca_file, size_t ca_size) {
  if (config_load_proxy(proxy, proxy_size) != 0) return -1;
  if (api_key && key_size) api_key[0] = 0;
  if (ca_file && ca_size) ca_file[0] = 0;

  int fd = sceIoOpen(CFG_FILE, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  char buf[768]; int n = sceIoRead(fd, buf, sizeof(buf)-1); sceIoClose(fd);
  if (n <= 0) return 0; buf[n]=0;

  char *line1 = buf;
  char *line2 = strchr(line1, '\n');
  if (!line2) return 0;
  *line2++ = 0;
  char *line3 = strchr(line2, '\n');
  if (line3) *line3++ = 0;

  while (*line2==' ' || *line2=='\t' || *line2=='\r') line2++;
  char *e2=line2+strlen(line2); while(e2>line2 && (e2[-1]=='\r'||e2[-1]==' '||e2[-1]=='\t')) *--e2=0;
  if (api_key && key_size) { size_t len=strlen(line2); if(len>=key_size) len=key_size-1; memcpy(api_key,line2,len); api_key[len]=0; }

  if (line3 && ca_file && ca_size) {
    while (*line3==' ' || *line3=='\t' || *line3=='\r' || *line3=='\n') line3++;
    char *e3=line3; while(*e3 && *e3!='\r' && *e3!='\n') e3++;
    size_t len=(size_t)(e3-line3); if(len>=ca_size) len=ca_size-1; memcpy(ca_file,line3,len); ca_file[len]=0;
  }
  return 0;
}

int config_save_proxy(const char *proxy, const char *api_key, const char *ca_file) {
  if (!proxy || !proxy[0]) return -1;
  sceIoMkdir(CFG_DIR, 0777);
  int fd = sceIoOpen(CFG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) return -1;
  char buf[1024];
  int n = snprintf(buf, sizeof(buf), "%s\n%s\n%s\n",
                   proxy, api_key ? api_key : "", ca_file ? ca_file : "");
  if (n < 0 || n >= (int)sizeof(buf)) { sceIoClose(fd); return -1; }
  int wr = sceIoWrite(fd, buf, n);
  sceIoClose(fd);
  return wr == n ? 0 : -1;
}
