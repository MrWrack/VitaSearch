#include "config.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>

#define CFG_DIR "ux0:data/vitasearch"
#define CFG_FILE CFG_DIR "/config.txt"

int config_load_proxy(char *out, size_t out_size) {
  if (!out || out_size < 8) return -1;
  sceIoMkdir(CFG_DIR, 0777);
  int fd = sceIoOpen(CFG_FILE, SCE_O_RDONLY, 0);
  if (fd < 0) {
    const char *def = "https://192.168.1.50:8443\n\n\n";
    fd = sceIoOpen(CFG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, def, strlen(def)); sceIoClose(fd); }
    strncpy(out, "https://192.168.1.50:8443", out_size - 1);
    out[out_size - 1] = 0;
    return 0;
  }
  int n = sceIoRead(fd, out, out_size - 1);
  sceIoClose(fd);
  if (n <= 0) return -1;
  out[n] = 0;
  while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' ')) out[--n] = 0;
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
