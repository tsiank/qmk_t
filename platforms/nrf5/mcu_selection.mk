ifneq ($(findstring NRF52840, $(MCU)),)
  # Cortex version
  MCU = NRF52840
  PLATFORM=NRF5
  PLATFORM_KEY=nrf5
  FIRMWARE_FORMAT?=bin
endif
