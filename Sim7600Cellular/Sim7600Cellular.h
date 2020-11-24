#ifndef SIM7600CELLULAR_H
#define SIM7600CELLULAR_H

#include "mbed.h"

class Sim7600Cellular {

public:
  Sim7600Cellular(ATCmdParser *_parser);
  Sim7600Cellular(PinName tx, PinName rx);
  bool check_modem_status(int rty = 20);
  bool check_attachNW();
  int get_csq(int *power, int *ber);
  int set_creg(int n);
  int get_creg();
  bool set_full_FUNCTION();
  int get_revID(char *revid);
  int get_IMEI(char *simei);
  int get_ICCID(char *ciccid);

  int get_IPAddr(char *ipaddr);
  int get_cpsi(char *cpsi);
  int set_tz_update(int en);
  int dns_resolve(char *src, char *dst);
  int ping_dstNW(char *dst, int nrty = 4, int p_size = 64, int dest_type = 1);

  bool mqtt_start();
  bool mqtt_stop();
  bool mqtt_release(int clientindex=0);
  bool mqtt_accquire_client(char *clientName);
  bool mqtt_connect(char *broker_ip, char *usr, char *pwd, int port = 1883,int clientindex=0);
  int mqtt_connect_stat(void);
  int mqtt_connect_stat(char *ret_msg);
  int mqtt_isdisconnect(int clientindex=0);
  bool mqtt_publish(char topic[64], char payload[256], int qos = 1,
                    int interval_s = 60);

private:
  ATCmdParser *_atc;
  BufferedSerial *serial;

  void printHEX(unsigned char *msg, unsigned int len);
};

#endif