#pragma once
#include <stddef.h>
#include <vita2d.h>

typedef struct {
  int connected;
  int playing;
  int progress_ms;
  int duration_ms;
  int volume;
  char title[160];
  char artist[160];
  char device[96];
  char cover_url[512];
} SpotifyState;

typedef struct {
  char name[144];
  char artist[120];
  char uri[192];
  char cover_url[512];
} SpotifyTrack;

int spotify_get_state(const char *proxy, SpotifyState *out);
int spotify_command(const char *proxy, const char *command);
int spotify_seek(const char *proxy, int position_ms);
int spotify_volume(const char *proxy, int volume);
int spotify_search(const char *proxy, const char *query, SpotifyTrack *out, int max_items);
int spotify_play_uri(const char *proxy, const char *uri);
int spotify_queue_uri(const char *proxy, const char *uri);
vita2d_texture *spotify_load_cover(const char *proxy, const char *remote_url);
