# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the production cached battery command with hardware present/absent."""
import unittest
import test_esb as helpers

class BatteryTests(unittest.TestCase):
    def run_case(self, supported):
        headers = {
            'app_util_platform.h': '#define CRITICAL_REGION_ENTER() {\n#define CRITICAL_REGION_EXIT() }\n',
            'nrf.h': """#pragma once
#include <stdint.h>
static struct {uint32_t USBREGSTATUS;} power;
static struct {uint32_t ENABLE;} usbd;
#define NRF_POWER (&power)
#define NRF_USBD (&usbd)
#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1
""",
            'adc.h': '#include <stdint.h>\nuint16_t get_vcc(void);\nuint8_t get_battery_level(void);\n',
        }
        helpers.EsbTests().run_c(r'''
#include <assert.h>
#include "features/battery.c"
static uint16_t voltage;static uint8_t percent;static unsigned reads;
uint16_t get_vcc(void){reads++;return voltage;}
uint8_t get_battery_level(void){reads++;return percent;}
static uint8_t p[32];
static void query(void){memset(p,0,32);p[0]=0x86;p[1]=1;p[28]=0xAB;p[31]=0xCD;nrf5_battery_command(p);assert(p[0]==0x86&&p[1]==1&&p[28]==0xAB&&p[31]==0xCD);for(unsigned i=6;i<28;i++)assert(!p[i]);}
int main(void){
 query();
#ifdef NRF5_BATTERY_ENABLE
 assert(p[2]==1&&!p[3]&&!p[4]&&!p[5]);
 voltage=3200;percent=0;query();assert(p[2]==3&&!p[3]&&p[4]==12&&p[5]==128);
 voltage=4100;percent=99;power.USBREGSTATUS=1;query();assert(p[2]==7&&p[3]==99&&p[4]==16&&p[5]==4);
 power.USBREGSTATUS=0;usbd.ENABLE=1;query();assert(p[2]==7);
 usbd.ENABLE=0;percent=101;query();assert(p[2]==1&&!p[3]&&!p[4]&&!p[5]);
 percent=10;voltage=65535;query();assert(p[2]==1&&!p[3]&&!p[4]&&!p[5]);
#else
 assert(!p[2]&&!reads);power.USBREGSTATUS=1;query();assert(p[2]==4&&!reads);
#endif
 memset(p,0,32);p[0]=0x86;p[1]=2;nrf5_battery_command(p);assert(p[0]==0xFF);
 memset(p,0,32);p[0]=0x86;p[1]=1;p[27]=1;unsigned before=reads;nrf5_battery_command(p);assert(p[0]==0xFF&&reads==before);
}
''', headers, flags=['-DNRF5_BATTERY_ENABLE'] if supported else [])
    def test_cached_battery_zero_unknown_usb_and_invalid_samples(self):
        self.run_case(True)
    def test_board_without_battery_never_reads_adc(self):
        self.run_case(False)
