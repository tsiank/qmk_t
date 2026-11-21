@echo off
nrfjprog -f nrf52 --recover
nrfjprog -f nrf52 --eraseall
nrfjprog -f nrf52 --program %~dp0\s140_nrf52_6.1.1_softdevice.hex
echo:%~dp0
nrfjprog -f nrf52 --program %~dp0\nrf52840_xxaa.hex
nrfjprog -f nrf52 --reset
pause