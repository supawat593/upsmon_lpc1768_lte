#include "Sim7600Cellular.h"
#include <cstdio>
#include <cstring>

// extern ATCmdParser *_parser;

Sim7600Cellular::Sim7600Cellular(ATCmdParser *_parser) : _atc(_parser) {}

Sim7600Cellular::Sim7600Cellular(PinName tx, PinName rx) {
  serial = new BufferedSerial(tx, rx, 115200);
  _atc = new ATCmdParser(serial, "\r\n", 256, 8000);
}

void Sim7600Cellular::printHEX(unsigned char *msg, unsigned int len) {

  printf(
      "\r\n>>>>>------------------- printHEX -------------------------<<<<<");
  printf("\r\nAddress :  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
  printf(
      "\r\n----------------------------------------------------------------");

  unsigned int k = 0;
  for (unsigned int j = 0; j < len; j++) {
    if ((j % 16) == 0) {
      printf("\r\n0x%04X0 : ", k);
      k++;
    }
    printf("%02X ", (unsigned)msg[j]);
  }
  printf("\r\n----------------------------------------------------------------"
         "\r\n");
}

bool Sim7600Cellular::check_modem_status(int rty) {
  bool bAT_OK = false;
  _atc->set_timeout(1000);

  for (int i = 0; (!bAT_OK) && (i < rty); i++) {

    if (_atc->send("AT")) {
      //   ThisThread::sleep_for(100ms);
      if (_atc->recv("OK")) {
        bAT_OK = true;
        printf("Module SIM7600  OK\r\n");
      } else {
        bAT_OK = false;
        printf("Module SIM7600  Fail : %d\r\n", i);
      }
    }
  }

  _atc->set_timeout(8000);
  return bAT_OK;
}

bool Sim7600Cellular::enable_echo(bool en) {
  char cmd_txt[10];
  sprintf(cmd_txt, "ATE%d", (int)en);
  if (_atc->send(cmd_txt) && _atc->recv("OK")) {
    if (en) {
      printf("Enable AT Echo\r\n");
    } else {
      printf("Disable AT Echo\r\n");
    }
    return true;
  }
  return false;
}
bool Sim7600Cellular::save_setting() {
  if (_atc->send("AT&W0") && _atc->recv("OK")) {
    return true;
  }
  return false;
}

bool Sim7600Cellular::check_attachNW() {
  int ret = 0;
  if (_atc->send("AT+CGATT?") && _atc->recv("+CGATT: %d\r\n", &ret)) {
    if (ret == 1) {
      return true;
    } else {
      return false;
    }
  }
  return false;
}

int Sim7600Cellular::set_cops(int mode, int format) {
  char _cmd[16];
  sprintf(_cmd, "AT+COPS=%d,%d", mode, format);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set---> AT+COPS=%d,%d\r\n", mode, format);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_cops(char *cops) {
  char _ret[64];

  if (_atc->send("AT+COPS?") && _atc->recv("+COPS: %*d,%*d,\"%[^\"]\"", _ret)) {
    // printf("imei=  %s\r\n", _imei);
    strcpy(cops, _ret);
    return 1;
  }
  strcpy(cops, "");
  return -1;
}

int Sim7600Cellular::get_csq(int *power, int *ber, int retry) {
  int _power = 99;
  int _ber = 99;
  //   char ret[20];
  int i = 0;
  _atc->set_timeout(2000);
  while ((i < retry) && (_power == 99)) {
    if (_atc->send("AT+CSQ") && _atc->recv("+CSQ: %d,%d\r\n", &_power, &_ber)) {
      printf("retry: %d -> +CSQ: %d,%d\r\n", i, _power, _ber);
    }
    i++;
    ThisThread::sleep_for(1000);
  }
  _atc->set_timeout(8000);

  //   if (_atc->send("AT+CSQ") && _atc->recv("+CSQ: %[^\n]\r\n", ret)) {
  //     // printf("ret= %s\r\n",ret);
  //     if (sscanf(ret, "%d,%d", &_power, &_ber) == 2) {
  //       *power = _power;
  //       *ber = _ber;
  //       return 1;
  //     }
  //     *power = 0;
  //     *ber = 0;
  //     return 0;
  //   }
  if (i < retry) {
    *power = _power;
    *ber = _ber;
    return 1;
  }

  *power = 99;
  *ber = 99;
  return -1;
}

int Sim7600Cellular::set_creg(int n) {
  char cmd[10];
  sprintf(cmd, "AT+CREG=%d", n);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("modem set---> AT+CREG=%d\r\n", n);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_creg() {
  int n = 0;
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CREG?") && _atc->scanf("+CREG: %[^\n]\r\n", ret)) {
    // printf("pattern found +CREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);

    if (sscanf(ret, "%d,%d", &n, &stat) == 2) {
      return stat;
    } else if (sscanf(ret, "%d,%d,%*s,%*s", &n, &stat) == 2) {
      return stat;
    } else {
      return -1;
    }
  }

  return -1;
}

int Sim7600Cellular::get_creg(char *payload) {
  int n = 0;
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CREG?") && _atc->scanf("+CREG: %[^\n]\r\n", ret)) {
    printf("pattern found +CREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);
    char ret_msg[64];
    sprintf(ret_msg, "+CREG: %s", ret);
    strcpy(payload, ret_msg);
    return 1;
  }
  strcpy(payload, "");
  return -1;
}

int Sim7600Cellular::set_cereg(int n) {
  char cmd[10];
  sprintf(cmd, "AT+CEREG=%d", n);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("modem set---> AT+CEREG=%d\r\n", n);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_cereg(char *payload) {
  int n = 0;
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CEREG?") && _atc->scanf("+CEREG: %[^\n]\r\n", ret)) {
    printf("pattern found +CEREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);
    char ret_msg[64];
    sprintf(ret_msg, "+CEREG: %s", ret);
    strcpy(payload, ret_msg);
    return 1;
  }
  strcpy(payload, "");
  return -1;
}

bool Sim7600Cellular::set_full_FUNCTION() {
  bool bcops = false;
  bool bcfun = false;

  if (set_cops() == 1) {
    bcops = true;
  }

  if (_atc->send("AT+CFUN=1") && _atc->recv("OK")) {
    printf("set---> AT+CFUN=1\r\n");
    bcfun = true;
  }
  return bcops && bcfun;
}

bool Sim7600Cellular::set_min_cFunction() {
  if (_atc->send("AT+CFUN=0") && _atc->recv("OK")) {
    printf("set_min_cFunction---> AT+CFUN=0\r\n");
    return true;
  }
  return false;
}

int Sim7600Cellular::get_revID(char *revid) {
  char _revid[20];
  if (_atc->send("AT+CGMR") && _atc->scanf("+CGMR: %[^\n]\r\n", _revid)) {
    printf("+CGMR: %s\r\n", _revid);
    strcpy(revid, _revid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_IMEI(char *simei) {
  char _imei[16];
  if (_atc->send("AT+SIMEI?") && _atc->scanf("+SIMEI: %[^\n]\r\n", _imei)) {
    // printf("imei=  %s\r\n", _imei);
    strcpy(simei, _imei);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_ICCID(char *ciccid) {
  char _iccid[20];
  if (_atc->send("AT+CICCID") && _atc->scanf("+ICCID: %[^\n]\r\n", _iccid)) {
    // printf("iccid= %s\r\n", _iccid);
    strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::set_pref_Mode(int mode) {
  char _cmd[20];
  sprintf(_cmd, "AT+CNMP=%d", mode);
  _atc->set_timeout(12000);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set Preferred Mode -> AT+CNMP=%d\r\n", mode);
    _atc->set_timeout(8000);
    return 1;
  }

  _atc->set_timeout(8000);
  return -1;
}

int Sim7600Cellular::get_pref_Mode() {
  int ret = 0;
  if (_atc->send("AT+CNMP?") && _atc->recv("+CNMP: %d\r\n", &ret) &&
      _atc->recv("OK")) {
    printf("+CNMP: %d\r\n", ret);
    // strcpy(ciccid, _iccid);
    return ret;
  }
  return -1;
}

int Sim7600Cellular::set_acq_order(int a1, int a2, int a3, int a4, int a5,
                                   int a6) {
  char _cmd[30];
  sprintf(_cmd, "AT+CNAOP=7,%d,%d,%d,%d,%d,%d", a1, a2, a3, a4, a5, a6);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set_acq_order -> %s\r\n", _cmd);
    // strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}
int Sim7600Cellular::get_acq_order() {
  char ret[30];
  if (_atc->send("AT+CNAOP?") && _atc->scanf("+CNAOP: %[^\n]\r\n", ret)) {
    printf("+CNAOP: %s\r\n", ret);
    // strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_IPAddr(char *ipaddr) {
  char _ipaddr[32];
  if (_atc->send("AT+CGPADDR=1") &&
      _atc->scanf("+CGPADDR: 1,%[^\n]\r\n", _ipaddr)) {
    strcpy(ipaddr, _ipaddr);
    return 1;
  }
  strcpy(ipaddr, "0.0.0.0");
  return -1;
}

int Sim7600Cellular::get_cpsi(char *cpsi) {
  char _cpsi[128];
  if (_atc->send("AT+CPSI?") && _atc->scanf("+CPSI: %[^\n]\r\n", _cpsi)) {
    // strcpy(cpsi, _ipaddr);
    sprintf(cpsi, "+CPSI: %s", _cpsi);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::set_tz_update(int en) {
  char cmd[10];
  sprintf(cmd, "AT+CTZU=%d", en);

  if (_atc->send(cmd) && _atc->recv("OK")) {
    if (en) {
      printf("Enable Automatic timezone update via NITZ\r\n");
      return 1;
    } else {
      printf("Disable Automatic timezone update via NITZ\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::dns_resolve(char *src, char *dst) {
  int ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0;
  char ret_dns[128];
  char cmd[150];
  char result[16];

  if (sscanf(src, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4) {
    strcpy(dst, src);
    printf("host is ip --> %d.%d.%d.%d\r\n", ip1, ip2, ip3, ip4);
    return 0;
  } else {
    sprintf(cmd, "AT+CDNSGIP=\"%s\"", src);
    if (_atc->send(cmd) && _atc->scanf("+CDNSGIP: %[^\n]\r\n", ret_dns) &&
        _atc->recv("OK")) {
      printf("dns resolve Complete --> %s\r\n", ret_dns);

      if (sscanf(ret_dns, "%*d,\"%*[^\"]\",\"%[^\"]\"", result) == 1) {
        printf("host ip --> %s\r\n", result);
        strcpy(dst, result);
        return 1;
      }
      strcpy(dst, "");
      return -1;
    }
    strcpy(dst, "");
    return -1;
  }
}

int Sim7600Cellular::ping_dstNW(char *dst, int nrty, int p_size,
                                int dest_type) {

  printf("\r\n-------- uping_dstNW()!!! -------->  [%s]\r\n", dst);

  char cping[100];

  sprintf(cping, "AT+CPING=\"%s\",%d,%d,%d,1000,10000,255", dst, dest_type,
          nrty, p_size);
  //   printf("CMD : %s\r\n", cping);

  _atc->set_timeout(12000);
  _atc->flush();

  int len = 64 * nrty;
  char pbuf[len];
  memset(pbuf, 0, len);

  if (_atc->send(cping) && _atc->recv("OK")) {

    _atc->read(pbuf, len);
    // printHEX((unsigned char *)pbuf, len);
    // printf("pbuf = %s\r\n", pbuf);
  }

  _atc->set_timeout(8000);

  int st = 0, end;
  //   char end_text[] = {0x0d, 0x0a, 0x00, 0x00};
  char end_text[] = {0x0d, 0x0a};

  while ((strncmp(&pbuf[st], "+CPING: 3", 9) != 0) && (st < len)) {
    st++;
  }

  end = st;
  while ((strncmp(&pbuf[end], end_text, 2) != 0) && (end < len)) {
    end++;
  }

  // printf("st=%d end=%d\r\n", st, end);

  char res_ping[end - st + 1];
  memset(res_ping, 0, end - st + 1);
  memcpy(&res_ping, &pbuf[st], end - st);
  printf("res_ping = %s\r\n", res_ping);

  int num_sent = nrty, num_recv = 0, num_lost = 0, min_rtt = 0, max_rtt = 0,
      avg_rtt = 0;

  sscanf(res_ping, "+CPING: 3,%d,%d,%d,%d,%d,%d", &num_sent, &num_recv,
         &num_lost, &min_rtt, &max_rtt, &avg_rtt);

  printf("min=%d max=%d avg=%d\r\n", min_rtt, max_rtt, avg_rtt);
  if (num_sent == num_recv) {
    printf("ping =>  %s  [OK] rtt = %d ms.\r\n", dst, avg_rtt);
    printf("---------------------------------------------\r\n");

    // free(pmask);  //for using with pointer

    return avg_rtt;
  } else {
    printf("ping => %s [Fail]\r\n", dst);
    printf("---------------------------------------------\r\n");
    // return false;

    // free(pmask);	//for using with pointer

    return 9999;
  }
}

bool Sim7600Cellular::mqtt_start() {
  bool bmqtt_start = false;
  //   if (_atc->send("AT+CMQTTSTART") && _atc->recv("OK")
  //   &&_atc->recv("+CMQTTSTART: 0")) {
  if (_atc->send("AT+CMQTTSTART") && _atc->recv("+CMQTTSTART: 0")) {
    // printf("AT+CMQTTSTART --> Completed\r\n");
    bmqtt_start = true;
  }
  return bmqtt_start;
}

bool Sim7600Cellular::mqtt_stop() {
  bool bmqtt_start = false;
  if (_atc->send("AT+CMQTTSTOP") && _atc->recv("+CMQTTSTOP: 0") &&
      _atc->recv("OK")) {
    printf("mqtt stop --> Completed\r\n");
    bmqtt_start = true;
  }
  return bmqtt_start;
}

bool Sim7600Cellular::mqtt_release(int clientindex) {
  char cmd[32];
  sprintf(cmd, "AT+CMQTTREL=%d", clientindex);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("Release mqtt client : index %d\r\n", clientindex);
    return true;
  }
  return false;
}

bool Sim7600Cellular::mqtt_accquire_client(char *clientName) {
  char cmd[128];
  sprintf(cmd, "AT+CMQTTACCQ=0,\"%s\"", clientName);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("set client id -> %s\r\n", cmd);
    return true;
  }
  return false;
}

bool Sim7600Cellular::mqtt_connect(char *broker_ip, char *usr, char *pwd,
                                   int port, int clientindex) {
  char cmd[128];
  int index = 0;
  int err = 0;
  //   bool bmqtt_cnt = false;
  sprintf(cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,0,\"%s\",\"%s\"",
          broker_ip, port, usr, pwd);
  printf("connect cmd -> %s\r\n", cmd);
  if (_atc->send(cmd) && _atc->recv("OK") &&
      _atc->recv("+CMQTTCONNECT: %d,%d\r\n", &index, &err)) {
    // if (_parser->send(cmd)) {
    // printf("connect cmd -> %s" CRLF, cmd);

    if ((index == clientindex) && (err == 0)) {
      //   printf("MQTT connected\r\n");
      return true;
    }
    printf("MQTT connect : index=%d err=%d\r\n", index, err);
    return false;
  }
  printf("MQTT connect : Pattern Fail!\r\n");
  return false;
}

int Sim7600Cellular::mqtt_connect_stat() {
  char _ret[128];
  if (_atc->send("AT+CMQTTCONNECT?") &&
      _atc->scanf("+CMQTTCONNECT: %[^\n]\r\n", _ret)) {
    // strcpy(cpsi, _ipaddr);

    int nret = 0, keepAlive = 0, clean = 0;
    // 0,"tcp://188.166.189.39:1883",60,0,"IoTdevices","devices@iot"
    if (sscanf(_ret, "%d,\"%*[^\"]\",%d,%d,\"%*[^\"]\",\"%*[^\"]\"", &nret,
               &keepAlive, &clean) == 3) {

      //   sprintf(ret_msg, "+CMQTTCONNECT: %s", _ret);
      return 1;

    } else {
      //   strcpy(ret_msg, "Disconnected\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::mqtt_connect_stat(char *ret_msg) {
  char _ret[128];
  if (_atc->send("AT+CMQTTCONNECT?") &&
      _atc->scanf("+CMQTTCONNECT: %[^\n]\r\n", _ret)) {
    // strcpy(cpsi, _ipaddr);

    int nret = 0, keepAlive = 0, clean = 0;
    // 0,"tcp://188.166.189.39:1883",60,0,"IoTdevices","devices@iot"
    if (sscanf(_ret, "%d,\"%*[^\"]\",%d,%d,\"%*[^\"]\",\"%*[^\"]\"", &nret,
               &keepAlive, &clean) == 3) {

      sprintf(ret_msg, "+CMQTTCONNECT: %s", _ret);
      return 1;

    } else {
      strcpy(ret_msg, "Disconnected\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::mqtt_isdisconnect(int clientindex) {
  char rcvbuf[64];
  char rcv[15];
  int disc_state = 0;
  int len = 0;
  sprintf(rcv, "+CMQTTDISC: %d", clientindex);
  len = strlen(rcv);
  _atc->set_timeout(2000);
  _atc->flush();
  _atc->send("AT+CMQTTDISC?");
  //   if (_atc->send("AT+CMQTTDISC?")) {
  _atc->read(rcvbuf, 64);
  _atc->set_timeout(8000);
  //   printHEX((unsigned char *)rcvbuf, 64);

  int st = 0;
  while ((strncmp(&rcvbuf[st], rcv, len) != 0) && (st < 64)) {
    st++;
  }

  if (st < 64) {
    char sub_ret[64 - st];
    memset(sub_ret, 0, 64 - st);
    memcpy(&sub_ret, &rcvbuf[st], 64 - st);
    sscanf(sub_ret, "+CMQTTDISC: %*d,%d\r\n", &disc_state);

    if (!disc_state) {
      printf("MQTT : Connection\r\n");
      return 1;
    } else {
      printf("MQTT : Disconnection\r\n");
      return 0;
    }
  }
  return -1;
}

bool Sim7600Cellular::mqtt_publish(char topic[64], char payload[256], int qos,
                                   int interval_s) {
  int len_topic = 0;
  int len_payload = 0;
  char cmd_pub_topic[32];
  char cmd_pub_msg[32];
  char cmd_pub[32];

  len_topic = strlen(topic);
  len_payload = strlen(payload);

  sprintf(cmd_pub_topic, "AT+CMQTTTOPIC=0,%d", len_topic);
  sprintf(cmd_pub_msg, "AT+CMQTTPAYLOAD=0,%d", len_payload);
  sprintf(cmd_pub, "AT+CMQTTPUB=0,%d,%d", qos, interval_s);

  printf("cmd_pub_topic= %s\r\n", cmd_pub_topic);
  _atc->flush();
  _atc->send(cmd_pub_topic);

  if (_atc->recv(">")) {

    if ((_atc->write(topic, len_topic) == len_topic) && _atc->recv("OK")) {
      printf("set topic : %s Done!\r\n", topic);
    }
  }

  printf("cmd_pub_msg= %s\r\n", cmd_pub_msg);
  _atc->flush();
  if (_atc->send(cmd_pub_msg) && _atc->recv(">")) {
    if ((_atc->write(payload, len_payload) == len_payload) &&
        _atc->recv("OK")) {
      printf("set pubmsg : %s : Done!\r\n", payload);
    }
  }

  printf("cmd_pub= %s\r\n", cmd_pub);
  ThisThread::sleep_for(1s);
  _atc->flush();
  _atc->set_timeout(12000);

  if (_atc->send(cmd_pub) && _atc->recv("OK") && _atc->recv("+CMQTTPUB: 0,0")) {
    printf("Publish msg : Done!!!\r\n");
    _atc->set_timeout(8000);
    return true;
  }

  _atc->set_timeout(8000);
  return false;
}