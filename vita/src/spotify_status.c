#include "spotify_status.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

static int json_bool(const char *json,const char *key){
    char pat[64];
    snprintf(pat,sizeof(pat),"\"%s\":true",key);
    return strstr(json,pat)!=NULL;
}
static void json_string(const char *json,const char *key,char *out,size_t cap){
    if(!out||cap==0)return;
    out[0]=0;
    char pat[64];
    snprintf(pat,sizeof(pat),"\"%s\":\"",key);
    const char *p=strstr(json,pat);
    if(!p)return;
    p+=strlen(pat);
    const char *e=p;
    while(*e && *e!='"')e++;
    size_t n=(size_t)(e-p);
    if(n>=cap)n=cap-1;
    memcpy(out,p,n);
    out[n]=0;
}
int spotify_status_fetch(const char *proxy,VitaSpotifyStatus *out){
    if(!proxy||!out)return -1;
    memset(out,0,sizeof(*out));
    strcpy(out->callback,"unknown");
    strcpy(out->token,"unknown");
    strcpy(out->device,"None");

    char url[512];
    snprintf(url,sizeof(url),"%s/spotify/native/status",proxy);
    NetBuffer b;
    if(net_get(url,&b)!=0)return -2;
    const char *json=(const char*)b.data;
    if(!json){net_buffer_free(&b);return -3;}

    out->connected=json_bool(json,"connected");
    json_string(json,"callback",out->callback,sizeof(out->callback));
    json_string(json,"token",out->token,sizeof(out->token));
    const char *d=strstr(json,"\"device\":");
    if(d && strstr(d,"null")==NULL){
        json_string(d,"name",out->device,sizeof(out->device));
        json_string(d,"type",out->device_type,sizeof(out->device_type));
    }
    net_buffer_free(&b);
    return 0;
}
