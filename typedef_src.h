#ifndef _TYPEDEF_SRC_H
#define _TYPEDEF_SRC_H

#include <cstdint>

typedef struct {
  unsigned int utc;
  char cmd[16];
  char resp[128];
} mail_t;

typedef struct {
  char broker[32];
  int port;
  char usr[32];
  char pwd[32];
  char topic_path[100];
  char full_cmd[64];
  char model[32];
  char siteID[32];
} init_script_t;

#endif