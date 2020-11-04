#ifndef SIM7600CELLULAR_H
#define SIM7600CELLULAR_H

#include "mbed.h"

bool check_modem_status(int rty=20);
bool check_attachNW();
int get_creg();
bool set_full_FUNCTION();
int get_IMEI(char *simei);
int get_ICCID(char *ciccid);

int dns_resolve(char *src,char *dst);

bool mqtt_start();
bool mqtt_accquire_client(char *clientName);
bool mqtt_connect(char *broker_ip, char *usr, char *pwd, int port=1883);

#endif