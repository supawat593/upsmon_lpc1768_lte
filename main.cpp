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
// #include "USBSerial.h"

#include "./ExtStorage/ExtStorage.h"
#include "FATFileSystem.h"
#include "SPIFBlockDevice.h"

DigitalOut myled(LED1, 1);
DigitalOut netstat(LED2, 0);

FATFileSystem fs(SPIF_MOUNT_PATH);
SPIFBlockDevice bd(MBED_CONF_SPIF_DRIVER_SPI_MOSI,
                   MBED_CONF_SPIF_DRIVER_SPI_MISO,
                   MBED_CONF_SPIF_DRIVER_SPI_CLK, MBED_CONF_SPIF_DRIVER_SPI_CS);
ExtStorage ext(&bd, &fs);

// static UnbufferedSerial pc(USBTX, USBRX);
static BufferedSerial mdm(MDM_TXD_PIN, MDM_RXD_PIN, 115200);
ATCmdParser *_parser;
// Sim7600Cellular *modem;
CellularService *modem;
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
volatile bool is_usb_plug = false;
bool mdmOK = false;
bool mdmAtt = false;
bool bmqtt_start = false;
bool bmqtt_cnt = false;
bool bmqtt_sub = false;

int last_ntp_sync = 0;
unsigned int last_rtc_check_NW = 0;
unsigned int rtc_uptime = 0;

char str_topic[128];
char mqtt_msg[256];
char str_sub_topic[128];

Thread blink_thread(osPriorityNormal, 0x100, nullptr, "blink_thread"),
    netstat_thread(osPriorityNormal, 0x100, nullptr, "netstat_thread"),
    capture_thread(osPriorityNormal, 0x500, nullptr, "data_capture_thread"),
    mdm_notify_thread(osPriorityNormal, 0x0c00, nullptr, "mdm_notify_thread");

Thread *usb_thread;
Thread isr_thread(osPriorityAboveNormal, 0x400, nullptr, "isr_queue_thread");

EventQueue isr_queue;

init_script_t init_script, iap_init_script;

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

void usb_passthrough() {

  printf("---> start usb_passthrough Thread ...\r\n");

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
          //   read_xtc_to_char(str_usb_ret, 128, '\r');
          read_xparser_to_char(str_usb_ret, 128, '\r', xtc232);
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
  Timer duration_tme;

  while ((ptr != 0) && (n_cmd < 6)) {
    // strcpy(str_cmd_buff[k], ptr);
    sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  printf("---> start Capture Thread <---\r\n");

  while (true) {
    // printf("*********************************" CRLF);
    // printf("capture---> timestamp: %d" CRLF, (unsigned int)rtc_read());
    // printf("*********************************" CRLF);

    // is_idle_rs232 = false;
    set_idle_rs232(false);
    duration_tme.reset();
    duration_tme.start();

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

      //   read_xtc_to_char(ret_rs232, 128, '\r');
      read_xparser_to_char(ret_rs232, 128, '\r', xtc232);
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
    // if (period_min < 2) {
    //   period_min = 2;
    // }
    static int cmd_period =
        duration_cast<chrono::seconds>(duration_tme.elapsed_time()).count();
    duration_tme.stop();

    // ThisThread::sleep_for(chrono::minutes(period_min));
    ThisThread::sleep_for(chrono::seconds(60 * period_min - cmd_period));
  }
}

void mdm_notify_routine() {
  printf("mdm_notify_routine() started --->\r\n");
  char mdm_xbuf[512];
  char xbuf_trim[512];

  int len_topic = 0;
  int len_payload = 0;
  int st = 0, end = 0;

  memcpy(&script_bkp, &init_script, sizeof(init_script_t));
  volatile bool is_rx_mqtt = false;
  rx_notify_t rx_mqttsub_msg = {"\0", "\0", 0, 0, 0};

  while (true) {
    // if (get_notify_ready() && (!get_mdm_busy()) && mdm.readable()) {
    if (get_notify_ready() && (!get_mdm_busy())) {

      if (mdm.readable()) {

        set_mdm_busy(true);
        memset(mdm_xbuf, 0, 512);
        memset(xbuf_trim, 0, 512);

        // modem->read_atc_to_char(mdm_xbuf, 512, '\r');
        read_xparser_to_char(mdm_xbuf, 512, '\r', _parser);
        if (detection_notify("+CMQTTRXSTART:", mdm_xbuf, xbuf_trim)) {

          printf("Notify msg: %s\r\n", xbuf_trim);

          int len_buf = 512;
          if (sscanf(xbuf_trim, "+CMQTTRXSTART: %*d,%d,%d", &len_topic,
                     &len_payload) == 2) {

            len_buf = (len_topic + len_payload) << 1;
          }
          //   debug_if(len_buf < 512, "len_buf=%d\r\n", len_buf);

          memset(mdm_xbuf, 0, len_buf);
          memset(xbuf_trim, 0, len_buf);

          _parser->set_timeout(500);
          _parser->read(mdm_xbuf, len_buf);
          _parser->set_timeout(8000);
          st = 0;
          while ((strncmp(&mdm_xbuf[st], "+CMQTTRXTOPIC", 13) != 0) &&
                 (st < (len_buf - 13))) {
            st++;
          }

          if (st < (len_buf - 13)) {

            memcpy(&xbuf_trim[0], &mdm_xbuf[st], sizeof(mdm_xbuf) - st);
            // printf("Notify msg: %s\r\n", xbuf_trim);
            rx_mqttsub_msg = {"\0", "\0", 0, 0, 0};
            if (sscanf(xbuf_trim, mqtt_sub_topic_pattern,
                       &rx_mqttsub_msg.len_topic, rx_mqttsub_msg.sub_topic,
                       &rx_mqttsub_msg.len_payload, rx_mqttsub_msg.sub_payload,
                       &rx_mqttsub_msg.client_idx) == 5) {

              printout_mqttsub_notify(&rx_mqttsub_msg);
              is_rx_mqtt = true;
            }
          }

        }

        else if (detection_notify("+CEREG:", mdm_xbuf, xbuf_trim)) {
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

        if (is_rx_mqtt) {
          //   debug("is_rx_mqtt: true\r\n");

          if (script_config_process(rx_mqttsub_msg.sub_payload) > 0) {

            debug("before write script file\r\n");
            ext.write_init_script(&script_bkp, FULL_SCRIPT_FILE_PATH);
            ext.deinit();
            system_reset();
          }
          is_rx_mqtt = false;
        }

        set_mdm_busy(false);
      }
    }
  }
}

int main() {

  // Initialise the digital pin LED1 as an output

  rtc_init();
  period_min = (read_dipsw() < 1) ? 1 : read_dipsw();

  printf("\r\n\n----------------------------------------\r\n");
  printf("| Hello,I am UPSMON_LPC1768_SIM7600E-H |\r\n");
  printf("----------------------------------------\r\n");
  printf("Firmware Version: %s" CRLF, firmware_vers);
  printf("SystemCoreClock : %.3f MHz.\r\n", SystemCoreClock / 1000000.0);
  printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("capture period : %d minutes\r\n", period_min);

  ext.init();
  ext.read_init_script(&init_script, FULL_SCRIPT_FILE_PATH);
  //   initial_FlashIAPBlockDevice(&iap);

  //   iap_to_script(&iap, &iap_init_script);
  //   if (iap_init_script.topic_path[0] == 0xff) {
  //     printf("iap is blank!!!\r\n");
  //     script_to_iap(&iap, &init_script);
  //   } else {
  //     printf("topic: %s\r\n", iap_init_script.topic_path);
  //     // printHEX((unsigned char *)&iap_init_script, sizeof(init_script_t));
  //   }

  isr_thread.start(callback(&isr_queue, &EventQueue::dispatch_forever));
  tpl5010.init(&isr_queue);

  blink_thread.start(mbed::callback(blink_routine, &myled));
  netstat_thread.start(callback(blip_netstat, &netstat));

  if (!ext.get_script_flag()) {
    blink_led(NOFILE);
  }

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  modem = new CellularService(_parser, vrf_en, mdm_rst);

  xtc232 = new ATCmdParser(&rs232, "\r", 256, 1000);
  ThisThread::sleep_for(500ms);

  modem->vrf_enable(1);
  modem->ctrl_timer(1);
  int sys_time_ms = modem->read_sys_time();

  while (mdm_status.read() && (modem->read_sys_time() - sys_time_ms < 18000))
    ThisThread::sleep_for(250ms);

  debug_if(modem->read_sys_time() - sys_time_ms > 18000,
           "MDM_STAT Timeout : %d sec.\r\n",
           modem->read_sys_time() - sys_time_ms);

  sys_time_ms = modem->read_sys_time();
  modem->ctrl_timer(0);
  modem->check_modem_status(3) ? netstat_led(IDLE) : netstat_led(OFF);

  if (ext.get_script_flag() && modem->initial_NW()) {
    netstat_led(CONNECTED);
  }

  modem->ntp_setup();

  if (modem->get_cclk(modem->cell_info.cclk_msg)) {
    modem->sync_rtc(modem->cell_info.cclk_msg);
    printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  }

  last_rtc_check_NW = (unsigned int)rtc_read();
  last_ntp_sync = (int)rtc_read();

  bmqtt_start = modem->mqtt_start();
  debug_if(bmqtt_start, "MQTT Started\r\n");

  if (bmqtt_start) {

    modem->mqtt_accquire_client(modem->cell_info.imei);

    if (modem->dns_resolve(init_script.broker, modem->cell_info.dns_ip) < 0) {
      memset(modem->cell_info.dns_ip, 0, 16);
      strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
    }

    bmqtt_cnt = modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                                    init_script.pwd, init_script.port);
    debug_if(bmqtt_cnt, "MQTT Connected\r\n");
    device_stat_update(modem, init_script.topic_path, "RESTART");
  }

  if (ext.get_script_flag()) {
    blink_led(NORMAL); //  normal Mode
    set_mdm_busy(false);
  }

  //   mdm_notify_thread.start(callback(mdm_notify_routine));

  if (bmqtt_cnt) {
    memset(str_sub_topic, 0, 128);
    sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
            modem->cell_info.imei);
    bmqtt_sub = modem->mqtt_sub(str_sub_topic);

    // if (bmqtt_sub) {
    //   set_notify_ready(true);
    // }
  }

  mdm_notify_thread.start(callback(mdm_notify_routine));
  capture_thread.start(callback(capture_thread_routine));
  //   usb_thread.start(callback(usb_passthrough));

  //   pwake.fall(&fall_wake);

  while (true) {

    if (tpl5010.get_wdt()) {
      tpl5010.set_wdt(false);
    }

    if ((usb_det.read() > 0) && (!is_usb_plug)) {

      usb_thread = new Thread(osPriorityNormal, 0x800, nullptr, "usb_thread");
      usb_thread->start(callback(usb_passthrough));
      is_usb_plug = true;
    }

    if ((usb_det.read() < 1) && (is_usb_plug)) {

      debug_if(usb_thread->terminate() == osOK, "usb_thread Terminated\r\n");
      delete usb_thread;
      is_usb_plug = false;
    }

    mail_t *mail = mail_box.try_get_for(Kernel::Clock::duration(500));
    if (mail != nullptr) {
      printf("utc : %d" CRLF, mail->utc);
      printf("cmd : %s" CRLF, mail->cmd);
      printf("resp : %s" CRLF, mail->resp);

      memset(str_topic, 0, 128);
      memset(mqtt_msg, 0, 256);
      sprintf(str_topic, "%s/data/%s", init_script.topic_path,
              modem->cell_info.imei);
      sprintf(mqtt_msg, payload_pattern, modem->cell_info.imei, mail->utc,
              init_script.model, init_script.siteID, mail->cmd, mail->resp);

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
      if ((((unsigned int)rtc_read() - last_rtc_check_NW) > 65) &&
          (!get_mdm_busy())) {
        last_rtc_check_NW = (unsigned int)rtc_read();
        printf(CRLF "<----- Checking NW. Status ----->" CRLF);
        printf("timestamp : %d\r\n", (unsigned int)rtc_read());

        set_notify_ready(false);
        set_mdm_busy(true);

        // check AT>OK
        if (!modem->check_modem_status(10)) {
          netstat_led(OFF);
          //   vrf_en = 0;
          modem->vrf_enable(0);
          ThisThread::sleep_for(500ms);
          //   vrf_en = 1;
          modem->vrf_enable(1);
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

          memset(modem->cell_info.cclk_msg, 0, 32);
          if (modem->get_cclk(modem->cell_info.cclk_msg)) {
            modem->sync_rtc(modem->cell_info.cclk_msg);
            printf("timestamp : %d\r\n", (unsigned int)rtc_read());
          }

          debug_if(modem->get_csq(&modem->cell_info.sig, &modem->cell_info.ber),
                   "sig=%d ber=%d\r\n", modem->cell_info.sig,
                   modem->cell_info.ber);

          memset(modem->cell_info.cpsi_msg, 0, 128);
          debug_if(modem->get_cpsi(modem->cell_info.cpsi_msg), "cpsi=> %s\r\n",
                   modem->cell_info.cpsi_msg);

          mdmAtt = modem->check_attachNW();
          if (mdmAtt && (modem->get_creg() == 1)) {

            netstat_led(CONNECTED);
            debug_if(modem->get_IPAddr(modem->cell_info.ipaddr),
                     "ipaddr= %s\r\n", modem->cell_info.ipaddr);

            if (!bmqtt_start) {
              mdmAtt = modem->check_attachNW();
              if (mdmAtt && (modem->get_creg() == 1)) {

                netstat_led(CONNECTED);

                if (modem->dns_resolve(init_script.broker,
                                       modem->cell_info.dns_ip) < 0) {
                  memset(modem->cell_info.dns_ip, 0, 16);
                  strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
                }

                bmqtt_start = modem->mqtt_start();
                debug_if(bmqtt_start, "MQTT Started\r\n");

                if (bmqtt_start) {
                  modem->mqtt_accquire_client(modem->cell_info.imei);

                  bmqtt_cnt = modem->mqtt_connect(
                      modem->cell_info.dns_ip, init_script.usr, init_script.pwd,
                      init_script.port);
                  debug_if(bmqtt_cnt, "MQTT Connected\r\n");

                  if (bmqtt_cnt) {
                    memset(str_sub_topic, 0, 128);
                    sprintf(str_sub_topic, "%s/config/%s",
                            init_script.topic_path, modem->cell_info.imei);
                    bmqtt_sub = modem->mqtt_sub(str_sub_topic);
                  }
                }
              }
            }

            if (modem->mqtt_connect_stat() < 1) {
              if (bmqtt_start) {
                bmqtt_cnt = modem->mqtt_connect(
                    modem->cell_info.dns_ip, init_script.usr, init_script.pwd,
                    init_script.port);
                debug_if(bmqtt_cnt, "MQTT Connected\r\n");

                if (bmqtt_cnt) {
                  memset(str_sub_topic, 0, 128);
                  sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
                          modem->cell_info.imei);
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
                  modem->MDM_HW_reset();
                  printf("MQTT Stop Fail: Rebooting Modem!!!" CRLF);

                  rtc_uptime = (unsigned int)rtc_read();
                  while (mdm_status.read() &&
                         ((unsigned int)rtc_read() - rtc_uptime < 18))
                    ;

                  if (modem->check_modem_status(10)) {
                    printf("Restart Modem Complete : AT Ready!!!" CRLF);
                    modem->set_cops();
                    modem->set_full_FUNCTION();
                    // modem->set_creg(2);
                    // modem->set_cereg(2);
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
              modem->ntp_setup();
              memset(modem->cell_info.cclk_msg, 0, 32);
              if (modem->get_cclk(modem->cell_info.cclk_msg)) {
                modem->sync_rtc(modem->cell_info.cclk_msg);
                printf("timestamp : %d\r\n", (unsigned int)rtc_read());
              }

              last_ntp_sync = (int)rtc_read();
              device_stat_update(modem, init_script.topic_path);
            }

          } else {
            netstat_led(OFF);
            bmqtt_start = false;
            bmqtt_cnt = false;
            bmqtt_sub = false;
            // mdm_rst = 1;
            // ThisThread::sleep_for(500ms);
            // mdm_rst = 0;
            modem->MDM_HW_reset();

            printf("NW Attaching Fail: Rebooting Modem!!!" CRLF);

            rtc_uptime = (unsigned int)rtc_read();
            while (mdm_status.read() &&
                   ((unsigned int)rtc_read() - rtc_uptime < 18))
              ;

            if (modem->check_modem_status(10)) {
              printf("Restart Modem Complete : AT Ready!!!" CRLF);
              modem->set_cops();
              modem->set_full_FUNCTION();
              //   modem->set_creg(2);
              //   modem->set_cereg(2);
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
