#include "spotify.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *skip_ws(const char *p){ while(p && *p && isspace((unsigned char)*p)) ++p; return p; }

static int json_string_after(const char *json, const char *key, char *out, size_t cap) {
  char needle[96]; snprintf(needle,sizeof(needle),"\"%s\":",key);
  const char *p=strstr(json,needle); if(!p) return -1; p=skip_ws(p+strlen(needle));
  if(*p!='\"') return -1; ++p; size_t j=0;
  while(*p && *p!='\"' && j+1<cap){
    if(*p=='\\' && p[1]) { ++p; if(*p=='n') out[j++]=' '; else if(*p=='t') out[j++]=' '; else out[j++]=*p; ++p; }
    else out[j++]=*p++;
  }
  out[j]=0; return 0;
}
static int json_int_after(const char *json,const char *key,int def){char needle[96];snprintf(needle,sizeof(needle),"\"%s\":",key);const char*p=strstr(json,needle);if(!p)return def;p=skip_ws(p+strlen(needle));return atoi(p);}
static int json_bool_after(const char *json,const char *key,int def){char needle[96];snprintf(needle,sizeof(needle),"\"%s\":",key);const char*p=strstr(json,needle);if(!p)return def;p=skip_ws(p+strlen(needle));return !strncmp(p,"true",4)?1:!strncmp(p,"false",5)?0:def;}

static int get(const char *proxy,const char *path,NetBuffer*b){char url[1024];snprintf(url,sizeof(url),"%s%s",proxy,path);return net_get(url,b);}
static int post(const char *proxy,const char *path,const char *body){char url[1024];snprintf(url,sizeof(url),"%s%s",proxy,path);NetBuffer b;int rc=net_post_json(url,body?body:"{}",&b);net_buffer_free(&b);return rc;}

static void urlenc(const char *s,char *o,size_t cap){static const char h[]="0123456789ABCDEF";size_t j=0;for(size_t i=0;s[i]&&j+4<cap;i++){unsigned char c=s[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o[j++]=c;else{o[j++]='%';o[j++]=h[c>>4];o[j++]=h[c&15];}}o[j]=0;}
static void jsonesc(const char*s,char*o,size_t cap){size_t j=0;for(size_t i=0;s[i]&&j+2<cap;i++){char c=s[i];if(c=='\"'||c=='\\')o[j++]='\\';if((unsigned char)c>=32)o[j++]=c;}o[j]=0;}

int spotify_get_state(const char *proxy, SpotifyState *out){
  if(!out)return -1; memset(out,0,sizeof(*out)); out->volume=50;
  NetBuffer b; if(get(proxy,"/spotify/native/state",&b)!=0)return -1;
  const char*j=(const char*)b.data; out->connected=json_bool_after(j,"connected",0); out->playing=json_bool_after(j,"playing",0);
  out->progress_ms=json_int_after(j,"progress_ms",0); out->duration_ms=json_int_after(j,"duration_ms",0); out->volume=json_int_after(j,"volume",50);
  json_string_after(j,"title",out->title,sizeof(out->title)); json_string_after(j,"artist",out->artist,sizeof(out->artist));
  json_string_after(j,"device",out->device,sizeof(out->device)); json_string_after(j,"cover_url",out->cover_url,sizeof(out->cover_url));
  net_buffer_free(&b); return 0;
}
int spotify_command(const char *proxy,const char *command){char p[160];snprintf(p,sizeof(p),"/spotify/api/%s",command);return post(proxy,p,"{}");}
int spotify_seek(const char *proxy,int pos){char body[96];snprintf(body,sizeof(body),"{\"position_ms\":%d}",pos);return post(proxy,"/spotify/api/seek",body);}
int spotify_volume(const char *proxy,int vol){char body[64];if(vol<0)vol=0;if(vol>100)vol=100;snprintf(body,sizeof(body),"{\"volume\":%d}",vol);return post(proxy,"/spotify/api/volume",body);}
int spotify_play_uri(const char *proxy,const char *uri){char e[384],body[448];jsonesc(uri,e,sizeof(e));snprintf(body,sizeof(body),"{\"uri\":\"%s\"}",e);return post(proxy,"/spotify/api/play",body);}
int spotify_queue_uri(const char *proxy,const char *uri){char e[384],body[448];jsonesc(uri,e,sizeof(e));snprintf(body,sizeof(body),"{\"uri\":\"%s\"}",e);return post(proxy,"/spotify/api/queue",body);}

int spotify_search(const char *proxy,const char *query,SpotifyTrack*out,int max_items){
  if(!out||max_items<=0)return 0; char q[768],path[900];urlenc(query,q,sizeof(q));snprintf(path,sizeof(path),"/spotify/native/search?q=%s",q);
  NetBuffer b;if(get(proxy,path,&b)!=0)return -1;const char*p=(const char*)b.data;int n=0;
  while(n<max_items && (p=strstr(p,"{\"name\":"))){
    const char*end=strchr(p,'}'); if(!end)break; size_t len=(size_t)(end-p+1); char obj[1600]; if(len>=sizeof(obj))len=sizeof(obj)-1;memcpy(obj,p,len);obj[len]=0;
    memset(&out[n],0,sizeof(out[n])); json_string_after(obj,"name",out[n].name,sizeof(out[n].name)); json_string_after(obj,"artist",out[n].artist,sizeof(out[n].artist));
    json_string_after(obj,"uri",out[n].uri,sizeof(out[n].uri)); json_string_after(obj,"cover_url",out[n].cover_url,sizeof(out[n].cover_url)); n++; p=end+1;
  }
  net_buffer_free(&b);return n;
}

vita2d_texture *spotify_load_cover(const char *proxy,const char *remote_url){
  if(!remote_url||!remote_url[0])return NULL;char enc[1400],path[1550];urlenc(remote_url,enc,sizeof(enc));snprintf(path,sizeof(path),"/spotify/native/image?url=%s",enc);
  NetBuffer b;if(get(proxy,path,&b)!=0)return NULL; vita2d_texture*t=vita2d_load_JPEG_buffer(b.data,b.size);if(!t)t=vita2d_load_PNG_buffer(b.data);net_buffer_free(&b);return t;
}
