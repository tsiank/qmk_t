
// Initializing ADC
void adc_init(void);

// Measure VCC voltage
void adc_start(void);

/// Get VCC voltage in mV
uint16_t get_vcc();

/// Get battery level in percentage
uint8_t get_battery_level();