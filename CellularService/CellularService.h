#ifndef _CELLULARSERVICE_H
#define _CELLULARSERVICE_H

#include "../Sim7600Cellular/Sim7600Cellular.h"
#include "../typedef_src.h"

class CellularService : public Sim7600Cellular {
public:
  cellular_data_t cell_info;

  CellularService(PinName tx, PinName rx, DigitalOut &en, DigitalOut &rst);
  CellularService(BufferedSerial *_serial, DigitalOut &en, DigitalOut &rst);
  CellularService(ATCmdParser *_parser, DigitalOut &en, DigitalOut &rst);
  void vrf_enable(bool en = false);
  bool MDM_HW_reset(void);
  bool initial_NW();
  void sync_rtc(char cclk[64]);

private:
  Timer tm1;
  DigitalOut &vrf_en;
  DigitalOut &mdm_rst;

  struct tm struct_tm;

  int read_sys_time();
};

#endif