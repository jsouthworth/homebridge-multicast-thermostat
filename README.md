# homebridge-multicast-thermometer

This homebridge-plugin implements the TemperatureSensor and
HumiditySensor services for a simple IP multicast thermometer. The
sensor listens for messages on 239.0.10.1:10000 in the following
format:

Temperature:
```
{
	id: xx:xx:xx:xx:xx:xx
	epoch: 1513317156,
	type: 'temperature',
	data: 21.8
}
```

where epoch is a 32bit unix epoch, and data is a float represneting
Celcius.

Humidity:
```
{
	id: xx:xx:xx:xx:xx:xx
	epoch: 1513317156,
	type: 'humidity',
	data: 50.1
}
```

where epoch is a 32bit unix epoch, and data is a float represneting
relative humidity.

Included is an Arduino project that will emit the correct
information. It is based on an Adafruit Feather M0 WiFi board and a
DHT22 temp/humidity sensor. The code uses the ArduinoProcessScheduler
that has been ported to the SAMD boards
https://github.com/jsouthworth/ArduinoProcessScheduler

## Installation

npm install -g homebridge-multicast-thermometer

## Configuration

No configuration is required, this is an auto plugin and it will respond automatically to thermometers speaking the correct protocol.
