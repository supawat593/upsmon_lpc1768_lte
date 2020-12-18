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

#define firmware_vers "631218"
#define Dev_Group "LTE"

#define INITIAL_APP_FILE "initial_script.txt"
#define SPIF_MOUNT_PATH "spif"
#define FULL_SCRIPT_FILE_PATH "/" SPIF_MOUNT_PATH "/" INITIAL_APP_FILE

// Blinking rate in milliseconds
#define BLINKING_RATE 500ms
#define CRLF "\r\n"

DigitalOut myled(LED1, 1);
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

DigitalOut vrf_en(MDM_VRF_EN_PIN);
DigitalOut mdm_rst(MDM_RST_PIN);
DigitalOut mdm_pwr(MDM_PWR_PIN, 1);
DigitalOut mdm_flight(MDM_FLIGHT_PIN);
// DigitalOut mdm_flight(MDM_FLIGHT_PIN, 1);
DigitalIn mdm_status(MDM_STATUS_PIN);

DigitalOut mdm_dtr(MDM_DTR_PIN);
DigitalIn mdm_ri(MDM_RI_PIN);

InterruptIn pwake(WDT_WAKE_PIN);
DigitalOut pdone(WDT_DONE_PIN, 0);
DigitalIn usb_det(USB_DET_PIN);

BusIn dipsw(DIPSW_P4_PIN, DIPSW_P3_PIN, DIPSW_P2_PIN, DIPSW_P1_PIN);

volatile bool isWake = false;
volatile bool is_script_read = false;

// char *pc2uart = new char[1];
// char *uart2pc = new char[1];

int period_min = 0;
int sig = 0, ber = 0;
char ret_cgdcont[128];

unsigned int last_ntp_sync = 0;
unsigned int last_rtc_check_NW = 0;
unsigned int rtc_uptime = 0;

char revID[20];
bool mdmOK = false;
bool mdmAtt = false;

char imei[16];
char iccid[20];
char dns_ip[16];

char msg_cpsi[128];
char str_topic[128];
char mqtt_msg[256];

// char usb_ret[128];
volatile bool is_usb_cnnt = false;
volatile char is_idle_rs232 = true;

// const char *str_cmd[] = {"Q1", "Q4", "QF"}; // 1phase
const char *str_ret[] = {
    "(221.0 204.0 219.0 000 50.9 2.26 27.0 00000000",
    "(222.7 000.0 000.0 204.0 222.0 000 000 50.9 382 384 108.4 27.0 IM",
    "(07 204.0 49.8 208.6 50.3 152 010.5 433 414 100.2 03.4 01111111",
    "NO_RESP1", "NO_RESP2"};

bool bmqtt_start = false;
bool bmqtt_cnt = false;

// volatile int period_ms = 500;
// volatile float duty = 0.05;

// Timer tme1;

Thread blink_thread, netstat_thread, capture_thread, usb_thread;
Mutex mutex_idle_rs232, mutex_usb_cnnt;

init_script_t init_script;
struct tm struct_tm;

typedef struct {
  int led_state;
  int period_ms;
  float duty;
} blink_t;

MemoryPool<int, 1> netstat_mpool;
Queue<int, 1> netstat_queue;

MemoryPool<blink_t, 1> blink_mpool;
Queue<blink_t, 1> blink_queue;

// Mail<mail_t, 16> mail_box;
Mail<mail_t, 8> mail_box, ret_usb_mail;

void printHEX(unsigned char *msg, unsigned int len);
int read_xtc_to_char(char *tbuf, int size, char end);
void read_initial_script();
void apply_script(FILE *file);
void device_stat_update(const char *stat_mode = "Normal");

bool get_idle_rs232() {
  bool temp;
  mutex_idle_rs232.lock();
  temp = is_idle_rs232;
  mutex_idle_rs232.unlock();
  return temp;
}

void set_idle_rs232(bool temp) {
  mutex_idle_rs232.lock();
  is_idle_rs232 = temp;
  mutex_idle_rs232.unlock();
}

bool get_usb_cnnt() {
  bool temp;
  mutex_usb_cnnt.lock();
  temp = is_usb_cnnt;
  mutex_usb_cnnt.unlock();
  return temp;
}

void set_usb_cnnt(bool temp) {
  mutex_usb_cnnt.lock();
  is_usb_cnnt = temp;
  mutex_usb_cnnt.unlock();
}

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

void netstat_led(netstat_mode inp = IDLE) {
  int *net_queue = netstat_mpool.try_alloc();

  switch (inp) {

  case IDLE:
    *net_queue = 1;
    netstat_queue.try_put(net_queue);
    break;
  case CONNECTED:
    *net_queue = 2;
    netstat_queue.try_put(net_queue);
    break;
  default:
    *net_queue = 0;
    netstat_queue.try_put(net_queue);
  }
}

void blip_netstat(DigitalOut *led) {
  int led_state = 1;

  while (true) {
    int *net_queue;

    if (netstat_queue.try_get_for(Kernel::Clock::duration(200), &net_queue)) {
      led_state = *net_queue;
      netstat_mpool.free(net_queue);
    }

    if (led_state == 1) {
      *led = 1;
    } else if (led_state == 2) {
      *led = !*led;
    } else {
      *led = 0;
    }
  }
}

void usb_passthrough() {
  printf("\r\n-------------------------------\r\n");
  printf("start usb_passthrough Thread...\r\n");
  printf("-------------------------------\r\n");
  char str_usb_ret[128];
  USBSerial pc2;

  uint8_t a, b;
  // char *a = new char[1];
  while (true) {

    while (pc2.connected()) {
      //   is_usb_cnnt = true;
      set_usb_cnnt(true);
      //     pc2.printf("%s\n", usb_ret);
      //   }

      mail_t *xmail = ret_usb_mail.try_get_for(Kernel::Clock::duration(100));
      if (xmail != nullptr) {
        printf("usb_resp : %s" CRLF, xmail->resp);
        pc2.printf("%s\r", xmail->resp);
        ret_usb_mail.free(xmail);
      }

      if (get_idle_rs232()) {
        //   if (is_idle_rs232) {
        //   while (is_idle_rs232) {

        if (pc2.readable()) {
          a = pc2.getc();
          rs232.write(&a, 1);
        }
        if (rs232.readable()) {

          //   b = rs232.read(&b, 1);
          //   pc2.write(&b, 1);
          memset(str_usb_ret, 0, 128);
          read_xtc_to_char(str_usb_ret, 128, '\r');
          pc2.printf("%s\r", str_usb_ret);
        }
      }
    }

    // is_usb_cnnt = false;
    set_usb_cnnt(false);
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

  while ((ptr != 0) && (n_cmd < 6)) {
    // strcpy(str_cmd_buff[k], ptr);
    sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  while (true) {
    // printf("*********************************" CRLF);
    // printf("capture---> timestamp: %d" CRLF, (unsigned int)rtc_read());
    // printf("*********************************" CRLF);

    // is_idle_rs232 = false;
    set_idle_rs232(false);

    for (int j = 0; j < n_cmd; j++) {
      ThisThread::sleep_for(1s);
      mail_t *mail = mail_box.try_alloc();

      memset(ret_rs232, 0, 128);
      memset(mail->cmd, 0, 16);
      memset(mail->resp, 0, 128);

      mail->utc = (unsigned int)rtc_read();
      strcpy(mail->cmd, str_cmd[j]);
      // strcpy(mail->resp, dummy_msg);
      xtc232->flush();
      xtc232->send(mail->cmd);

      read_xtc_to_char(ret_rs232, 128, '\r');
      //   strcpy(ret_rs232, str_ret[j]);

      if (strlen(ret_rs232) > 0) {

        // printf("cmd[%d][0]= 0x%02X , ret[0]= 0x%02X\r\n", j,
        // str_cmd[j][0],ret_rs232[0]);

        strcpy(mail->resp, ret_rs232);

        // memset(usb_ret, 0, 128);
        // strcpy(usb_ret, ret_rs232);

        // <---------- mail for usb return msg ------------->
        mail_t *xmail = ret_usb_mail.try_alloc();
        memset(xmail->resp, 0, 128);
        strcpy(xmail->resp, mail->resp);

        if (get_usb_cnnt()) {
          // if (is_usb_cnnt) {
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
    // is_idle_rs232 = true;
    set_idle_rs232(true);

    period_min = read_dipsw();
    if (period_min < 2) {
      period_min = 2;
    }

    ThisThread::sleep_for(chrono::minutes(period_min));
  }
}

void fall_wake() { isWake = true; }
void kick_wdt() {
  pdone = 1;
  wait_us(10);
  pdone = 0;
}

void blink_led(blink_mode inp = PWRON) {

  blink_t *led_queue = blink_mpool.try_alloc();

  switch (inp) {

  case NORMAL:
    led_queue->led_state = 1;
    led_queue->duty = 0.5;
    led_queue->period_ms = 1000;
    blink_queue.try_put(led_queue);
    break;
  case NOFILE:
    led_queue->led_state = 2;
    led_queue->duty = 0.5;
    led_queue->period_ms = 200;
    blink_queue.try_put(led_queue);
    break;
  default:
    led_queue->led_state = 0;
    led_queue->duty = 0.5;
    led_queue->period_ms = 1000;
    blink_queue.try_put(led_queue);
  }
}

void blink_routine(DigitalOut *led) {
  int led_state = 0;
  int period_ms = 1000;
  float duty = 0.5;
  while (true) {
    blink_t *led_queue;
    if (blink_queue.try_get_for(Kernel::Clock::duration(int(period_ms * duty)),
                                &led_queue)) {
      led_state = led_queue->led_state;
      period_ms = led_queue->period_ms;
      duty = led_queue->duty;
      blink_mpool.free(led_queue);
    }

    if (led_state == 0) {
      *led = 1;
    } else {
      *led = !*led;
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

  if ((int)sec_rtc > 0) {
    rtc_write(sec_rtc);
  }
}

void ntp_sync() {

  if (_parser->send("AT+CNTP=\"time1.google.com\",28") && _parser->recv("OK")) {
    printf("set ntp server Done\r\n");
    char ret_ntp[64];

    if (_parser->send("AT+CNTP?") &&
        _parser->scanf("+CNTP: %[^\n]\r\n", ret_ntp)) {
      printf("ret_ntp--> +CNTP: %s\r\n", ret_ntp);
      _parser->set_timeout(12000);

      if (_parser->send("AT+CNTP") && _parser->recv("+CNTP: 0")) {
        //   if (_parser->send("AT+CNTP") && _parser->recv("OK")) {
        printf("NTP: Operation succeeded\r\n");
        last_ntp_sync = (unsigned int)rtc_read();
      }
      _parser->set_timeout(8000);
    }
  }
}

int main() {

  // Initialise the digital pin LED1 as an output
  kick_wdt();
  rtc_init();
  period_min = read_dipsw();
  if (period_min < 2) {
    period_min = 2;
  }

  printf("\r\n\n-------------------------------------\r\n");
  printf("| Hello,I am UPSMON_K22F_SIM7600E-H |\r\n");
  printf("-------------------------------------\r\n");
  printf("Firmware Version: %s" CRLF, firmware_vers);
  printf("SystemCoreClock : %.3f MHz.\r\n", SystemCoreClock / 1000000.0);
  printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("capture period : %d minutes\r\n", period_min);
  blink_thread.start(mbed::callback(blink_routine, &myled));
  read_initial_script();
  if (!is_script_read) {
    blink_led(NOFILE);
  }

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  //_parser->debug_on(1);
  modem = new Sim7600Cellular(_parser);
  // modem=new Sim7600Cellular(PTD3,PTD2);

  xtc232 = new ATCmdParser(&rs232, "\r", 256, 1000);
  ThisThread::sleep_for(500ms);
  vrf_en = 1;
  rtc_uptime = (unsigned int)rtc_read();
  // while (mdm_status.read() && ((unsigned int)rtc_read() - rtc_uptime < 18));
  while (mdm_status.read() && ((unsigned int)rtc_read() - rtc_uptime < 18))
    ;

  if ((unsigned int)rtc_read() - rtc_uptime > 18) {
    printf("MDM_STAT Timeout : %d sec.\r\n",
           (unsigned int)rtc_read() - rtc_uptime);
    rtc_uptime = (unsigned int)rtc_read();
  }
  mdmOK = modem->check_modem_status();

  if (mdmOK) {
    printf("SIM7600 Status: Ready\r\n");
    netstat_thread.start(callback(blip_netstat, &netstat));

    if (modem->enable_echo(0)) {

      //   modem->set_min_cFunction();
      //   ThisThread::sleep_for(1000ms);

      //   if (modem->get_pref_Mode() == 2) {
      //     modem->set_pref_Mode(54);
      //     ThisThread::sleep_for(3000ms);
      //     modem->get_pref_Mode();
      //   }

      if (!modem->save_setting()) {
        printf("Save ME user setting : Fail!!!" CRLF);
      }
    }
  }

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
  modem->set_creg(2);
  while (modem->get_creg() != 1)
    ;
  printf("NW Registered...\r\n");
  netstat_led(CONNECTED);

  if (mdmAtt) {
    modem->get_csq(&sig, &ber);
    printf("sig=%d ber=%d\r\n", sig, ber);
  }

  //   modem->set_creg(2);
  //   modem->get_creg();

  ntp_sync();
  //   last_ntp_sync = (unsigned int)rtc_read();
  last_rtc_check_NW = (unsigned int)rtc_read();

  bmqtt_start = modem->mqtt_start();
  debug_if(bmqtt_start, "MQTT Started\r\n");

  if (bmqtt_start) {
    modem->mqtt_accquire_client(imei);

    if (modem->dns_resolve(init_script.broker, dns_ip) < 0) {
      memset(dns_ip, 0, 16);
      strcpy(dns_ip, mqtt_broker_ip);
    }

    bmqtt_cnt = modem->mqtt_connect(dns_ip, init_script.usr, init_script.pwd,
                                    init_script.port);
    debug_if(bmqtt_cnt, "MQTT Connected\r\n");
    device_stat_update("RESTART");
  }

  if (is_script_read) {
    blink_led(NORMAL); //  normal Mode
  }

  capture_thread.start(callback(capture_thread_routine));
  //   blink_thread.start(mbed::callback(blink_routine));
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

      memset(str_topic, 0, 128);
      memset(mqtt_msg, 0, 256);

      //   sprintf(str_topic, "UPSMON/%s", imei);
      sprintf(str_topic, "%s/data/%s", init_script.topic_path, imei);
      //   sprintf(mqtt_msg, payload_pattern, imei, mail->utc, model_Name,
      //   site_ID,
      //           mail->cmd, mail->resp);
      sprintf(mqtt_msg, payload_pattern, imei, mail->utc, init_script.model,
              init_script.siteID, mail->cmd, mail->resp);
      //   bmqtt_cnt = (modem->mqtt_connect_stat() == 1) ? true : false;

      if (bmqtt_cnt) {
        modem->mqtt_publish(str_topic, mqtt_msg);
        printf("< ------------------------------------------------ >" CRLF);
      }
      mail_box.free(mail);
    }
    //  else NullPtr
    else {
      if (((unsigned int)rtc_read() - last_rtc_check_NW) > 55) {
        last_rtc_check_NW = (unsigned int)rtc_read();
        printf(CRLF "<----- Checking NW. Status ----->" CRLF);
        printf("timestamp : %d\r\n", (unsigned int)rtc_read());

        if (!modem->check_modem_status(10)) {
          netstat_led(OFF);
          vrf_en = 0;
          ThisThread::sleep_for(500ms);
          vrf_en = 1;
          bmqtt_start = false;
          bmqtt_cnt = false;
          rtc_uptime = (unsigned int)rtc_read();

          while (mdm_status.read() &&
                 ((unsigned int)rtc_read() - rtc_uptime < 18))
            ;

          if ((unsigned int)rtc_read() - rtc_uptime > 18) {
            printf("MDM_STAT Timeout : %d sec.\r\n",
                   (unsigned int)rtc_read() - rtc_uptime);
            rtc_uptime = (unsigned int)rtc_read();
          }

          mdmOK = modem->check_modem_status();

          if (mdmOK) {
            printf("Power re-attached -> SIM7600 Status: Ready\r\n");
            netstat_led(IDLE);

            if (modem->set_full_FUNCTION()) {
              printf("AT Command and set full_function : OK\r\n");
              modem->set_creg(2);
              mdmAtt = modem->check_attachNW();
            }
          }

        } else {
          char msg[32];

          if (_parser->send("AT+CCLK?") &&
              _parser->scanf("+CCLK: \"%[^\"]\"\r\n", msg)) {
            printf("msg= %s\r\n", msg);
            sync_rtc(msg);
            printf("timestamp : %d\r\n", (unsigned int)rtc_read());
          }

          modem->get_csq(&sig, &ber);
          printf("sig=%d ber=%d\r\n", sig, ber);
          memset(msg_cpsi, 0, 128);

          if (modem->get_cpsi(msg_cpsi) > 0) {
            printf("cpsi=> %s\r\n", msg_cpsi);
          }

          if (modem->check_attachNW() && (modem->get_creg() == 1)) {

            char ipaddr[32];

            if (modem->get_IPAddr(ipaddr) > 0) {
              netstat_led(CONNECTED);
              printf("ipaddr= %s\r\n", ipaddr);
            }

            // if (((unsigned int)rtc_read() - last_ntp_sync) > 3600) {
            //   ntp_sync();
            //   last_ntp_sync = (unsigned int)rtc_read();
            // }

            if (!bmqtt_start) {

              if (modem->check_attachNW() && (modem->get_creg() == 1)) {

                netstat_led(CONNECTED);

                if (modem->dns_resolve(init_script.broker, dns_ip) < 0) {
                  memset(dns_ip, 0, 16);
                  strcpy(dns_ip, mqtt_broker_ip);
                }

                bmqtt_start = modem->mqtt_start();
                debug_if(bmqtt_start, "MQTT Started\r\n");

                if (bmqtt_start) {
                  modem->mqtt_accquire_client(imei);

                  bmqtt_cnt =
                      modem->mqtt_connect(dns_ip, init_script.usr,
                                          init_script.pwd, init_script.port);
                  debug_if(bmqtt_cnt, "MQTT Connected\r\n");
                }
              }
            }

            if (modem->mqtt_isdisconnect() < 1) {
              bmqtt_cnt = false;

              if (modem->mqtt_release()) {

                if (modem->mqtt_stop()) {
                  bmqtt_start = false;
                } else {
                  bmqtt_start = false;
                  mdm_rst = 1;
                  ThisThread::sleep_for(500ms);
                  mdm_rst = 0;
                  printf("MQTT Stop Fail: Rebooting Modem!!!" CRLF);

                  rtc_uptime = (unsigned int)rtc_read();
                  while (mdm_status.read() &&
                         ((unsigned int)rtc_read() - rtc_uptime < 18))
                    ;

                  if (modem->check_modem_status(10)) {
                    printf("Restart Modem Complete : AT Ready!!!" CRLF);
                    modem->set_cops();
                    netstat_led(IDLE);
                  }
                }
              }
            } else {
              bmqtt_cnt = true;
              char ret_cnnt_stat[128];
              modem->mqtt_connect_stat(ret_cnnt_stat);
              printf("ret_cnnt_stat-> %s\r\n", ret_cnnt_stat);
            }

            if (modem->ping_dstNW("8.8.8.8") < 1000) {
              bmqtt_cnt = (bmqtt_cnt && true);
            }

            if (((unsigned int)rtc_read() - last_ntp_sync) > 3600) {
              ntp_sync();
              last_ntp_sync = (unsigned int)rtc_read();
              device_stat_update();
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
      }
    } // end else NullPtr
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

    printf("\r\n<---------------------------------------->\r\n");
    printf("    broker: %s" CRLF, init_script.broker);
    printf("    port: %d" CRLF, init_script.port);
    // printf("    usr: %s" CRLF, init_script.usr);
    // printf("    pwd: %s" CRLF, init_script.pwd);
    printf("    topic_path: %s" CRLF, init_script.topic_path);
    printf("    full_cmd: %s" CRLF, init_script.full_cmd);
    printf("    model: %s" CRLF, init_script.model);
    printf("    siteID: %s" CRLF, init_script.siteID);
    printf("<---------------------------------------->\r\n\n");
  }

  /* the whole file is now loaded in the memory buffer. */

  // terminate
  fclose(file);
  free(buffer);
}

void device_stat_update(const char *stat_mode) {
  if (bmqtt_cnt) {
    char stat_payload[256];
    char stat_topic[128];
    char cereg_msg[64];
    char cops_msg[64];
    char str_stat[15];
    strcpy(str_stat,stat_mode);
    sprintf(stat_topic, "%s/status/%s", init_script.topic_path, imei);
    modem->set_cereg(2);
    modem->get_csq(&sig, &ber);
    modem->get_cops(cops_msg);
    if (modem->get_cereg(cereg_msg) > 0) {
      sprintf(stat_payload, stat_pattern, imei, (unsigned int)rtc_read(),
              firmware_vers, Dev_Group, str_stat, sig, ber, cops_msg,
              cereg_msg);
      modem->mqtt_publish(stat_topic, stat_payload);
    }
  }
}
