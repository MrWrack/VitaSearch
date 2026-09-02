#ifndef VITASEARCH_SETTINGS_H
#define VITASEARCH_SETTINGS_H

int settings_set_javascript(const char *proxy, const char *session, int enabled);
int settings_clear_data(const char *proxy, const char *session, const char *what);

#endif
