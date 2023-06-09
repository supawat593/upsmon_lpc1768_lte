/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "upsmon_rtos.h"
#include <chrono>
#include <cstdio>
#include <cstring>

#include "./TimerTPL5010/TimerTPL5010.h"
#include "Sim7600Cellular.h"
#include "USBSerial.h"

#include "FATFileSystem.h"
#include "SPIFBlockDevice.h"

#include "Base64.h"

#define firmware_vers "660106"
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
TimerTPL5010 tpl5010(WDT_WAKE_PIN, WDT_DONE_PIN);

BufferedSerial rs232(ECARD_TX_PIN, ECARD_RX_PIN, 2400);
ATCmdParser *xtc232;

DigitalOut vrf_en(MDM_VRF_EN_PIN);
DigitalOut mdm_rst(MDM_RST_PIN);
DigitalOut mdm_pwr(MDM_PWR_PIN, 1);
DigitalOut mdm_flight(MDM_FLIGHT_PIN);
DigitalIn mdm_status(MDM_STATUS_PIN);

DigitalOut mdm_dtr(MDM_DTR_PIN);
DigitalIn mdm_ri(MDM_RI_PIN);

// InterruptIn pwake(WDT_WAKE_PIN);
// DigitalOut pdone(WDT_DONE_PIN, 0);
DigitalIn usb_det(USB_DET_PIN);

BusIn dipsw(DIPSW_P4_PIN, DIPSW_P3_PIN, DIPSW_P2_PIN, DIPSW_P1_PIN);

// volatile bool isWake = false;
volatile bool is_script_read = false;
bool mdmOK = false;
bool mdmAtt = false;
bool bmqtt_start = false;
bool bmqtt_cnt = false;
bool bmqtt_sub = false;

int period_min = 0;
int sig = 0, ber = 0;

unsigned int last_ntp_sync = 0;
unsigned int last_rtc_check_NW = 0;
unsigned int rtc_uptime = 0;

char imei[16];
char iccid[20];
char ipaddr[32];
char dns_ip[16];

char encode_key[64];
char revID[20];
char ret_cgdcont[128];
char msg_cpsi[128];
char str_topic[128];
char mqtt_msg[256];
char str_sub_topic[128];

Thread blink_thread, netstat_thread, capture_thread, usb_thread,
    mdm_notify_thread;
Thread isr_thread(osPriorityAboveNormal, 0x400, nullptr, "isr_queue_thread");
EventQueue isr_queue;

init_script_t init_script, iap_init_script;
struct tm struct_tm;

Base64 base64_obj;

int read_parser_to_char(char *tbuf, int size, char end);
int read_xtc_to_char(char *tbuf, int size, char end);
void read_initial_script();
void apply_script(FILE *file);
void device_stat_update(const char *stat_mode = "NORMAL");
void write_init_script();

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

int detection_notify(const char *keyword, char *src, char *dst) {
  int st = 0, end = 0;
  int len_keyword = 0;
  len_keyword = strlen(keyword);

  int len_src = 0;
  if (strlen(src) > 1) {
    len_src = strlen(src);
  } else {
    len_src = 512;
  }

  //   printf("Notify: len_src=%d\r\n", len_src);
  // printHEX((unsigned char *)src, len_src);

  char end_text[] = {0x0d, 0x0a, 0x00, 0x00};
  st = 0;
  end = 0;

  while ((strncmp(&src[st], keyword, len_keyword) != 0) && (st < len_src)) {
    st++;
  }

  end = st;
  while ((strncmp(&src[end], end_text, 4) != 0) && (end < len_src)) {
    end++;
  }

  if (st < len_src) {
    memcpy(&dst[0], &src[st], end - st + 2);
    return 1;
  } else {
    strcpy(dst, "");
    return 0;
  }
}

void usb_passthrough() {
  printf("\r\n----------------------------------\r\n");
  printf("start usb_passthrough Thread...\r\n");
  printf("----------------------------------\r\n");
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

  char full_cmd[64];
  strcpy(full_cmd, init_script.full_cmd);

  char delim[] = ",";
  char *ptr;
  //   ptr = strtok(init_script.full_cmd, delim);
  ptr = strtok(full_cmd, delim);
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

void script_config_process(char cfg_msg[512]) {
  char str_cmd[10][100];
  char raw_key[64];
  char delim[] = "&";
  char *ptr;
  //   ptr = strtok(init_script.full_cmd, delim);
  ptr = strtok(cfg_msg, delim);
  int n_cmd = 0;

  while ((ptr != 0) && (n_cmd < 10)) {
    strcpy(str_cmd[n_cmd], ptr);
    // sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  int cfg_success = 0;

  for (int i = 0; i < n_cmd; i++) {
    if (sscanf(str_cmd[i], "Command: [%[^]]]", init_script.full_cmd) == 1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Topic: \"%[^\"]\"",
                      init_script.topic_path) == 1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Model: \"%[^\"]\"", init_script.model) ==
               1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Site_ID: \"%[^\"]\"", init_script.siteID) ==
               1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Key: \"%[^\"]\"", raw_key) == 1) {
      char raw_usr[16], raw_pwd[16];

      size_t len_key_encode = (size_t)strlen(raw_key);
      size_t len_key_decode;
      char *key_decode1 =
          base64_obj.Decode(raw_key, len_key_encode, &len_key_decode);

      size_t len_key_decode2;
      char *key_decode2 =
          base64_obj.Decode(key_decode1, len_key_decode, &len_key_decode2);

      if (sscanf(key_decode2, "%[^ ] %[^\n]", raw_usr, raw_pwd) == 2) {
        memset(encode_key, 0, 64);
        strcpy(encode_key, raw_key);
        printf("configuration process: Key Changed" CRLF);
        cfg_success++;
      }
    } else {
      printf("Not Matched!\r\n");
    }
  }

  if (cfg_success > 0) {
    write_init_script();
    system_reset();
  }
}

void mdm_notify_routine() {
  printf("mdm_notify_routine() started --->\r\n");
  char mdm_xbuf[512];
  char xbuf_trim[512];
  char sub_topic[128];
  char sub_payload[256];
  int client_idx = 0;
  int len_topic = 0;
  int len_payload = 0;
  int st = 0, end = 0;

  while (true) {
    // if (get_notify_ready() && (!get_mdm_busy()) && mdm.readable()) {
    if (get_notify_ready() && (!get_mdm_busy())) {
      if (mdm.readable()) {
        set_mdm_busy(true);
        memset(mdm_xbuf, 0, 512);
        memset(xbuf_trim, 0, 512);

        // _parser->debug_on(1);
        _parser->set_timeout(1000);
        _parser->read(mdm_xbuf, 512);
        //   read_parser_to_char(mdm_xbuf, 512, '\n');
        _parser->set_timeout(8000);
        // _parser->debug_on(0);

        if (detection_notify("+CMQTTRXSTART:", mdm_xbuf, xbuf_trim)) {

          if (sscanf(xbuf_trim, mqtt_sub_pattern, &len_topic, sub_topic,
                     &len_payload, sub_payload, &client_idx) == 5) {
            printf(
                "\r\n< -------------------------------------------------\r\n");
            printf("len_topic=%d\r\n", len_topic);
            printf("sub_topic=%s\r\n", sub_topic);
            printf("len_payload=%d\r\n", len_payload);
            printf("sub_payload=%s\r\n", sub_payload);
            printf("client_idx=%d\r\n", client_idx);
            printf("------------------------------------------------- >\r\n");
          }
          script_config_process(sub_payload);

        } else if (detection_notify("+CEREG:", mdm_xbuf, xbuf_trim)) {
          printf("Notify msg: %s\r\n", xbuf_trim);
        } else if (detection_notify("+CREG:", mdm_xbuf, xbuf_trim)) {
          printf("Notify msg: %s\r\n", xbuf_trim);
        } else if (detection_notify("+CMQTTCONNLOST:", mdm_xbuf, xbuf_trim)) {
          printf("Notify msg: %s\r\n", xbuf_trim);
          bmqtt_cnt = false;
          bmqtt_sub = false;
        } else if (detection_notify("+CMQTTNONET", mdm_xbuf, xbuf_trim)) {
          printf("Notify msg: %s\r\n", xbuf_trim);
          bmqtt_start = false;
          bmqtt_cnt = false;
          bmqtt_sub = false;
        } else {
        }

        set_mdm_busy(false);
      }
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
      _parser->set_timeout(15000);

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
  //   kick_wdt();
  rtc_init();
  period_min = read_dipsw();
  if (period_min < 2) {
    period_min = 2;
  }

  if (period_min < 8) {
    rtc_write(time_t(1686000000));
  }

  printf("\r\n\n----------------------------------------\r\n");
  printf("| Hello,I am UPSMON_LPC1768_SIM7600E-H |\r\n");
  printf("----------------------------------------\r\n");
  printf("Firmware Version: %s" CRLF, firmware_vers);
  printf("SystemCoreClock : %.3f MHz.\r\n", SystemCoreClock / 1000000.0);
  printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("capture period : %d minutes\r\n", period_min);

  read_initial_script();
  initial_FlashIAPBlockDevice(&iap);

  iap_to_script(&iap, &iap_init_script);
  if (iap_init_script.topic_path[0] == 0xff) {
    printf("iap is blank!!!\r\n");
    script_to_iap(&iap, &init_script);
  } else {
    printf("topic: %s\r\n", iap_init_script.topic_path);
    // printHEX((unsigned char *)&iap_init_script, sizeof(init_script_t));
  }

  isr_thread.start(callback(&isr_queue, &EventQueue::dispatch_forever));
  tpl5010.init(&isr_queue);

  blink_thread.start(mbed::callback(blink_routine, &myled));

  if (!is_script_read) {
    blink_led(NOFILE);
  }

  //   isr_thread.start(callback(&isr_queue, &EventQueue::dispatch_forever));
  //   tpl5010.init(&isr_queue);

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  //_parser->debug_on(1);
  modem = new Sim7600Cellular(_parser);
  // modem=new Sim7600Cellular(PTD3,PTD2);

  xtc232 = new ATCmdParser(&rs232, "\r", 256, 1000);
  ThisThread::sleep_for(500ms);
  vrf_en = 1;
  rtc_uptime = (unsigned int)rtc_read();

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
    modem->set_creg(2);
    modem->set_cereg(2);
  }

  debug_if(modem->get_IMEI(imei), "imei=  %s\r\n", imei);
  debug_if(modem->get_ICCID(iccid), "iccid=  %s\r\n", iccid);

  while (!mdmAtt) {
    mdmAtt = modem->check_attachNW();
    ThisThread::sleep_for(2000ms);
  }

  debug_if(mdmAtt, "NW Attached!!!\r\n");
  debug_if(modem->get_csq(&sig, &ber), "sig=%d ber=%d\r\n", sig, ber);

  while (modem->get_creg() != 1)
    ;

  if (modem->get_creg() > 0) {
    printf("NW Registered!!!\r\n");
    if (is_script_read) {
      netstat_led(CONNECTED);
    }
  }

  ntp_sync();

  char cclk_msg[32];
  if (_parser->send("AT+CCLK?") &&
      _parser->scanf("+CCLK: \"%[^\"]\"\r\n", cclk_msg)) {
    printf("msg= %s\r\n", cclk_msg);
    sync_rtc(cclk_msg);
    printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  }

  last_rtc_check_NW = (unsigned int)rtc_read();
  last_ntp_sync = (unsigned int)rtc_read();

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
    set_mdm_busy(false);
  }

  if (bmqtt_cnt) {
    memset(str_sub_topic, 0, 128);
    sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path, imei);
    bmqtt_sub = modem->mqtt_sub(str_sub_topic);

    // if (bmqtt_sub) {
    //   set_notify_ready(true);
    // }
  }

  capture_thread.start(callback(capture_thread_routine));
  usb_thread.start(callback(usb_passthrough));
  mdm_notify_thread.start(callback(mdm_notify_routine));

  //   pwake.fall(&fall_wake);

  while (true) {
    // if (isWake) {
    //   kick_wdt();
    //   printf("wakeup!!!\r\n");
    //   isWake = false;
    // }
    if (tpl5010.get_wdt()) {
      tpl5010.set_wdt(false);
    }

    mail_t *mail = mail_box.try_get_for(Kernel::Clock::duration(500));
    if (mail != nullptr) {
      printf("utc : %d" CRLF, mail->utc);
      printf("cmd : %s" CRLF, mail->cmd);
      printf("resp : %s" CRLF, mail->resp);

      memset(str_topic, 0, 128);
      memset(mqtt_msg, 0, 256);
      sprintf(str_topic, "%s/data/%s", init_script.topic_path, imei);
      sprintf(mqtt_msg, payload_pattern, imei, mail->utc, init_script.model,
              init_script.siteID, mail->cmd, mail->resp);

      if (bmqtt_cnt && (!get_mdm_busy())) {
        set_notify_ready(false);
        set_mdm_busy(true);

        if (modem->mqtt_publish(str_topic, mqtt_msg)) {
          printf("< ------------------------------------------------ >" CRLF);
        }
        set_mdm_busy(false);

        // if (bmqtt_sub) {
        //   set_notify_ready(true);
        // }
      }

      mail_box.free(mail);
    }
    //  else NullPtr
    else {
      //   if (((unsigned int)rtc_read() - last_rtc_check_NW) > 55) {
      if ((((unsigned int)rtc_read() - last_rtc_check_NW) > 55) &&
          (!get_mdm_busy())) {
        last_rtc_check_NW = (unsigned int)rtc_read();
        printf(CRLF "<----- Checking NW. Status ----->" CRLF);
        printf("timestamp : %d\r\n", (unsigned int)rtc_read());

        set_notify_ready(false);
        set_mdm_busy(true);

        // check AT>OK
        if (!modem->check_modem_status(10)) {
          netstat_led(OFF);
          vrf_en = 0;
          ThisThread::sleep_for(500ms);
          vrf_en = 1;
          bmqtt_start = false;
          bmqtt_cnt = false;
          bmqtt_sub = false;

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
              modem->set_cereg(2);
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

          debug_if(modem->get_csq(&sig, &ber), "sig=%d ber=%d\r\n", sig, ber);
          memset(msg_cpsi, 0, 128);

          debug_if(modem->get_cpsi(msg_cpsi), "cpsi=> %s\r\n", msg_cpsi);

          mdmAtt = modem->check_attachNW();
          if (mdmAtt && (modem->get_creg() == 1)) {
            netstat_led(CONNECTED);

            debug_if(modem->get_IPAddr(ipaddr), "ipaddr= %s\r\n", ipaddr);

            if (!bmqtt_start) {
              mdmAtt = modem->check_attachNW();
              if (mdmAtt && (modem->get_creg() == 1)) {

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

                  if (bmqtt_cnt) {
                    memset(str_sub_topic, 0, 128);
                    sprintf(str_sub_topic, "%s/config/%s",
                            init_script.topic_path, imei);
                    bmqtt_sub = modem->mqtt_sub(str_sub_topic);
                  }
                }
              }
            }

            if (modem->mqtt_connect_stat() < 1) {
              if (bmqtt_start) {
                bmqtt_cnt = modem->mqtt_connect(
                    dns_ip, init_script.usr, init_script.pwd, init_script.port);
                debug_if(bmqtt_cnt, "MQTT Connected\r\n");

                if (bmqtt_cnt) {
                  memset(str_sub_topic, 0, 128);
                  sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
                          imei);
                  bmqtt_sub = modem->mqtt_sub(str_sub_topic);
                }
              }
            }

            if (modem->mqtt_isdisconnect() < 1) {
              bmqtt_cnt = false;
              bmqtt_sub = false;

              if (modem->mqtt_release()) {

                if (modem->mqtt_stop()) {
                  bmqtt_start = false;
                } else {
                  bmqtt_start = false;
                  netstat_led(OFF);
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
                    modem->set_full_FUNCTION();
                    modem->set_creg(2);
                    modem->set_cereg(2);
                    netstat_led(IDLE);
                  }
                }
              }
            } else {
              bmqtt_cnt = true;
              //   char ret_cnnt_stat[128];
              //   modem->mqtt_connect_stat(ret_cnnt_stat);
              //   printf("ret_cnnt_stat-> %s\r\n", ret_cnnt_stat);

              //   if (bmqtt_sub) {
              //     bmqtt_sub = false;
              //     memset(str_sub_topic, 0, 128);
              //     sprintf(str_sub_topic, "%s/config/%s",
              //     init_script.topic_path,
              //             imei);
              //     debug_if(modem->mqtt_unsub(str_sub_topic),
              //              "MQTT Unsub Complete!!!\r\n");
              //   }
            }

            // if (modem->ping_dstNW("8.8.8.8") < 1000) {
            //   bmqtt_cnt = (bmqtt_cnt && true);
            // }

            if (((unsigned int)rtc_read() - last_ntp_sync) > 3600) {
              ntp_sync();
              last_ntp_sync = (unsigned int)rtc_read();
              device_stat_update();
            }

          } else {
            netstat_led(OFF);
            bmqtt_start = false;
            bmqtt_cnt = false;
            bmqtt_sub = false;
            mdm_rst = 1;
            ThisThread::sleep_for(500ms);
            mdm_rst = 0;
            printf("NW Attaching Fail: Rebooting Modem!!!" CRLF);

            rtc_uptime = (unsigned int)rtc_read();
            while (mdm_status.read() &&
                   ((unsigned int)rtc_read() - rtc_uptime < 18))
              ;

            if (modem->check_modem_status(10)) {
              printf("Restart Modem Complete : AT Ready!!!" CRLF);
              modem->set_cops();
              modem->set_full_FUNCTION();
              modem->set_creg(2);
              modem->set_cereg(2);
              netstat_led(IDLE);
            }
          }
        }
        // end check AT>OK

        set_mdm_busy(false);

        if (bmqtt_sub) {
          set_notify_ready(true);
        }

      } // end last_rtc_check_NW > 55
    }   // end else NullPtr
  }     // end while(true)
} // end main()

int read_parser_to_char(char *tbuf, int size, char end) {
  int count = 0;
  int x = 0;

  if (size > 0) {
    for (count = 0; (count < size) && (x >= 0) && (x != end); count++) {
      x = _parser->getc();
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

  char key_encoded[64];

  //   if (sscanf(script_buff, init_cfg_pattern, init_script.broker,
  //              &init_script.port, init_script.usr, init_script.pwd,
  //              init_script.topic_path, init_script.full_cmd,
  //              init_script.model, init_script.siteID) == 8) {
  if (sscanf(script_buff, init_cfg_pattern, init_script.broker,
             &init_script.port, key_encoded, init_script.topic_path,
             init_script.full_cmd, init_script.model,
             init_script.siteID) == 7) {
    if (init_script.topic_path[strlen(init_script.topic_path) - 1] == '/') {
      init_script.topic_path[strlen(init_script.topic_path) - 1] = '\0';
    }

    printf("\r\n<---------------------------------------->\r\n");
    printf("    broker: %s" CRLF, init_script.broker);
    printf("    port: %d" CRLF, init_script.port);
    printf("    Key: %s" CRLF, key_encoded);
    // printf("    usr: %s" CRLF, init_script.usr);
    // printf("    pwd: %s" CRLF, init_script.pwd);
    printf("    topic_path: %s" CRLF, init_script.topic_path);
    printf("    full_cmd: %s" CRLF, init_script.full_cmd);
    printf("    model: %s" CRLF, init_script.model);
    printf("    siteID: %s" CRLF, init_script.siteID);
    printf("<---------------------------------------->\r\n\n");

    // size_t len_key_encode = (size_t)strlen(key_encoded);
    // size_t len_key_decode;
    // char *key_decode1 =
    //     base64_obj.Decode(key_encoded, len_key_encode, &len_key_decode);

    // size_t len_key_decode2;
    // char *key_decode2 =
    //     base64_obj.Decode(key_decode1, len_key_decode, &len_key_decode2);

    char key_encoded2[64];
    char *key_decode2;
    strcpy(key_encoded2, key_encoded);

    size_t len_key_encode = (size_t)strlen(key_encoded2);
    size_t len_key_decode;

    for (int ix = 0; ix < 2; ix++) {
      key_decode2 =
          base64_obj.Decode(key_encoded2, len_key_encode, &len_key_decode);

      len_key_encode = len_key_decode;
      strcpy(key_encoded2, key_decode2);
    }

    // printf("key_decode2= %s\r\n\n", key_decode2);

    if (sscanf(key_decode2, "%[^ ] %[^\n]", init_script.usr, init_script.pwd) ==
        2) {
      //   printf("usr=%s pwd=%s\r\n", init_script.usr, init_script.pwd);
      is_script_read = true;
      strcpy(encode_key, key_encoded);
    }
  }

  /* the whole file is now loaded in the memory buffer. */

  // terminate
  fclose(file);
  free(buffer);
}

void device_stat_update(const char *stat_mode) {
  if (bmqtt_cnt) {
    char stat_payload[512];
    char stat_topic[128];
    char cereg_msg[64];
    char cops_msg[64];
    char str_stat[15];
    strcpy(str_stat, stat_mode);
    sprintf(stat_topic, "%s/status/%s", init_script.topic_path, imei);
    modem->set_cereg(2);
    modem->get_csq(&sig, &ber);
    modem->get_cops(cops_msg);

    memset(msg_cpsi, 0, 128);
    modem->get_cpsi(msg_cpsi);

    if (modem->get_cereg(cereg_msg) > 0) {
      sprintf(stat_payload, stat_pattern, imei, (unsigned int)rtc_read(),
              firmware_vers, Dev_Group, period_min, str_stat, sig, ber,
              cops_msg, cereg_msg, msg_cpsi);
      modem->mqtt_publish(stat_topic, stat_payload);
    }
  }
}

void write_init_script() {
  bd.init();
  int err = fs.mount(&bd);

  if (err) {
    printf("%s filesystem mount failed\r\n", fs.getName());
  }

  FILE *file = fopen(FULL_SCRIPT_FILE_PATH, "w");

  if (file != NULL) {
    printf("initial script found\r\n");
    char fbuffer[512];
    sprintf(fbuffer, init_cfg_write, init_script.broker, init_script.port,
            encode_key, init_script.topic_path, init_script.full_cmd,
            init_script.model, init_script.siteID);

    // sprintf(fbuffer, "Hello : %d\r\n",15);

    printf("------------- fbuffer ------------- >\r\n%s\r\n< "
           "--------------------------\r\n",
           fbuffer);
    fprintf(file, fbuffer);
  }

  fclose(file);

  fs.unmount();
  bd.deinit();
}
