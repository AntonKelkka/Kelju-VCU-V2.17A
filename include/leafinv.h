/*
 * This file is part of the tumanako_vc project.
 *
 * Copyright (C) 2020 Johannes Huebner <dev@johanneshuebner.com>
 *                      Damien Maguire <info@evbmw.com>
 * Yes I'm really writing software now........run.....run away.......
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LEAFINV_H
#define LEAFINV_H
#include <stdint.h>
#include "my_fp.h"

enum class PDMChargingState
{
    No_Chg,
    AC_Chg,
    DC_Chg
};

class LeafINV
{
public:
    static void DecodeCAN(int id, uint32_t data[2]);
    static void DecodePDM679(uint32_t data[2]);
    static void DecodePDM390(uint32_t data[2]);
    static void Send10msMessages();
    static void Send100msMessages();
    static PDMChargingState ControlCharge(bool RunCh);
    static int16_t speed;
    static void SetTorque(int8_t gear, int16_t torque);
    static int16_t inv_temp;
    static int16_t motor_temp;
    static bool error;
    static uint16_t voltage;


private:
    static void nissan_crc(uint8_t *data, uint8_t polynomial);
    static int8_t fahrenheit_to_celsius(uint16_t fahrenheit);
    static uint8_t run10ms;
    static uint8_t run100ms;
    static uint32_t lastRecv;



    static int16_t final_torque_request;
    //
};

#endif // LEAFINV_H
