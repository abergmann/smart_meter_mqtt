# smart_meter_mqtt

Simple daemon that receives SML messages from a smart meter and pushes the current power consumption and the smart meter counter to an MQTT broker.

# Install

	aclocal
	autoconf 
	automake --add-missing
	./configure
	make
	make install

