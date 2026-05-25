/* Copyright 2021 QMK
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
#pragma once

//#define GPIO(port, pin) port << 6 | pin

#define PORT_SHIFTER 6

#if defined(PROTOCOL_NRF5)
#    define PINA_ADDRESS 0x0
#    define PINB_ADDRESS 0x1

#define GPIO(port, pin) ((PIN##port##_ADDRESS << PORT_SHIFTER) | pin)

#define A1 GPIO(A,0)
#define A2 GPIO(A,1)
#define A3 GPIO(A,2)
#define A4 GPIO(A,3)
#define A5 GPIO(A,4)
#define A6 GPIO(A,5)
#define A7 GPIO(A,6)
#define A8 GPIO(A,7)
#define A9 GPIO(A,8)
#define B1 GPIO(A,9)
#define B2 GPIO(A,10)
#define B3 GPIO(A,11)
#define B4 GPIO(A,12)
#define B5 GPIO(A,13)
#define B6 GPIO(A,14)
#define B7 GPIO(A,15)
#define B8 GPIO(A,16)
#define B9 GPIO(A,17)
#define C1 GPIO(A,18)
#define C2 GPIO(A,19)
#define C3 GPIO(A,20)
#define C4 GPIO(A,21)
#define C5 GPIO(A,22)
#define C6 GPIO(A,23)
#define C7 GPIO(A,24)
#define C8 GPIO(A,25)
#define C9 GPIO(A,26)
#define D1 GPIO(A,27)
#define D2 GPIO(A,28)
#define D3 GPIO(A,29)
#define D4 GPIO(A,30)
#define D5 GPIO(A,31)
#define D6 GPIO(B,0)
#define D7 GPIO(B,1)
#define D8 GPIO(B,2)
#define D9 GPIO(B,3)
#define E1 GPIO(B,4)
#define E2 GPIO(B,5)
#define E3 GPIO(B,6)
#define E4 GPIO(B,7)
#define E5 GPIO(B,8)
#define E6 GPIO(B,9)
#define E7 GPIO(B,10)
#define E8 GPIO(B,11)
#define E9 GPIO(B,12)
#define F1 GPIO(B,13)
#define F2 GPIO(B,14)
#define F3 GPIO(B,15)

#endif