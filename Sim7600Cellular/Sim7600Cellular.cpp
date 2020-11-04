#include "Sim7600Cellular.h"

extern ATCmdParser *_parser;

bool check_modem_status(int rty) {
  bool bAT_OK = false;
  _parser->set_timeout(1000);

  for (int i = 0; !bAT_OK && (i < rty); i++) {
    _parser->flush();
    if (_parser->send("AT")) {
      ThisThread::sleep_for(100ms);
      if (_parser->recv("OK")) {
        bAT_OK = true;
        // printf("Module SIM7600  OK\r\n");
      } else {
        bAT_OK = false;
        // printf("Module SIM7600  Fail : %d\r\n",i);
      }
    }
  }

  _parser->set_timeout(8000);
  return bAT_OK;
}

bool check_attachNW() {
  int ret = 0;
  if (_parser->send("AT+CGATT?") && _parser->recv("+CGATT: %d\r\n", &ret)) {
    if (ret == 1) {
      return true;
    } else {
      return false;
    }
  }
  return false;
}

int get_creg() {
  int stat = 0;
  char ret[20];

  if (_parser->send("AT+CREG?") && _parser->recv("+CREG: %s\r\n", ret)) {
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

bool set_full_FUNCTION() {
  bool bcops = false;
  bool bcfun = false;
  if (_parser->send("AT+COPS=0") && _parser->recv("OK")) {
    printf("set---> AT+COPS=0\r\n");
    bcops = true;
  }

  if (_parser->send("AT+CFUN=1") && _parser->recv("OK")) {
    printf("set---> AT+CFUN=1\r\n");
    bcfun = true;
  }
  return bcops && bcfun;
}

int get_IMEI(char *simei) {
  char _imei[16];
  if (_parser->send("AT+SIMEI?") &&
      _parser->scanf("+SIMEI: %[^\n]\r\n", _imei)) {
    // printf("imei=  %s\r\n", _imei);
    strcpy(simei, _imei);
    return 1;
  }
  return -1;
}

int get_ICCID(char *ciccid) {
  char _iccid[20];
  if (_parser->send("AT+CICCID") &&
      _parser->scanf("+ICCID: %[^\n]\r\n", _iccid)) {
    // printf("iccid= %s\r\n", _iccid);
    strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}

int dns_resolve(char *src, char *dst) {
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
    if (_parser->send(cmd) && _parser->scanf("+CDNSGIP: %[^\n]\r\n", ret_dns) &&
        _parser->recv("OK")) {
      printf("dns resolve Complete --> %s\r\n", ret_dns);

      if (sscanf(ret_dns, "%*d,\"%*[^\"]\",\"%[^\"]\"", result) == 1) {
        printf("host ip --> %s\r\n", result);
        strcpy(dst,result);
        return 1;
      }
      strcpy(dst, "");
      return -1;
    }
    strcpy(dst, "");
    return -1;
  }
}

bool mqtt_start() {
  bool bmqtt_start = false;
  if (_parser->send("AT+CMQTTSTART") && _parser->recv("OK") &&
      _parser->recv("+CMQTTSTART: 0")) {
    printf("AT+CMQTTSTART --> Completed\r\n");
    bmqtt_start = true;
  }
  return bmqtt_start;
}

bool mqtt_accquire_client(char *clientName) {
  char cmd[128];
  sprintf(cmd, "AT+CMQTTACCQ=0,\"%s\"", clientName);
  if (_parser->send(cmd) && _parser->recv("OK")) {
    printf("set client id -> %s\r\n", cmd);
    return true;
  }
  return false;
}

bool mqtt_connect(char *broker_ip, char *usr, char *pwd, int port) {
  char cmd[128];
  bool bmqtt_cnt = false;
  sprintf(cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,0,\"%s\",\"%s\"",
          broker_ip, port, usr, pwd);
  printf("connect cmd -> %s\r\n", cmd);
  if (_parser->send(cmd) && _parser->recv("OK") &&
      _parser->recv("+CMQTTCONNECT: 0,0")) {
    // if (_parser->send(cmd)) {
    // printf("connect cmd -> %s" CRLF, cmd);
    printf("MQTT connected\r\n");
    bmqtt_cnt = true;
  }
  return bmqtt_cnt;
}