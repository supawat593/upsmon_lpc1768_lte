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
  char encoded_key[64];
  char topic_path[100];
  char full_cmd[64];
  char model[32];
  char siteID[32];
} init_script_t;

typedef struct {
  char revID[20];
  char imei[16];
  char iccid[20];
  char ipaddr[32];
  char dns_ip[16];
  int sig;
  int ber;
  char cereg_msg[64];
  char cops_msg[64];
  char cpsi_msg[128];
  char cclk_msg[32];
} __attribute__((__packed__)) cellular_data_t;

typedef struct {
  char stat_payload[512];
  char stat_topic[128];
  char str_data_topic[128];
  char str_data_msg[256];
  char str_sub_topic[128];

} mqtt_config_t;

typedef struct {
  char sub_topic[128];
  char sub_payload[256];
  int client_idx;
  int len_topic;
  int len_payload;
} rx_notify_t;

#endif