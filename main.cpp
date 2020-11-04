/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "devices_src.h"
#include "mbed.h"
#include <chrono>
#include <cstdio>
#include <cstring>

#include "Sim7600Cellular.h"

// Blinking rate in milliseconds
#define BLINKING_RATE 500ms
#define CRLF "\r\n"

DigitalOut led(LED1, 1);
static UnbufferedSerial pc(USBTX, USBRX);
static BufferedSerial mdm(PTD3, PTD2, 115200);
ATCmdParser *_parser;

DigitalOut vrf_en(PTC8, 0);
DigitalOut mdm_rst(PTB1);
DigitalOut mdm_pwr(PTB2);
DigitalOut mdm_flight(PTC10);
DigitalIn mdm_status(PTC9);

DigitalIn usb_det(PTA12);
BusIn dipsw(PTC7, PTC6, PTC5, PTC4);
InterruptIn pwake(PTC0);
DigitalOut pdone(PTB19, 0);

volatile bool isWake = false;

char *pc2uart = new char[1];
char *uart2pc = new char[1];

int sig = 0, ber = 0;
char ret_cgdcont[128];
unsigned int last_ntp_sync = 0;

char imei[16];
char iccid[20];

bool bmqtt_start = false;
bool bmqtt_cnt = false;

Thread blink_thread, netstat_thread, capture_thread;
;

struct tm struct_tm;

void printHEX(unsigned char *msg, unsigned int len);
void publish_process(char payload[256]);

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

void capture_thread_routine() {
  char cpsi3[128];
  char packet_cpsi[256];

  while (true) {
    printf("capture---> timestamp: %d" CRLF, (unsigned int)rtc_read());
    memset(cpsi3, 0, 128);
    memset(packet_cpsi, 0, 256);

    if (_parser->send("AT+CPSI?") &&
        _parser->scanf("+CPSI: %[^\n]\r\n", cpsi3)) {
      printf("cpsi=> +CPSI: %s\r\n", cpsi3);
    }

    sprintf(packet_cpsi, payload_pattern, imei, (unsigned int)rtc_read(), "Q1",
            cpsi3);

    publish_process(packet_cpsi);

    ThisThread::sleep_for(chrono::minutes(read_dipsw()));
  }
}

void fall_wake() { isWake = true; }
void kick_wdt() {
  pdone = 1;
  wait_us(10);
  pdone = 0;
}

void blink_routine() {
  while (true) {
    led = !led;
    ThisThread::sleep_for(BLINKING_RATE);
  }
}

void sync_rtc(char cclk[64]) {
  int qdiff = 0;
  char chdiff[4];
  // 20/11/02,00:17:48+28
  printf("sync_rtc(\"%s\")\r\n", cclk);
  sscanf(cclk, "%d/%d/%d,%d:%d:%d%s", &struct_tm.tm_year, &struct_tm.tm_mon,
         &struct_tm.tm_mday, &struct_tm.tm_hour, &struct_tm.tm_min,
         &struct_tm.tm_sec, chdiff);
  /*
      struct_tm.tm_sec=second;
      struct_tm.tm_min=minute;
      struct_tm.tm_hour=hour;
      struct_tm.tm_mday=day;
      struct_tm.tm_mon=month-1;
      struct_tm.tm_year=year+100;  //+2000-1900
  */
  qdiff = atoi(chdiff);
  qdiff /= 4;

  struct_tm.tm_mon -= 1;
  struct_tm.tm_year += 100;

  time_t sec_rtc = mktime(&struct_tm);

  unsigned int temp = 0;
  temp = (unsigned int)sec_rtc - (qdiff * 3600);
  sec_rtc = (time_t)temp;

  rtc_write(sec_rtc);
}

void ntp_sync() {

  if (_parser->send("AT+CNTP=\"time1.google.com\",28") && _parser->recv("OK")) {
    printf("set ntp server Done\r\n");
    char ret_ntp[64];

    if (_parser->send("AT+CNTP?") &&
        _parser->scanf("+CNTP: %[^\n]\r\n", ret_ntp)) {
      printf("ret_ntp--> +CNTP: %s\r\n", ret_ntp);

      if (_parser->send("AT+CNTP") && _parser->recv("OK")) {
        printf("NTP: Operation succeeded\r\n");
        last_ntp_sync = (unsigned int)rtc_read();
      }
    }
  }
}

void netstat_routine() {
  char cpsi2[128];
  while (true) {
    ThisThread::sleep_for(3000ms);
    printf("<----- Checking NW. Status ----->"CRLF);
    printf("timestamp : %d\r\n", (unsigned int)rtc_read());
    memset(cpsi2, 0, 128);

    if (_parser->send("AT+CPSI?") &&
        _parser->scanf("+CPSI: %[^\n]\r\n", cpsi2)) {
      printf("cpsi=> +CPSI: %s\r\n", cpsi2);
    }

    if (((unsigned int)rtc_read() - last_ntp_sync) > 3600) {
      ntp_sync();
    }

    //ThisThread::sleep_for(57s);
    ThisThread::sleep_for(chrono::seconds(read_dipsw()*20));
  }
}

int main() {

  // Initialise the digital pin LED1 as an output
  kick_wdt();
  rtc_init();

  printf("\r\n\n-------------------------------------\r\n");
  printf("| Hello,I am UPSMON_K22F_SIM7600E-H |\r\n");
  printf("-------------------------------------\r\n");
  printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("capture period : %d minutes\r\n", read_dipsw());

  _parser = new ATCmdParser(&mdm, "\r\n", 8000, 256);

  vrf_en = 1;

  while (mdm_status.read())
    ;
  printf("SIM7600 Status: Ready\r\n");

  bool mdmOK = false;
  bool mdmAtt = false;

  mdmOK = check_modem_status();

  /*
      if(_parser->send("AT+CTZU=1")&&_parser->recv("OK")){
          printf("Enable Automatic timezone update via NITZ\r\n");
      }
  */
  /*
if (_parser->send("AT+CTZU=0") && _parser->recv("OK")) {
  printf("Disable Automatic timezone update via NITZ\r\n");
}
  */
  if (mdmOK && set_full_FUNCTION()) {
    while (!check_attachNW())
      ;
    mdmAtt = true;
    printf("NW Attached!!!\r\n");
  }

  if (get_IMEI(imei) > 0) {
    printf("imei=  %s\r\n", imei);
  }

  if (get_ICCID(iccid) > 0) {
    printf("iccid=  %s\r\n", iccid);
  }

  while (get_creg() < 1)
    ;
  printf("NW Registered...\r\n");

  if (mdmAtt) {

    if (_parser->send("AT+CSQ") &&
        _parser->recv("+CSQ: %d,%d\r\n", &sig, &ber)) {
      printf("modem OK ---> sig=%d  ber=%d\r\n", sig, ber);
    }
  }

  if (_parser->send("AT+CREG=2") && _parser->recv("OK")) {
    printf("modem set---> AT+CREG=2\r\n");
  }

  ntp_sync();

bmqtt_start=mqtt_start();

  if (bmqtt_start) {
      mqtt_accquire_client(imei);

      char dns_ip[16];
      if(dns_resolve(mqtt_broker,dns_ip)<0){
          memset(dns_ip,0,16);
          strcpy(dns_ip,mqtt_broker_ip);
      }
      bmqtt_cnt=mqtt_connect(dns_ip, mqtt_usr, mqtt_pwd,mqtt_port);
  }



  capture_thread.start(callback(capture_thread_routine));
  blink_thread.start(mbed::callback(blink_routine));
  netstat_thread.start(mbed::callback(netstat_routine));
  pwake.fall(&fall_wake);

  while (true) {

    if (isWake) {
      kick_wdt();
      printf("wakeup!!!\r\n");
      isWake = false;

      char msg[32];
      if (_parser->send("AT+CCLK?") &&
          _parser->scanf("+CCLK: \"%[^\"]\"\r\n", msg)) {
        printf("msg= %s\r\n", msg);
        sync_rtc(msg);
        printf("timestamp : %d\r\n", (unsigned int)rtc_read());
      }

      char ret2[6];
      if (_parser->send("AT+CSQ") && _parser->recv("+CSQ: %[^\n]\r\n", ret2)) {
        printf("ret-> +CSQ: %s\r\n", ret2);
        sscanf(ret2, "%d, %d", &sig, &ber);
        printf("modem OK ---> sig=%d  ber=%d\r\n", sig, ber);
      }
      /*
            char msg2[128];
            if (_parser->send("AT+CREG?") &&
                _parser->scanf("+CREG: %[^\n]\r\n", msg2)) {
              printf("msg2=> +CREG: %s\r\n", msg2);
            }
      */
      get_creg();
      char cpsi[128];
      if (_parser->send("AT+CPSI?") &&
          _parser->scanf("+CPSI: %[^\n]\r\n", cpsi)) {
        printf("cpsi=> +CPSI: %s\r\n", cpsi);
      }

      char ipaddr[32];
      if (_parser->send("AT+CGPADDR=1") &&
          _parser->scanf("+CGPADDR: 1,%[^\n]\r\n", ipaddr)) {
        printf("ipaddr=> %s\r\n", ipaddr);
      }

      if (_parser->send("AT+NETOPEN?") && _parser->recv("+NETOPEN: 0")) {
        _parser->send("AT+NETOPEN") && _parser->recv("OK");
        printf("Starting... TCP/IP Service\r\n");
      }

      char ipaddrx[32];
      if (_parser->send("AT+IPADDR") &&
          _parser->scanf("+IPADDR: %[^\n]\r\n", ipaddrx)) {
        printf("ipaddrx=> %s\r\n", ipaddrx);
      }
    }
    /*
    if(pc.readable()){
        led=!led;
        pc.read(pc2uart, sizeof(pc2uart));
        mdm.write(pc2uart, sizeof(pc2uart));
    }

    if(mdm.readable()){
        mdm.read(uart2pc, sizeof(uart2pc));
        pc.write(uart2pc, sizeof(uart2pc));
    }
    */
  }
}

void printHEX(unsigned char *msg, unsigned int len) {

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

void publish_process(char payload[256]) {
  ThisThread::sleep_for(3s);
  if (bmqtt_cnt) {
    char str_topic[32];
    char str_payload[256];
    int len_topic = 0;
    int len_payload = 0;
    sprintf(str_topic, "UPSMON/%s", imei);
    // sprintf(str_payload, "I am %s", imei);
    strcpy(str_payload, payload);
    len_topic = strlen(str_topic);
    len_payload = strlen(str_payload);

    char cmd_pub_topic[32];
    char cmd_pub_msg[32];

    sprintf(cmd_pub_topic, "AT+CMQTTTOPIC=0,%d", len_topic);
    sprintf(cmd_pub_msg, "AT+CMQTTPAYLOAD=0,%d", len_payload);

    printf("cmd_pub_topic=%s" CRLF, cmd_pub_topic);
    _parser->send(cmd_pub_topic);

    if (_parser->recv(">")) {
      // if (_parser->send(cmd_pub_topic) && _parser->recv(">")) {
      //  if (_parser->send(cmd_pub_topic)) {
      /*
             char ret_pub[128];
             _parser->read(ret_pub, 128);
             printHEX((unsigned char *)ret_pub, 128);
     */
      //  _parser->write(str_topic, len_topic);
      //  if (_parser->recv("OK")) {
      if ((_parser->write(str_topic, len_topic) == len_topic) &&
          _parser->recv("OK")) {
        printf("set topic : %s Done!" CRLF, str_topic);
      }
    }

    printf("cmd_pub_msg=%s" CRLF, cmd_pub_msg);
    if (_parser->send(cmd_pub_msg) && _parser->recv(">")) {
      if ((_parser->write(str_payload, len_payload) == len_payload) &&
          _parser->recv("OK")) {
        printf("set pubmsg : %s Done!" CRLF, str_payload);
      }
    }
    ThisThread::sleep_for(1s);
    if (_parser->send("AT+CMQTTPUB=0,1,60") && _parser->recv("OK") &&
        _parser->recv("+CMQTTPUB: 0,0")) {
      printf("Publish msg Done!!!" CRLF);
    }
  }
}
