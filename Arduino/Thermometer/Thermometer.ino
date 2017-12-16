#include <ProcessScheduler.h>
#include <WiFi101.h>
#include <WiFiMDNSResponder.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <DHT_U.h>
#include <RingBufCPP.h>

#include "config.h"

class WifiProcess : public Process
{
public:
	WifiProcess(Scheduler &manager, const char *ssid, const char *pass) :
		Process(manager, LOW_PRIORITY, 30*SERVICE_SECONDLY),
		_ssid(ssid), _pass(pass), _status(WL_IDLE_STATUS) {}
	bool restart() {
		msg message = { .op	 = RESTART_EVENT };
		return this->_queue.add(message);
	}

protected:
	void setup() {
		//Adafruit Feather m0 wifi specific pin layout
		WiFi.setPins(8,7,4,2);
		WiFi.maxLowPowerMode();
		WiFi.begin(this->_ssid, this->_pass);
	}
	void cleanup() {
		WiFi.end();
	}
	void service() {
		msg message;
		while (receive(&message)) {
			switch (message.op) {
			case RESTART_EVENT:
				restart_service();
				break;
			}
		}
	}

private:
	enum message_op {
		RESTART_EVENT = 0,
	};
	
	typedef struct msg {
		message_op op;
	} msg;

	void restart_service() {
		WiFi.end();
		this->_status = WL_IDLE_STATUS;
		this->begin();
	}

	void begin() {
		this->_status = WiFi.begin(this->_ssid, this->_pass);
	}

	bool is_connected() {
		return this->_status == WL_CONNECTED;
	}

	bool receive(msg *message) {
		return this->_queue.pull(message);
	}

	RingBufCPP<msg, 1> _queue;
	uint8_t _status;
	const char *_ssid;
	const char *_pass;
};

class RTCProcess : public Process
{
public:
	RTCProcess(Scheduler &manager) :
		Process(manager, HIGH_PRIORITY, 2*SERVICE_SECONDLY),
		_rtc() {}
	uint32_t get_epoch() {
		return _rtc.getEpoch();
	}
protected:
	void setup() {
		this->_rtc.begin(true);
		this->set_time();
	}
	void service() {
		this->set_time();
	}
private:
	void set_time() {
		uint32_t epoch = WiFi.getTime();
		if (epoch < this->last_epoch) {
			return;
		}
		this->_rtc.setEpoch(epoch);
		last_epoch = epoch;
	}
	RTCZero _rtc;
	uint32_t last_epoch;
};

class PacketProcess : public Process
{
public:
PacketProcess(Scheduler &manager, RTCProcess &rtc, WifiProcess &wifi) :
		Process(manager, MEDIUM_PRIORITY, SERVICE_SECONDLY),
			_socket(), _addr(239, 0, 10, 1), _port(10000), _rtc(rtc), _wifi(wifi){}
	bool send_event(sensors_event_t &evt) {
		if (!this->_running) {
			return false;
		}
		msg message = { .op	 = SEND_SENSOR_EVENT, { .sensor_event = evt } };
		return this->_queue.add(message);
	}
protected:
	void service() {
		if (!this->start_if_necessary()) {
			return;
		}
		msg message;
		while (receive(&message)) {
			switch (message.op) {
			case SEND_SENSOR_EVENT:
				send(message.sensor_event);
				break;
			}
		}
	}
	void cleanup() {
		this->_socket.stop();
		this->_running = false;
	}
private:
	enum message_op {
		SEND_SENSOR_EVENT = 0,
	};
	
	typedef struct msg {
		message_op op;
		union {
			sensors_event_t sensor_event;
		};
	} msg;
	
	void send(sensors_event_t &event) {
		char buffer[128];
		uint32_t epoch = this->_rtc.get_epoch();
		if (epoch == 0) {
			return;
		}
		int n = this->encode_event(buffer, sizeof(buffer),
								   event, epoch);
		this->_socket.beginPacket(this->_addr, this->_port);
		this->_socket.write((uint8_t *)buffer, n);
		if (this->_socket.endPacket() != 1) {
			//Failed to send a packet, kill the socket and restart wifi
			this->cleanup();
			this->_wifi.restart();
		}
	}

	int encode_event(char *buffer, size_t size,
					 sensors_event_t &event, uint32_t epoch) {
		get_id();
		return snprintf(buffer, size,
			 "{\"id\":\"%s\", \"epoch\":%d,\"type\":\"%s\",\"data\":%.2f}",
						this->_id,
						epoch,
						this->event_get_type(event),
						this->event_get_data(event));
	}

	float event_get_data(sensors_event_t &event) {
		switch (event.type) {
		case SENSOR_TYPE_AMBIENT_TEMPERATURE:
			return event.temperature;
		case SENSOR_TYPE_RELATIVE_HUMIDITY:
			return	event.relative_humidity;
		}
	}

	const char * event_get_type(sensors_event_t &event) {
		switch (event.type) {
		case SENSOR_TYPE_AMBIENT_TEMPERATURE:
			return "temperature";
		case SENSOR_TYPE_RELATIVE_HUMIDITY:
			return	"humidity";
		}
	}

	bool start_if_necessary() {
		if (this->_running) {
			return true;
		}
		this->_running =
			this->_socket.beginMulticast(this->_addr, this->_port);
		return this->_running;
	}
	
	bool receive(msg *message) {
		return this->_queue.pull(message);
	}

	void get_id() {
		if (!this->needs_id()) {
			return;
		}
		byte mac[6];
		WiFi.macAddress(mac);
		snprintf(this->_id, sizeof(this->_id), "%02x:%02x:%02x:%02x:%02x:02x",
				 mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
		this->got_id();
	}

	bool needs_id() {
		return this->_needs_id;
	}

	void got_id() {
		this->_needs_id = false;
	}

	RTCProcess &_rtc;
	WifiProcess &_wifi;
	IPAddress _addr;
	uint16_t _port;
	bool _running = false;
	WiFiUDP _socket;
	RingBufCPP<msg, 10> _queue;
	bool _needs_id = true;
	char _id[18];
};

class ThermometerProcess : public Process
{
public:
	ThermometerProcess(Scheduler &manager, PacketProcess &packet_proc,
					   uint8_t pin, uint8_t type=DHT22) :
		Process(manager, MEDIUM_PRIORITY, 5*SERVICE_SECONDLY),
		_dht(pin, type), _pin(pin), _type(type),
		_delay(5*SERVICE_SECONDLY), _packet_proc(packet_proc) {}
protected:
	void setup() {
		this->_dht.begin();
		this->compute_delay();
		this->setPeriod(this->_delay);
	}
	void service() {
		sensors_event_t tevent, hevent;
		this->_dht.temperature().getEvent(&tevent);
		this->_dht.humidity().getEvent(&hevent);
		this->_packet_proc.send_event(tevent);
		this->_packet_proc.send_event(hevent);
	}
private:
	void compute_delay() {
		sensor_t sensor;
		this->_dht.temperature().getSensor(&sensor);
		this->_delay = max(sensor.min_delay / 1000, this->_delay);
	}
	uint8_t _pin;
	uint8_t _type;
	uint32_t _delay;
	DHT_Unified _dht;
	PacketProcess &_packet_proc;
};

Scheduler sched;

WifiProcess wifi_proc(sched, WIFI_SSID, WIFI_PASSWORD);
RTCProcess rtc_proc(sched);
PacketProcess packet_proc(sched, rtc_proc, wifi_proc);
ThermometerProcess therm_proc(sched, packet_proc, 12);

void setup() {
	sched.add(wifi_proc, true);
	sched.add(rtc_proc, true);
	sched.add(packet_proc, true);
	sched.add(therm_proc, true);
}

void loop() {
	sched.run();
	delay(1000);
}
