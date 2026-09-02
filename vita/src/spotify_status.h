#ifndef VITASEARCH_SPOTIFY_STATUS_H
#define VITASEARCH_SPOTIFY_STATUS_H

typedef struct {
    int connected;
    char callback[24];
    char token[24];
    char device[96];
    char device_type[32];
} VitaSpotifyStatus;

int spotify_status_fetch(const char *proxy, VitaSpotifyStatus *out);

#endif
