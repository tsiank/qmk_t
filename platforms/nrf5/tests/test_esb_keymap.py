# SPDX-License-Identifier: GPL-2.0-or-later
"""Default three-mode firmware restores the normal layout and persistent VIA."""
import json
import re
import unittest
import test_esb as helpers

class DefaultKeymapTests(unittest.TestCase):
    def test_default_layout_initializes_via_once_and_preserves_edits(self):
        root = helpers.ROOT
        source = (root/'keyboards/ciank67/keymaps/default/keymap.c').read_text()
        def section(text, begin, end):
            start = text.index(begin)
            return text[start:text.index(end, start)+len(end)]
        layers = section(source, 'enum ciank67_layers {', '};')
        keys = section(source, 'const uint16_t PROGMEM keymaps', '\n};')
        keys = re.sub(r'/\*.*?\*/', '', keys, flags=re.S)
        keys = re.sub(r'//[^\n]*', '', keys)
        mode_defs = section(source, '#ifdef NRF5_ESB_TX', '\n#endif')
        functions = {'LAYOUT', 'LT', 'LALT_T', 'RSFT_T', 'MO', 'TG', 'TO', 'LCTL', 'LSFT'}
        aliases = {'MATRIX_ROWS', 'MATRIX_COLS', 'PROGMEM', 'NRF5_RADIO_MODE_KEY', 'NRF5_AUTO_MODE_KEY'}
        names = set(re.findall(r'\b[A-Z][A-Z0-9_]+\b', keys)) | {'KC_TRNS', 'KC_NO', 'OU_AUTO', 'OU_2P4G'}
        defines = ['#define KC_NO 0', '#define KC_TRNS 1', '#define _______ KC_TRNS']
        for n, name in enumerate(sorted(names-functions-aliases-{'KC_NO', 'KC_TRNS'}), 100):
            defines.append(f'#define {name} {n}')
        for n, name in enumerate(sorted(functions-{'LAYOUT'}), 1):
            if name == 'LT': defines.append('#define LT(layer,key) (0x4000 | ((layer)<<8) | (key))')
            elif name == 'LCTL': defines.append('#define LCTL(key) (0x0100 | (key))')
            elif name == 'LSFT': defines.append('#define LSFT(key) (0x0200 | (key))')
            else: defines.append(f'#define {name}(key) ({0x5000+n*0x100} | (key))')
        layout = json.loads((root/'keyboards/ciank67/keyboard.json').read_text())['layouts']['LAYOUT']['layout']
        cells = {(key['matrix'][0],key['matrix'][1]):f'k{i}' for i,key in enumerate(layout)}
        rows = ['{'+','.join(cells.get((r,c),'KC_NO') for c in range(14))+'}' for r in range(5)]
        defines.append('#define LAYOUT('+','.join(f'k{i}' for i in range(len(layout)))+') {'+','.join(rows)+'}')
        # The compatibility target must not force QWERTY or block/reset VIA.
        wrapper = (root/'keyboards/ciank67/keymaps/esb_tx/keymap.c').read_text()
        wrapper = re.sub(r'//[^\n]*', '', wrapper).strip()
        self.assertEqual(wrapper, '#include "../default/keymap.c"')
        self.assertIn('NRF5_ESB_ROLE = tx', (root/'keyboards/ciank67/keymaps/default/rules.mk').read_text())
        via = (root/'quantum/via.c').read_text()
        init = via[via.index('void via_init(void)'):via.index('// This is generalized')]
        dynamic = (root/'quantum/dynamic_keymap.c').read_text()
        reset = dynamic[dynamic.index('void dynamic_keymap_reset(void)'):dynamic.index('void dynamic_keymap_get_buffer')]
        layer_source = (root/'quantum/action_layer.c').read_text()
        resolve = section(layer_source, 'uint8_t layer_switch_get_layer(keypos_t key)', '\n}')
        helpers.EsbTests.run_c(self, r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PROGMEM
#define MATRIX_ROWS 5
#define MATRIX_COLS 14
#define DYNAMIC_KEYMAP_LAYER_COUNT 7
#define VIA_EEPROM_LAYOUT_OPTIONS_DEFAULT 0
#define MAX_LAYER 8
#define ACTION_TRANSPARENT 1
typedef uint8_t layer_state_t;
typedef struct {uint8_t row,col;} keypos_t;
typedef struct {uint16_t code;} action_t;
static layer_state_t layer_state,default_layer_state=1;
static uint16_t saved_map[7][5][14];
static uint8_t macros[32],layout_options=9;
static bool valid=false;
static unsigned resets;
static void via_init_kb(void){}
static void via_eeprom_set_valid(bool v){valid=v;}
static bool via_eeprom_is_valid(void){return valid;}
static void via_set_layout_options(uint8_t options){layout_options=options;}
static uint8_t via_get_layout_options(void){return layout_options;}
static void via_set_layout_options_kb(uint8_t options){(void)options;}
static void nvm_via_erase(void){}
static void nvm_dynamic_keymap_erase(void){resets++;}
static void dynamic_keymap_macro_reset(void){memset(macros,0,sizeof(macros));}
static void dynamic_keymap_set_keycode(uint8_t l,uint8_t r,uint8_t c,uint16_t k){saved_map[l][r][c]=k;}
void eeconfig_init_via(void);
'''+ '\n'.join(defines)+'\n'+layers+'\n'+mode_defs+'\n'+keys+r'''
uint16_t keycode_at_keymap_location_raw(uint8_t l,uint8_t r,uint8_t c){return l<7&&r<5&&c<14?keymaps[l][r][c]:KC_NO;}
uint16_t keymap_key_to_keycode(uint8_t l,keypos_t k){return saved_map[l][k.row][k.col];}
static action_t action_for_key(uint8_t l,keypos_t k){return (action_t){keymap_key_to_keycode(l,k)};}
'''+reset+'\n'+init+'\n'+resolve+r'''
int main(void){
 memset(saved_map,0x22,sizeof(saved_map)); memset(macros,0x77,sizeof(macros));
 via_init(); assert(valid&&resets==1&&layout_options==0);
 assert(!memcmp(saved_map,keymaps,sizeof(saved_map)));
 assert(_DVORAKR==0&&_SIGN==1&&_NUM==2&&_FN==3&&_QWERTY==4&&_RGBST==5&&_MOUSE==6);
 assert(layer_state==0&&default_layer_state==1); // Normal boot, no forced QWERTY.
 keypos_t q={1,1}, three={0,3};
 assert(keymap_key_to_keycode(layer_switch_get_layer(q),q)==KC_5);
 assert(keymaps[_DVORAKR][4][7]==MO(_FN));
 assert(keymaps[_DVORAKR][4][8]==LCTL(LSFT(KC_N)));
 assert(keymaps[_QWERTY][4][6]==MO(_FN)&&keymaps[_QWERTY][4][7]==KC_RALT);
 for(int l=_NUM;l<=_FN;l++){
  layer_state=1<<l;
  assert(keymap_key_to_keycode(layer_switch_get_layer(three),three)==OU_AUTO);
  assert(keymap_key_to_keycode(layer_switch_get_layer(q),q)==OU_USB);
  assert(keymap_key_to_keycode(layer_switch_get_layer((keypos_t){1,2}),(keypos_t){1,2})==OU_BT);
  assert(keymap_key_to_keycode(layer_switch_get_layer((keypos_t){1,4}),(keypos_t){1,4})==OU_2P4G);
 }
 // Subsequent starts retain user edits, macros and layout options.
 saved_map[_DVORAKR][1][1]=KC_Q; macros[0]=42; layout_options=3;
 layer_state=0; via_init();
 assert(resets==1&&macros[0]==42&&layout_options==3);
 assert(keymap_key_to_keycode(layer_switch_get_layer(q),q)==KC_Q);
}
''', flags=['-DVIA_ENABLE','-DNRF5_ESB_TX'])

if __name__ == '__main__': unittest.main()
