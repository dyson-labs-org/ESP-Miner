#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
.PHONY: build power-and-flash power-and-log

build:
	idf.py build

power-and-flash:
	lab-control-cli run bitaxe_901x.labbench -- bash -c "sleep 1 && ./flashme"

power-and-log:
	lab-control-cli run bitaxe_901x_serial_log.labbench -- /usr/bin/bash -c "read"

clean: 
	ldf.py clean
