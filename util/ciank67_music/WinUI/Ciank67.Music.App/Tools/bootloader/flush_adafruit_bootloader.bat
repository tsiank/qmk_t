@echo off
nrfjprog -f nrf52 --recover
nrfjprog -f nrf52 --eraseall
nrfjprog -f nrf52 --program %~dp0\pca10056_bootloader-0.3.2_s140_6.1.1.hex
nrfjprog -f nrf52 --reset
pause