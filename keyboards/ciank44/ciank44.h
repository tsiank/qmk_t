#pragma once

#include "quantum.h"

#define LAYOUT( \
    k00, k01, k02, k03, k04, k05, k06, k07, k08, k09, k0A, K0B, \
    k10, k11, k12, k13, k14, k15, k16, k17, k18, k19, k1A, \
    k20,         k22, k23, k24, k25, k26, k27, k28, k29, k2A, k2B, \
    k30, k31, k32, k33,         k35,         k37, k38, k39, k3A, k3B \
) \
{ \
    {k00, k01, k02, k03, k04, k05, k06, k07, k08, k09, k0A, K0B}, \
    {k10, k11, k12, k13, k14, k15, k16, k17, k18, k19, k1A}, \
    {k20, KC_NO, k22, k23, k24, k25, k26, k27, k28, k29, k2A, k2B}, \
    {k30, k31, k32, k33, KC_NO, k35, KC_NO, k37, k38, k39, k3A, k3B} \
}
