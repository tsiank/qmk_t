#include "ciank67.h"

#ifdef RGB_MATRIX_ENABLE
#include "is31fl3741.h"
const is31fl3741_led_t PROGMEM g_is31fl3741_leds[RGB_MATRIX_LED_COUNT] = {
    {0, SW1_CS3, SW1_CS2, SW1_CS1}, /* RGB1 */
    {0, SW1_CS6, SW1_CS5, SW1_CS4}, /* RGB2 */
    {0, SW1_CS9, SW1_CS8, SW1_CS7}, /* RGB3 */
    {0, SW1_CS12, SW1_CS11, SW1_CS10}, /* RGB4 */
    {0, SW1_CS15, SW1_CS14, SW1_CS13}, /* RGB5 */
    {0, SW1_CS18, SW1_CS17, SW1_CS16}, /* RGB6 */
    {0, SW1_CS21, SW1_CS20, SW1_CS19}, /* RGB7 */
    {0, SW1_CS24, SW1_CS23, SW1_CS22}, /* RGB8 */
    {0, SW1_CS27, SW1_CS26, SW1_CS25}, /* RGB9 */
    {0, SW1_CS30, SW1_CS29, SW1_CS28}, /* RGB10 */
    {0, SW1_CS33, SW1_CS32, SW1_CS31}, /* RGB11 */
    {0, SW1_CS36, SW1_CS35, SW1_CS34}, /* RGB12 */
    {0, SW6_CS39, SW6_CS38, SW6_CS37}, /* RGB13*/
    {0, SW6_CS36, SW6_CS35, SW6_CS34}, /* RGB14 */

    {0, SW2_CS3, SW2_CS2, SW2_CS1}, /* RGB15 */
    {0, SW2_CS6, SW2_CS5, SW2_CS4}, /* RGB16 */
    {0, SW2_CS9, SW2_CS8, SW2_CS7}, /* RGB17 */
    {0, SW2_CS12, SW2_CS11, SW2_CS10}, /* RGB18 */
    {0, SW2_CS15, SW2_CS14, SW2_CS13}, /* RGB19 */
    {0, SW2_CS18, SW2_CS17, SW2_CS16}, /* RGB20 */
    {0, SW2_CS21, SW2_CS20, SW2_CS19}, /* RGB21 */
    {0, SW2_CS24, SW2_CS23, SW2_CS22}, /* RGB22 */
    {0, SW2_CS27, SW2_CS26, SW2_CS25}, /* RGB23 */
    {0, SW2_CS30, SW2_CS29, SW2_CS28}, /* RGB24 */
    {0, SW2_CS33, SW2_CS32, SW2_CS31}, /* RGB25 */
    {0, SW2_CS36, SW2_CS35, SW2_CS34}, /* RGB26 */
    {0, SW7_CS39, SW7_CS38, SW7_CS37}, /* RGB27*/
    {0, SW7_CS36, SW7_CS35, SW7_CS34}, /* RGB28 */

    {0, SW3_CS3, SW3_CS2, SW3_CS1}, /* RGB29 */
    {0, SW3_CS6, SW3_CS5, SW3_CS4}, /* RGB30 */
    {0, SW3_CS9, SW3_CS8, SW3_CS7}, /* RGB31 */
    {0, SW3_CS12, SW3_CS11, SW3_CS10}, /* RGB32 */
    {0, SW3_CS15, SW3_CS14, SW3_CS13}, /* RGB33 */
    {0, SW3_CS18, SW3_CS17, SW3_CS16}, /* RGB34 */
    {0, SW3_CS21, SW3_CS20, SW3_CS19}, /* RGB35 */
    {0, SW3_CS24, SW3_CS23, SW3_CS22}, /* RGB36 */
    {0, SW3_CS27, SW3_CS26, SW3_CS25}, /* RGB37 */
    {0, SW3_CS30, SW3_CS29, SW3_CS28}, /* RGB38 */
    {0, SW3_CS33, SW3_CS32, SW3_CS31}, /* RGB39 */
    {0, SW3_CS36, SW3_CS35, SW3_CS34}, /* RGB40 */
    {0, SW3_CS39, SW3_CS38, SW3_CS37}, /* RGB41*/

    {0, SW4_CS3, SW4_CS2, SW4_CS1}, /* RGB42 */
    {0, SW4_CS6, SW4_CS5, SW4_CS4}, /* RGB43 */
    {0, SW4_CS9, SW4_CS8, SW4_CS7}, /* RGB44 */
    {0, SW4_CS12, SW4_CS11, SW4_CS10}, /* RGB45 */
    {0, SW4_CS15, SW4_CS14, SW4_CS13}, /* RGB46 */
    {0, SW4_CS18, SW4_CS17, SW4_CS16}, /* RGB47 */
    {0, SW4_CS21, SW4_CS20, SW4_CS19}, /* RGB48 */
    {0, SW4_CS24, SW4_CS23, SW4_CS22}, /* RGB49 */
    {0, SW4_CS27, SW4_CS26, SW4_CS25}, /* RGB50 */
    {0, SW4_CS30, SW4_CS29, SW4_CS28}, /* RGB51 */
    {0, SW4_CS33, SW4_CS32, SW4_CS31}, /* RGB52 */
    {0, SW4_CS36, SW4_CS35, SW4_CS34}, /* RGB53 */
    {0, SW8_CS39, SW8_CS38, SW8_CS37}, /* RGB54*/
    {0, SW8_CS36, SW8_CS35, SW8_CS34}, /* RGB55 */
    	
    {0, SW5_CS3, SW5_CS2, SW5_CS1}, /* RGB56 */
    {0, SW5_CS6, SW5_CS5, SW5_CS4}, /* RGB57 */
    {0, SW5_CS9, SW5_CS8, SW5_CS7}, /* RGB58 */
    {0, SW5_CS12, SW5_CS11, SW5_CS10}, /* RGB59 */
    {0, SW5_CS18, SW5_CS17, SW5_CS16}, /* RGB60 */
    {0, SW5_CS21, SW5_CS20, SW5_CS19}, /* RGB61 */
    {0, SW5_CS24, SW5_CS23, SW5_CS22}, /* RGB62 */
    {0, SW5_CS27, SW5_CS26, SW5_CS25}, /* RGB63 */
    {0, SW5_CS33, SW5_CS32, SW5_CS31}, /* RGB64 */
    {0, SW5_CS36, SW5_CS35, SW5_CS34}, /* RGB65 */
    {0, SW9_CS39, SW9_CS38, SW9_CS37}, /* RGB66*/
    {0, SW9_CS36, SW9_CS35, SW9_CS34}, /* RGB67 */
};

led_config_t g_led_config = {{
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13 },
    { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 },
    { 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, NO_LED },
    { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54 },
    { 55, 56, 57, 58, NO_LED, 59, 60, NO_LED, 61, 62, 63, 64, 65, 66}
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