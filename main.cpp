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
#include "USBSerial.h"

#include "FATFileSystem.h"
#include "SPIFBlockDevice.h"

#define firmware_vers "631107"

#define INITIAL_APP_FILE "initial_script.txt"
#define SPIF_MOUNT_PATH "spif"
#define FULL_SCRIPT_FILE_PATH "/" SPIF_MOUNT_PATH "/" INITIAL_APP_FILE

// Blinking rate in milliseconds
#define BLINKING_RATE 500ms
#define CRLF "\r\n"

DigitalOut led(LED1, 1);
DigitalOut netstat(LED2, 0);

FATFileSystem fs(SPIF_MOUNT_PATH);
SPIFBlockDevice bd(MBED_CONF_SPIF_DRIVER_SPI_MOSI,
                   MBED_CONF_SPIF_DRIVER_SPI_MISO,
                   MBED_CONF_SPIF_DRIVER_SPI_CLK, MBED_CONF_SPIF_DRIVER_SPI_CS);

// static UnbufferedSerial pc(USBTX, USBRX);
static BufferedSerial mdm(MDM_TXD_PIN, MDM_RXD_PIN, 115200);
ATCmdParser *_parser;
Sim7600Cellular *modem;

BufferedSerial rs232(ECARD_TX_PIN, ECARD_RX_PIN, 2400);
ATCmdParser *xtc232;

DigitalOut vrf_en(MDM_VRF_EN_PIN, 0);
DigitalOut mdm_rst(MDM_RST_PIN);
DigitalOut mdm_pwr(MDM_PWR_PIN, 1);
DigitalOut mdm_flight(MDM_FLIGHT_PIN);
DigitalIn mdm_status(MDM_STATUS_PIN);

InterruptIn pwake(WDT_WAKE_PIN);
DigitalOut pdone(WDT_DONE_PIN, 0);
DigitalIn usb_det(USB_DET_PIN);

BusIn dipsw(DIPSW_P4_PIN, DIPSW_P3_PIN, DIPSW_P2_PIN, DIPSW_P1_PIN);

volatile bool isWake = false;
volatile bool is_script_read = false;

// char *pc2uart = new char[1];
// char *uart2pc = new char[1];

int sig = 0, ber = 0;
char ret_cgdcont[128];

unsigned int last_ntp_sync = 0;
unsigned int last_rtc_check_NW = 0;
// float last_tme_check_NW = 0.0;

char imei[16];
char iccid[20];

char cpsi3[128];
char str_topic[64];
char mqtt_msg[256];

// char usb_ret[128];
volatile bool is_usb_cnnt = false;
volatile bool is_msg_usb = false;
volatile char is_idle_rs232 = true;

// const char *str_cmd[] = {"Q1", "Q4", "QF"}; // 1phase
const char *str_ret[] = {
    "(221.0 204.0 219.0 000 50.9 2.26 27.0 00000000",
    "(222.7 000.0 000.0 204.0 222.0 000 000 50.9 382 384 108.4 27.0 IM",
    "(07 204.0 49.8 208.6 50.3 152 010.5 433 414 100.2 03.4 01111111",
    "NO_RESP1", "NO_RESP2"};

bool bmqtt_start = false;
bool bmqtt_cnt = false;

volatile int period_ms = 1000;
volatile float duty = 0.9;

// Timer tme1;

Thread blink_thread, netstat_thread, capture_thread, usb_thread;

init_script_t init_script;
struct tm struct_tm;

Mail<mail_t, 16> mail_box;
Mail<mail_t, 8> ret_usb_mail;

void printHEX(unsigned char *msg, unsigned int len);
int read_xtc_to_char(char *tbuf, int size, char end);
void read_initial_script();
void apply_script(FILE *file);

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

void netstat_led(netstat_mode inp = IDLE) {
  switch (inp) {
  case IDLE:
    period_ms = 400;
    duty = 0.95;
    break;
  case SENDING:
    period_ms = 400;
    duty = 0.5;
    break;
  default:
    period_ms = 2000;
    duty = 0.1;
  }
}

void blip_netstat(DigitalOut *led) {
  while (true) {
    *led = 1;
    ThisThread::sleep_for(chrono::milliseconds((int)(duty * period_ms)));

    *led = 0;
    ThisThread::sleep_for(chrono::milliseconds((int)((1 - duty) * period_ms)));
  }
}

void usb_passthrough() {
  printf("start usb_passthrough Thread...\r\n");
  char str_usb_ret[128];
  USBSerial pc2;

  uint8_t a, b;
  // char *a = new char[1];
  while (true) {

    while (pc2.connected()) {
      is_usb_cnnt = true;
      //   if (is_msg_usb) {
      //     is_msg_usb = false;
      //     pc2.printf("%s\n", usb_ret);
      //   }

      mail_t *xmail = ret_usb_mail.try_get_for(Kernel::Clock::duration(100));
      if (xmail != nullptr) {
        printf("usb_resp : %s" CRLF, xmail->resp);
        pc2.printf("%s\n", xmail->resp);
        ret_usb_mail.free(xmail);
      }

      if (is_idle_rs232) {
        //   while (is_idle_rs232) {

        if (pc2.readable()) {
          a = pc2.getc();
          rs232.write(&a, 1);
        }
        if (rs232.readable()) {

          //   b = rs232.read(&b, 1);
          //   pc2.write(&b, 1);
          memset(str_usb_ret, 0, 128);
          read_xtc_to_char(str_usb_ret, 250, '\n');
          pc2.printf("%s\n", str_usb_ret);
        }
      }
    }

    is_usb_cnnt = false;
    ThisThread::sleep_for(chrono::milliseconds(3000));
  }
}

void capture_thread_routine() {

  char ret_rs232[128];
  char str_cmd[6][20];
  char delim[] = ",";
  char *ptr;
  ptr = strtok(init_script.full_cmd, delim);
  int n_cmd = 0;

  while (ptr != 0) {
    // strcpy(str_cmd_buff[k], ptr);
    sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  while (true) {
    printf("*********************************" CRLF);
    printf("capture---> timestamp: %d" CRLF, (unsigned int)rtc_read());
    printf("*********************************" CRLF);

    is_idle_rs232 = false;

    for (int j = 0; j < n_cmd; j++) {
      mail_t *mail = mail_box.try_alloc();

      memset(ret_rs232, 0, 128);
      memset(mail->cmd, 0, 16);
      memset(mail->resp, 0, 128);

      mail->utc = (unsigned int)rtc_read();
      strcpy(mail->cmd, str_cmd[j]);
      // strcpy(mail->resp, dummy_msg);
      xtc232->send(mail->cmd);

      read_xtc_to_char(ret_rs232, 128, '\n');
      //   strcpy(ret_rs232, str_ret[j]);

      if (strlen(ret_rs232) > 0) {
        strcpy(mail->resp, ret_rs232);

        // memset(usb_ret, 0, 128);
        // strcpy(usb_ret, ret_rs232);

        is_msg_usb = true;

        // <---------- mail for usb return msg ------------->
        mail_t *xmail = ret_usb_mail.try_alloc();
        memset(xmail->resp, 0, 128);
        strcpy(xmail->resp, mail->resp);

        if (is_usb_cnnt) {
          ret_usb_mail.put(xmail);
        } else {
          ret_usb_mail.free(xmail);
        }
        // <------------------------------------------------>

        // printf("cmd=%s : ret=%s" CRLF, mail->cmd, mail->resp);
        mail_box.put(mail);

      } else {
        mail_box.free(mail);
      }
    }

    //   mail_box.put(mail);
    is_idle_rs232 = true;

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
    if (is_script_read) {
      ThisThread::sleep_for(BLINKING_RATE);
    } else {
      ThisThread::sleep_for(chrono::milliseconds(BLINKING_RATE / 5));
    }
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

int main() {

  // Initialise the digital pin LED1 as an output
  kick_wdt();
  rtc_init();

  printf("\r\n\n-------------------------------------\r\n");
  printf("| Hello,I am UPSMON_K22F_SIM7600E-H |\r\n");
  printf("-------------------------------------\r\n");
  printf("Firmware Version: %s" CRLF, firmware_vers);
  printf("SystemCoreClock : %.3f MHz.\r\n", SystemCoreClock / 1000000.0);
  printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("capture period : %d minutes\r\n", read_dipsw());

  read_initial_script();

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  //_parser->debug_on(1);
  modem = new Sim7600Cellular(_parser);
  // modem=new Sim7600Cellular(PTD3,PTD2);

  xtc232 = new ATCmdParser(&rs232, "\n", 128, 1000);

  vrf_en = 1;

  while (mdm_status.read())
    ;
  printf("SIM7600 Status: Ready\r\n");

  bool mdmOK = false;
  bool mdmAtt = false;
  char revID[20];

  mdmOK = modem->check_modem_status();

  netstat_thread.start(callback(blip_netstat, &netstat));

  modem->get_revID(revID);
  modem->set_tz_update(0);

  if (mdmOK && modem->set_full_FUNCTION()) {
    while (!modem->check_attachNW())
      ;
    mdmAtt = true;
    printf("NW Attached!!!\r\n");
  }

  if (modem->get_IMEI(imei) > 0) {
    printf("imei=  %s\r\n", imei);
  }

  if (modem->get_ICCID(iccid) > 0) {
    printf("iccid=  %s\r\n", iccid);
  }

  while (modem->get_creg() != 1)
    ;
  printf("NW Registered...\r\n");
  netstat_led(SENDING);

  if (mdmAtt) {
    modem->get_csq(&sig, &ber);
    printf("sig=%d ber=%d\r\n", sig, ber);
  }

  modem->set_creg(2);

  ntp_sync();
  last_ntp_sync = (unsigned int)rtc_read();

  bmqtt_start = modem->mqtt_start();

  if (bmqtt_start) {
    modem->mqtt_accquire_client(imei);

    char dns_ip[16];
    // if (modem->dns_resolve(mqtt_broker, dns_ip) < 0) {
    if (modem->dns_resolve(init_script.broker, dns_ip) < 0) {
      memset(dns_ip, 0, 16);
      strcpy(dns_ip, mqtt_broker_ip);
    }
    // bmqtt_cnt = modem->mqtt_connect(dns_ip, mqtt_usr, mqtt_pwd, mqtt_port);
    bmqtt_cnt = modem->mqtt_connect(dns_ip, init_script.usr, init_script.pwd,
                                    init_script.port);
  }

  last_rtc_check_NW = (unsigned int)rtc_read();
  // last_tme_check_NW = tme1.read();

  capture_thread.start(callback(capture_thread_routine));
  blink_thread.start(mbed::callback(blink_routine));

  usb_thread.start(callback(usb_passthrough));

  pwake.fall(&fall_wake);

  while (true) {

    if (isWake) {
      kick_wdt();
      printf("wakeup!!!\r\n");
      isWake = false;
    }

    mail_t *mail = mail_box.try_get_for(Kernel::Clock::duration(500));
    if (mail != nullptr) {
      printf("utc : %d" CRLF, mail->utc);
      printf("cmd : %s" CRLF, mail->cmd);
      printf("resp : %s" CRLF, mail->resp);

      memset(str_topic, 0, 64);
      memset(mqtt_msg, 0, 256);

      //   sprintf(str_topic, "UPSMON/%s", imei);
      sprintf(str_topic, "%s/%s", init_script.topic_path, imei);
      //   sprintf(mqtt_msg, payload_pattern, imei, mail->utc, model_Name,
      //   site_ID,
      //           mail->cmd, mail->resp);
      sprintf(mqtt_msg, payload_pattern, imei, mail->utc, init_script.model,
              init_script.siteID, mail->cmd, mail->resp);
      //   bmqtt_cnt = (modem->mqtt_connect_stat() == 1) ? true : false;

      if (bmqtt_cnt) {
        modem->mqtt_publish(str_topic, mqtt_msg);
      }
      mail_box.free(mail);
    }

    // if ((tme1.read() - last_tme_check_NW) > 55.0) {
    if (((unsigned int)rtc_read() - last_rtc_check_NW) > 55) {
      last_rtc_check_NW = (unsigned int)rtc_read();
      // last_tme_check_NW = tme1.read();

      printf(CRLF "<----- Checking NW. Status ----->" CRLF);
      printf("timestamp : %d\r\n", (unsigned int)rtc_read());

      //   if (modem->get_creg() == 1) {
      if (modem->check_attachNW()) {
        modem->get_creg();

        char msg[32];
        if (_parser->send("AT+CCLK?") &&
            _parser->scanf("+CCLK: \"%[^\"]\"\r\n", msg)) {
          printf("msg= %s\r\n", msg);
          sync_rtc(msg);
          printf("timestamp : %d\r\n", (unsigned int)rtc_read());
        }

        modem->get_csq(&sig, &ber);
        printf("sig=%d ber=%d\r\n", sig, ber);

        memset(cpsi3, 0, 128);

        if (modem->get_cpsi(cpsi3) > 0) {
          printf("cpsi=> %s\r\n", cpsi3);
        }

        char ipaddr[32];
        if (modem->get_IPAddr(ipaddr) > 0) {
          netstat_led(SENDING);
          printf("ipaddr= %s\r\n", ipaddr);
        }

        if (((unsigned int)rtc_read() - last_ntp_sync) > 3600) {
          ntp_sync();
          last_ntp_sync = (unsigned int)rtc_read();
        }

        if (modem->mqtt_connect_stat() < 1) {
          char dns_ip[16];
          //   if (modem->dns_resolve(mqtt_broker, dns_ip) < 0) {
          if (modem->dns_resolve(init_script.broker, dns_ip) < 0) {
            memset(dns_ip, 0, 16);
            strcpy(dns_ip, mqtt_broker_ip);
          }
          bmqtt_cnt =
              //   modem->mqtt_connect(dns_ip, mqtt_usr, mqtt_pwd, mqtt_port);
              bmqtt_cnt = modem->mqtt_connect(
                  dns_ip, init_script.usr, init_script.pwd, init_script.port);
        }

        if (modem->ping_dstNW("8.8.8.8") < 1000) {
          bmqtt_cnt = (bmqtt_cnt && true);
        }

        if (!bmqtt_cnt) {
          vrf_en = 0;
          ThisThread::sleep_for(100ms);
          vrf_en = 1;
        }

      } else {
        netstat_led(IDLE);
        mdm_flight = 1;
        ThisThread::sleep_for(500ms);
        mdm_flight = 0;
        bmqtt_cnt = false;
        printf("NW Connection Fail: On/Off Flight Mode" CRLF);
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

int read_xtc_to_char(char *tbuf, int size, char end) {
  int count = 0;
  int x = 0;

  if (size > 0) {
    for (count = 0; (count < size) && (x >= 0) && (x != end); count++) {
      x = xtc232->getc();
      *(tbuf + count) = (char)x;
    }

    count--;
    *(tbuf + count) = 0;

    // Convert line endings:
    // If end was '\n' (0x0a) and the preceding character was 0x0d, then
    // overwrite that with null as well.
    if ((count > 0) && (end == '\n') && (*(tbuf + count - 1) == '\x0d')) {
      count--;
      *(tbuf + count) = 0;
    }
  }

  return count;
}

void read_initial_script() {

  bd.init();
  int err = fs.mount(&bd);

  if (err) {
    printf("%s filesystem mount failed\r\n", fs.getName());
  }

  FILE *file = fopen(FULL_SCRIPT_FILE_PATH, "r");

  if (file != NULL) {
    printf("initial script found\r\n");

    // apply_update(file, POST_APPLICATION_ADDR );
    apply_script(file);
    // remove(FULL_UPDATE_FILE_PATH);
  }
  //  else {
  //     printf("No update found to apply\r\n");
  // }

  fs.unmount();
  bd.deinit();
}

void apply_script(FILE *file) {
  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  printf("initial script file size is %ld bytes\r\n", len);
  fseek(file, 0, SEEK_SET);

  int result = 0;
  // allocate memory to contain the whole file:
  char *buffer = (char *)malloc(sizeof(char) * len);
  if (buffer == NULL) {
    printf("malloc : Memory error" CRLF);
  }

  // copy the file into the buffer:
  result = fread(buffer, 1, len, file);
  if (result != len) {
    printf("file %s : Reading error" CRLF, FULL_SCRIPT_FILE_PATH);
  }

  //   printHEX((unsigned char *)buffer, len);
  //   printf("buffer -> \r\n%s\r\n\n", buffer);

  char script_buff[len];
  int idx = 0;
  while ((strncmp(&buffer[idx], "START:", 6) != 0) && (idx < len - 6)) {
    idx++;
  }
  if (idx < len) {
    memcpy(&script_buff, &buffer[idx], len - idx);
    // printf("script_buf: %s" CRLF, script_buff);
  }

  //   if (sscanf(script_buff, init_cfg_pattern, init_script.broker,
  //              &init_script.port, init_script.usr, init_script.pwd,
  //              init_script.topic_path, init_script.full_cmd,
  //              init_script.model, init_script.siteID) == 8) {
  if (sscanf(script_buff, init_cfg_pattern, init_script.broker,
             &init_script.port, init_script.topic_path, init_script.full_cmd,
             init_script.model, init_script.siteID) == 6) {
    if (init_script.topic_path[strlen(init_script.topic_path) - 1] == '/') {
      init_script.topic_path[strlen(init_script.topic_path) - 1] = '\0';
    }

    strcpy(init_script.usr, mqtt_usr);
    strcpy(init_script.pwd, mqtt_pwd);

    is_script_read = true;

    printf("\r\n<-------------------------------->\r\n");
    printf("    broker: %s" CRLF, init_script.broker);
    printf("    port: %d" CRLF, init_script.port);
    // printf("    usr: %s" CRLF, init_script.usr);
    // printf("    pwd: %s" CRLF, init_script.pwd);
    printf("    topic_path: %s" CRLF, init_script.topic_path);
    printf("    full_cmd: %s" CRLF, init_script.full_cmd);
    printf("    model: %s" CRLF, init_script.model);
    printf("    siteID: %s" CRLF, init_script.siteID);
    printf("<-------------------------------->\r\n\n");
  }

  /* the whole file is now loaded in the memory buffer. */

  // terminate
  fclose(file);
  free(buffer);
}
