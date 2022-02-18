#include "ciank67.h"

#ifdef RGB_MATRIX_ENABLE
const is31_led g_is31_leds[DRIVER_LED_TOTAL] = {
    {0, CS3_SW1, CS2_SW1, CS1_SW1}, /* RGB1 */
    {0, CS6_SW1, CS5_SW1, CS4_SW1}, /* RGB2 */
    {0, CS9_SW1, CS8_SW1, CS7_SW1}, /* RGB3 */
    {0, CS12_SW1, CS11_SW1, CS10_SW1}, /* RGB4 */
    {0, CS15_SW1, CS14_SW1, CS13_SW1}, /* RGB5 */
    {0, CS18_SW1, CS17_SW1, CS16_SW1}, /* RGB6 */
    {0, CS21_SW1, CS20_SW1, CS19_SW1}, /* RGB7 */
    {0, CS24_SW1, CS23_SW1, CS22_SW1}, /* RGB8 */
    {0, CS27_SW1, CS26_SW1, CS25_SW1}, /* RGB9 */
    {0, CS30_SW1, CS29_SW1, CS28_SW1}, /* RGB10 */
    {0, CS33_SW1, CS32_SW1, CS31_SW1}, /* RGB11 */
    {0, CS36_SW1, CS35_SW1, CS34_SW1}, /* RGB12 */
    {0, CS39_SW6, CS38_SW6, CS37_SW6}, /* RGB13*/
    {0, CS36_SW6, CS35_SW6, CS34_SW6}, /* RGB14 */

    {0, CS3_SW2, CS2_SW2, CS1_SW2}, /* RGB15 */
    {0, CS6_SW2, CS5_SW2, CS4_SW2}, /* RGB16 */
    {0, CS9_SW2, CS8_SW2, CS7_SW2}, /* RGB17 */
    {0, CS12_SW2, CS11_SW2, CS10_SW2}, /* RGB18 */
    {0, CS15_SW2, CS14_SW2, CS13_SW2}, /* RGB19 */
    {0, CS18_SW2, CS17_SW2, CS16_SW2}, /* RGB20 */
    {0, CS21_SW2, CS20_SW2, CS19_SW2}, /* RGB21 */
    {0, CS24_SW2, CS23_SW2, CS22_SW2}, /* RGB22 */
    {0, CS27_SW2, CS26_SW2, CS25_SW2}, /* RGB23 */
    {0, CS30_SW2, CS29_SW2, CS28_SW2}, /* RGB24 */
    {0, CS33_SW2, CS32_SW2, CS31_SW2}, /* RGB25 */
    {0, CS36_SW2, CS35_SW2, CS34_SW2}, /* RGB26 */
    {0, CS39_SW7, CS38_SW7, CS37_SW7}, /* RGB27*/
    {0, CS36_SW7, CS35_SW7, CS34_SW7}, /* RGB28 */

    {0, CS3_SW3, CS2_SW3, CS1_SW3}, /* RGB29 */
    {0, CS6_SW3, CS5_SW3, CS4_SW3}, /* RGB30 */
    {0, CS9_SW3, CS8_SW3, CS7_SW3}, /* RGB31 */
    {0, CS12_SW3, CS11_SW3, CS10_SW3}, /* RGB32 */
    {0, CS15_SW3, CS14_SW3, CS13_SW3}, /* RGB33 */
    {0, CS18_SW3, CS17_SW3, CS16_SW3}, /* RGB34 */
    {0, CS21_SW3, CS20_SW3, CS19_SW3}, /* RGB35 */
    {0, CS24_SW3, CS23_SW3, CS22_SW3}, /* RGB36 */
    {0, CS27_SW3, CS26_SW3, CS25_SW3}, /* RGB37 */
    {0, CS30_SW3, CS29_SW3, CS28_SW3}, /* RGB38 */
    {0, CS33_SW3, CS32_SW3, CS31_SW3}, /* RGB39 */
    {0, CS36_SW3, CS35_SW3, CS34_SW3}, /* RGB40 */
    {0, CS39_SW3, CS38_SW3, CS37_SW3}, /* RGB41*/

    {0, CS3_SW4, CS2_SW4, CS1_SW4}, /* RGB42 */
    {0, CS6_SW4, CS5_SW4, CS4_SW4}, /* RGB43 */
    {0, CS9_SW4, CS8_SW4, CS7_SW4}, /* RGB44 */
    {0, CS12_SW4, CS11_SW4, CS10_SW4}, /* RGB45 */
    {0, CS15_SW4, CS14_SW4, CS13_SW4}, /* RGB46 */
    {0, CS18_SW4, CS17_SW4, CS16_SW4}, /* RGB47 */
    {0, CS21_SW4, CS20_SW4, CS19_SW4}, /* RGB48 */
    {0, CS24_SW4, CS23_SW4, CS22_SW4}, /* RGB49 */
    {0, CS27_SW4, CS26_SW4, CS25_SW4}, /* RGB50 */
    {0, CS30_SW4, CS29_SW4, CS28_SW4}, /* RGB51 */
    {0, CS33_SW4, CS32_SW4, CS31_SW4}, /* RGB52 */
    {0, CS36_SW4, CS35_SW4, CS34_SW4}, /* RGB53 */
    {0, CS39_SW8, CS38_SW8, CS37_SW8}, /* RGB54*/
    {0, CS36_SW8, CS35_SW8, CS34_SW8}, /* RGB55 */
    	
    {0, CS3_SW5, CS2_SW5, CS1_SW5}, /* RGB56 */
    {0, CS6_SW5, CS5_SW5, CS4_SW5}, /* RGB57 */
    {0, CS9_SW5, CS8_SW5, CS7_SW5}, /* RGB58 */
    {0, CS12_SW5, CS11_SW5, CS10_SW5}, /* RGB59 */
    {0, CS18_SW5, CS17_SW5, CS16_SW5}, /* RGB60 */
    {0, CS21_SW5, CS20_SW5, CS19_SW5}, /* RGB61 */
    {0, CS24_SW5, CS23_SW5, CS22_SW5}, /* RGB62 */
    {0, CS27_SW5, CS26_SW5, CS25_SW5}, /* RGB63 */
    {0, CS33_SW5, CS32_SW5, CS31_SW5}, /* RGB64 */
    {0, CS36_SW5, CS35_SW5, CS34_SW5}, /* RGB65 */
    {0, CS39_SW9, CS38_SW9, CS37_SW9}, /* RGB66*/
    {0, CS36_SW9, CS35_SW9, CS34_SW9}, /* RGB67 */
};

led_config_t g_led_config = {{
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13 },
    { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 },
    { 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
    { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54 },
    { 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66}
}, {
    {0, 0},{16, 0}, {32, 0}, {48, 0}, {64, 0}, {80, 0}, {96, 0}, {112, 0}, {128, 0}, {144, 0}, {160, 0}, {176, 0}, {192, 0}, {216, 0}, 
    {4, 16},{24, 16}, {40, 16}, {56, 16}, {72, 16}, {88, 16}, {104, 16}, {120, 16}, {136, 16}, {152, 16}, {168, 16}, {184, 16}, {200, 16}, {220, 16}, 
    {6, 32},{28, 32}, {44, 32}, {60, 32}, {76, 32}, {92, 32}, {108, 32}, {124, 32}, {140, 32}, {156, 32}, {172, 32}, {188, 32}, {214, 32}, 
    {8, 48},{32, 48}, {48, 48}, {64, 48}, {80, 48}, {96, 48}, {112, 48}, {128, 48}, {144, 48}, {160, 48}, {176, 48}, {192, 48}, {208, 48}, {224, 48}, 
    {2, 64},{22, 64}, {42, 64}, {62, 64}, {88, 64}, {114, 64}, {134, 64}, {154, 64}, {174, 64}, {192, 64}, {208, 64}, {224, 64}
}, {
    1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,
    8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,
    1, 1, 1, 1,    4, 1, 1, 1,     1, 4, 4, 4,
}};
#endif