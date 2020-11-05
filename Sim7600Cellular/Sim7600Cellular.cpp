#include "Sim7600Cellular.h"

// extern ATCmdParser *_parser;

Sim7600Cellular::Sim7600Cellular(ATCmdParser *_parser) : _atc(_parser) {}

Sim7600Cellular::Sim7600Cellular(PinName tx, PinName rx) {
  serial = new BufferedSerial(tx, rx, 115200);
  _atc = new ATCmdParser(serial, "\r\n", 256, 8000);
}

bool Sim7600Cellular::check_modem_status(int rty) {
  bool bAT_OK = false;
  _atc->set_timeout(1000);

  for (int i = 0; !bAT_OK && (i < rty); i++) {
    _atc->flush();
    if (_atc->send("AT")) {
      ThisThread::sleep_for(100ms);
      if (_atc->recv("OK")) {
        bAT_OK = true;
        // printf("Module SIM7600  OK\r\n");
      } else {
        bAT_OK = false;
        // printf("Module SIM7600  Fail : %d\r\n",i);
      }
    }
  }

  _atc->set_timeout(8000);
  return bAT_OK;
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

int Sim7600Cellular::get_csq(int *power, int *ber) {
  int _power = 0;
  int _ber = 0;
  char ret[20];

  if (_atc->send("AT+CSQ") && _atc->recv("+CSQ: %[^\n]\r\n", ret)) {
    // printf("ret= %s\r\n",ret);
    if (sscanf(ret, "%d,%d", &_power, &_ber) == 2) {
      *power = _power;
      *ber = _ber;
      return 1;
    }
    *power = 0;
    *ber = 0;
    return 0;
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
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CREG?") && _atc->recv("+CREG: %s\r\n", ret)) {
    printf("pattern found +CREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);

    if (sscanf(ret, "%*d,%d", &stat) == 1) {
      return stat;
    } else if (sscanf(ret, "%*d,%d,%*s,%*s", &stat) == 1) {
      return stat;
    } else {
      return -1;
    }
  }

  return -1;
}

bool Sim7600Cellular::set_full_FUNCTION() {
  bool bcops = false;
  bool bcfun = false;
  if (_atc->send("AT+COPS=0") && _atc->recv("OK")) {
    printf("set---> AT+COPS=0\r\n");
    bcops = true;
  }

  if (_atc->send("AT+CFUN=1") && _atc->recv("OK")) {
    printf("set---> AT+CFUN=1\r\n");
    bcfun = true;
  }
  return bcops && bcfun;
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

int Sim7600Cellular::get_IPAddr(char *ipaddr) {
  char _ipaddr[32];
  if (_atc->send("AT+CGPADDR=1") &&
      _atc->scanf("+CGPADDR: 1,%[^\n]\r\n", _ipaddr)) {
    strcpy(ipaddr, _ipaddr);
    return 1;
  }
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

bool Sim7600Cellular::mqtt_start() {
  bool bmqtt_start = false;
  if (_atc->send("AT+CMQTTSTART") && _atc->recv("OK") &&
      _atc->recv("+CMQTTSTART: 0")) {
    printf("AT+CMQTTSTART --> Completed\r\n");
    bmqtt_start = true;
  }
  return bmqtt_start;
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
                                   int port) {
  char cmd[128];
  bool bmqtt_cnt = false;
  sprintf(cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,0,\"%s\",\"%s\"",
          broker_ip, port, usr, pwd);
  printf("connect cmd -> %s\r\n", cmd);
  if (_atc->send(cmd) && _atc->recv("OK") && _atc->recv("+CMQTTCONNECT: 0,0")) {
    // if (_parser->send(cmd)) {
    // printf("connect cmd -> %s" CRLF, cmd);
    printf("MQTT connected\r\n");
    bmqtt_cnt = true;
  }
  return bmqtt_cnt;
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
  _atc->send(cmd_pub_topic);

  if (_atc->recv(">")) {

    if ((_atc->write(topic, len_topic) == len_topic) && _atc->recv("OK")) {
      printf("set topic : %s Done!\r\n", topic);
    }
  }

  printf("cmd_pub_msg= %s\r\n", cmd_pub_msg);

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