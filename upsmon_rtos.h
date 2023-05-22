#include "FlashIAPBlockDevice.h"
#include "devices_src.h"
#include "mbed.h"

#define iap_script_offset 0
#define iap_startup_offset 0x8000 // 32768

typedef enum { OFF = 0, IDLE, CONNECTED } netstat_mode;
typedef enum { PWRON = 0, NORMAL, NOFILE } blink_mode;

typedef struct {
  int led_state;
  int period_ms;
  float duty;
} blink_t;

FlashIAPBlockDevice iap;

Mutex mutex_idle_rs232, mutex_usb_cnnt, mutex_mdm_busy, mutex_notify;

MemoryPool<int, 1> netstat_mpool;
Queue<int, 1> netstat_queue;

MemoryPool<blink_t, 1> blink_mpool;
Queue<blink_t, 1> blink_queue;

Mail<mail_t, 8> mail_box, ret_usb_mail;

volatile bool is_usb_cnnt = false;
volatile bool is_idle_rs232 = true;
volatile bool is_mdm_busy = true;
volatile bool is_notify_ready = false;

bool get_notify_ready() {
  bool temp;
  mutex_notify.lock();
  temp = is_notify_ready;
  mutex_notify.unlock();
  return temp;
}

void set_notify_ready(bool temp) {
  mutex_notify.lock();
  is_notify_ready = temp;
  mutex_notify.unlock();
}

bool get_mdm_busy() {
  bool temp;
  mutex_mdm_busy.lock();
  temp = is_mdm_busy;
  mutex_mdm_busy.unlock();
  return temp;
}

void set_mdm_busy(bool temp) {
  mutex_mdm_busy.lock();
  is_mdm_busy = temp;
  mutex_mdm_busy.unlock();
}

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

void initial_FlashIAPBlockDevice(FlashIAPBlockDevice *_bd) {
  printf("FlashIAPBlockDevice initialising\r\n");
  //   _bd->init();
  debug_if(_bd->init() != 0, "initial FlashIAP Fail!!!\r\n");
  printf("FlashIAP block device size: %llu\r\n", _bd->size());
  printf("FlashIAP block device program_size: %llu\r\n",
         _bd->get_program_size());
  printf("FlashIAP block device read_size: %llu\r\n", _bd->get_read_size());
  printf("FlashIAP block device erase size: %llu\r\n", _bd->get_erase_size());
}

void script_to_iap(FlashIAPBlockDevice *_bd, init_script_t *_init) {
  init_script_t *buffer = (init_script_t *)malloc(sizeof(init_script_t));
  memcpy(buffer, _init, sizeof(init_script_t));

  _bd->erase(iap_script_offset, _bd->get_erase_size());
  _bd->program(buffer, iap_script_offset, _bd->get_erase_size());
  free(buffer);
}

void iap_to_script(FlashIAPBlockDevice *_bd, init_script_t *_init) {
  init_script_t *buffer = (init_script_t *)malloc(sizeof(init_script_t));
  _bd->read(buffer, iap_script_offset, sizeof(init_script_t));
  memcpy(_init, buffer, sizeof(init_script_t));
  free(buffer);
}