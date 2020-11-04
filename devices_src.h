// char mqtt_broker[]="trueiot.io";
char mqtt_broker_ip[] = "188.166.189.39";
char mqtt_broker[] = "trueiot.io";
int mqtt_port = 1883;
char mqtt_usr[] = "IoTdevices";
char mqtt_pwd[] = "devices@iot";
char payload_pattern[] =
    "{\"imei\":%s,\"utc\":%u,\"cmd\":\"%s\",\"resp\":\"%s\"}";