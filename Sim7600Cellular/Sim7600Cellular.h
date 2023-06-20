#ifndef SIM7600CELLULAR_H
#define SIM7600CELLULAR_H

#include "mbed.h"

class Sim7600Cellular {

public:
  Sim7600Cellular(ATCmdParser *_parser);
  Sim7600Cellular(BufferedSerial *_serial);
  Sim7600Cellular(PinName tx, PinName rx);
  bool check_modem_status(int rty = 20);
  bool enable_echo(bool en);
  bool save_setting();

  void set_ntp_srv(char *srv, int tz_q);
  int get_ntp_srv(char *ntp_srv);
  bool check_ntp_status();

  bool check_attachNW();
  int set_cops(int mode = 0, int format = 2);
  int get_cops(char *cops);
  int get_csq(int *power, int *ber, int retry = 10);
  int get_cclk(char *cclk);
  int set_creg(int n);
  int get_creg();
  int get_creg(char *payload);
  int set_cereg(int n);
  int get_cereg(char *payload);
  bool set_full_FUNCTION(int rst = 0);
  bool set_min_cFunction();
  int get_revID(char *revid);
  int get_IMEI(char *simei);
  int get_ICCID(char *ciccid);

  int set_pref_Mode(int mode = 2);
  int get_pref_Mode();
  int set_acq_order(int a1 = 9, int a2 = 5, int a3 = 3, int a4 = 11, int a5 = 2,
                    int a6 = 4);
  int get_acq_order();

  int get_IPAddr(char *ipaddr);
  int get_cpsi(char *cpsi);
  int set_tz_update(int en);
  int dns_resolve(char *src, char *dst);
  int ping_dstNW(char *dst, int nrty = 4, int p_size = 64, int dest_type = 1);

  bool mqtt_start();
  bool mqtt_stop();
  bool mqtt_release(int clientindex = 0);
  bool mqtt_accquire_client(char *clientName);
  bool mqtt_connect(char *broker_ip, char *usr, char *pwd, int port = 1883,
                    int clientindex = 0);
  int mqtt_connect_stat(void);
  int mqtt_connect_stat(char *ret_msg);
  int mqtt_isdisconnect(int clientindex = 0);
  bool mqtt_publish(char topic[128], char payload[512], int qos = 1,
                    int interval_s = 60);
  bool mqtt_sub(char topic[128], int clientindex = 0, int qos = 1);
  bool mqtt_unsub(char topic[128], int clientindex = 0, int dup = 0);

  int read_atc_to_char(char *tbuf, int size, char end);

private:
  ATCmdParser *_atc;
  BufferedSerial *serial;

  void printHEX(unsigned char *msg, unsigned int len);
};

#endif