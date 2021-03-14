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

    typedef uint8_t pin_t;

    #define setPinInputHigh(pin)    nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP)
    #define setPinInputLow(pin)     nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLDOWN)
    #define setPinOutput(pin)       nrf_gpio_cfg_output(pin)

    #define writePinHigh(pin)       nrf_gpio_pin_set(pin)
    #define writePinLow(pin)        nrf_gpio_pin_clear(pin)
    #define writePin(pin, level)    ((level) ? writePinHigh(pin) : writePinLow(pin))

    #define readPin(pin)            nrf_gpio_pin_read(pin)