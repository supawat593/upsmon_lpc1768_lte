#if TARGET_K22F
#define MDM_TXD_PIN PTD3
#define MDM_RXD_PIN PTD2
#define MDM_RI_PIN PTD1
#define MDM_DTR_PIN PTD4

#define MDM_VRF_EN_PIN PTC8
#define MDM_RST_PIN PTB1
#define MDM_PWR_PIN PTB2
#define MDM_FLIGHT_PIN PTC10
#define MDM_STATUS_PIN PTC9

#define ECARD_TX_PIN PTB17
#define ECARD_RX_PIN PTB16

#define WDT_WAKE_PIN PTC0
#define WDT_DONE_PIN PTB19
#define USB_DET_PIN PTA12

#define DIPSW_P1_PIN PTC4
#define DIPSW_P2_PIN PTC5
#define DIPSW_P3_PIN PTC6
#define DIPSW_P4_PIN PTC7

#endif

#if TARGET_LPC1768
#define MDM_TXD_PIN P0_15
#define MDM_RXD_PIN P0_16
#define MDM_RI_PIN P0_21
#define MDM_DTR_PIN P0_20

#define MDM_VRF_EN_PIN P1_29
#define MDM_RST_PIN P1_14
#define MDM_PWR_PIN P1_10
#define MDM_FLIGHT_PIN P1_25
#define MDM_STATUS_PIN P1_24

#define ECARD_TX_PIN P0_10
#define ECARD_RX_PIN P0_11

#define WDT_WAKE_PIN P2_1
#define WDT_DONE_PIN P2_0
#define USB_DET_PIN P1_31

#define DIPSW_P1_PIN P2_3
#define DIPSW_P2_PIN P2_4
#define DIPSW_P3_PIN P2_5
#define DIPSW_P4_PIN P2_6

#endif

#define firmware_vers "660105"
#define Dev_Group "LTE"

#define INITIAL_APP_FILE "initial_script.txt"
#define SPIF_MOUNT_PATH "spif"
#define FULL_SCRIPT_FILE_PATH "/" SPIF_MOUNT_PATH "/" INITIAL_APP_FILE

char mqtt_broker_ip[] = "188.166.189.39";
// char mqtt_broker[] = "trueiot.io";
// int mqtt_port = 1883;
char mqtt_usr[] = "IoTdevices";
char mqtt_pwd[] = "devices@iot";
// char model_Name[] = "Chuphotic1";
// char site_ID[] = "1234567890";

// const char *str_cmd[] = {"Q1", "Q4", "QF"}; // 1phase
const char *str_ret[] = {
    "(221.0 204.0 219.0 000 50.9 2.26 27.0 00000000",
    "(222.7 000.0 000.0 204.0 222.0 000 000 50.9 382 384 108.4 27.0 IM",
    "(07 204.0 49.8 208.6 50.3 152 010.5 433 414 100.2 03.4 01111111",
    "NO_RESP1",
    "NO_RESP2",
    "NO_RESP3"};

char init_cfg_pattern[] = {"%*[^\n]\nBroker: \"%[^\"]\"\nPort: %d\nKey: "
                           "\"%[^\"]\"\nTopic: \"%[^\"]\"\nCommand: "
                           "[%[^]]]\nModel: \"%[^\"]\"\nSite_ID: "
                           "\"%[^\"]\"\n%*s"};

char init_cfg_write[] = {
    "#Configuration file for UPS Monitor\r\n\r\nSTART:\r\nBroker: "
    "\"%s\"\r\nPort: %d\r\nKey: \"%s\"\r\nTopic: \"%s\"\r\nCommand: "
    "[%s]\r\nModel: \"%s\"\r\nSite_ID: \"%s\"\r\nSTOP:"};

char payload_pattern[] = "{\"imei\":%s,\"utc\":%u,\"model\":\"%s\",\"site_ID\":"
                         "\"%s\",\"cmd\":\"%s\",\"resp\":\"%s\"}";

char stat_pattern[] =
    "{\"imei\":%s,\"utc\":%u,\"firm_vers\":\"%s\",\"dev_group\":"
    "\"%s\",\"period_min\":%u,\"stat_mode\":"
    "\"%s\",\"csq_stat\":\"+CSQ: "
    "%d,%d\",\"cops_opn\":\"%s\",\"reg_stat\":\"%s\",\"ext_stat\":\"%s\"}";

char dummy_msg[] =
    "(224.4 000.0 000.0 204.0 220.5 000 000 50.9 385 380 108.4 24.0 IM";

char mqtt_sub_topic_pattern[] = "+CMQTTRXTOPIC: %*d,%d\r\n"
                                "%[^\r]\r\n"
                                "+CMQTTRXPAYLOAD: %*d,%d\r\n"
                                "%[^\r]\r\n"
                                "+CMQTTRXEND: %d\r\n";
