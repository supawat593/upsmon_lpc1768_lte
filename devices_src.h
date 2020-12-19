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

char init_cfg_pattern[] = {
    "%*[^\n]\nBroker: \"%[^\"]\"\nPort: %d\nTopic: \"%[^\"]\"\nCommand: "
    "[%[^]]]\nModel: \"%[^\"]\"\nSite_ID: "
    "\"%[^\"]\"\n%*s"};

char payload_pattern[] = "{\"imei\":%s,\"utc\":%u,\"model\":\"%s\",\"site_ID\":"
                         "\"%s\",\"cmd\":\"%s\",\"resp\":\"%s\"}";

char stat_pattern[] =
    "{\"imei\":%s,\"utc\":%u,\"firm_vers\":\"%s\",\"dev_group\":"
    "\"%s\",\"period_min\":%u,\"stat_mode\":"
    "\"%s\",\"csq_stat\":\"+CSQ: "
    "%d,%d\",\"cops_opn\":\"%s\",\"reg_stat\":\"%s\",\"ext_stat\":\"%s\"}";

char dummy_msg[] =
    "(224.4 000.0 000.0 204.0 220.5 000 000 50.9 385 380 108.4 24.0 IM";

typedef struct {
  unsigned int utc;
  char cmd[16];
  char resp[128];
} mail_t;

typedef struct {
  char broker[32];
  int port;
  char usr[32];
  char pwd[32];
  char topic_path[100];
  char full_cmd[64];
  char model[32];
  char siteID[32];
} init_script_t;
