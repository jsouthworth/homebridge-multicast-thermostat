var Accessory, Service, Characteristic, UUIDGen;

module.exports = function(homebridge) {
  Service = homebridge.hap.Service;
  Characteristic = homebridge.hap.Characteristic;
  Accessory = homebridge.platformAccessory;
  UUIDGen = homebridge.hap.uuid;
  homebridge.registerPlatform('homebridge-multicast-thermometer',
			      'Multicast Thermometer',
                              MulticastThermometerPlatform,
                              true);
};

const EventEmitter = require('events');
const dgram = require('dgram');

function MulticastThermometerPlatform(log, config, api) {
  this.log = log;
  this.api = api;
  this.config = config;
  this.emitter = new EventEmitter();
  this.platformAccessories = {};
  this.thermAccessories = {};
  this.startListener();
  this.socketRetries = 0;
}

MulticastThermometerPlatform.prototype.accessories = function(callback) {
  callback(Object.values(this.platformAccessories));
}

MulticastThermometerPlatform.prototype.addAccessory = function(log, obj) {
  accessory = new MulticastThermometerAccessory(log, obj);
  platformAccessory = accessory.asPlatformAccessory();
  this.thermAccessories[obj.id] = accessory;
  this.platformAccessories[obj.id] = platformAccessory;
  this.api.registerPlatformAccessories("homebridge-multicast-thermometer",
                                       "Multicast Thermometer",
                                       [platformAccessory]);
  return accessory;
}

MulticastThermometerPlatform.prototype.startListener = function() {
  this.socket = dgram.createSocket('udp4');
  this.socket.on('error', function(err) {
    this.log(err);
    this.restartListener();
  }.bind(this));
  this.socket.on('listening', function() {
    this.socket.addMembership('239.0.10.1');
  }.bind(this));
  this.socket.on('message', function(msg, rinfo) {
    obj = JSON.parse(msg);
    accessory = this.thermAccessories[obj.id]
    if (accessory === undefined) {
      accessory = this.addAccessory(this.log, obj);
    }
    accessory.received(obj);
  }.bind(this));
  this.socket.bind(10000);
};

MulticastThermometerPlatform.prototype.stopListener = function(cb) {
  this.socket.close(cb);
};

MulticastThermometerPlatform.prototype.restartListener = function() {
  this.socketRetries += 1;
  if (this.socketRetries > 10) {
    this.stopListener();
    return;
  }
  this.stopListener(function(){
                      setTimeout(this.startListener.bind(this), 10000)
                    }.bind(this));
};

MulticastThermometerPlatform.prototype.configureAccessory = function(accessory) {
  this.log(accessory.displayName, "Configure Accessory");
  obj = {
    "id": accessory.displayName,
    "temperature": accessory
                   .getService(Service.TemperatureSensor)
                   .getCharacteristic(Characteristic.CurrentTemperature)
                   .value,
    "humidity": accessory
                   .getService(Service.HumiditySensor)
                   .getCharacteristic(Characteristic.CurrentRelativeHumidity)
                   .value
  };
  thermAccessory = new MulticastThermometerAccessory(this.log, obj, accessory);
  platformAccessory = thermAccessory.asPlatformAccessory();
  this.thermAccessories[obj.id] = thermAccessory;
  this.platformAccessories[obj.id] = platformAccessory;
}

function MulticastThermometerAccessory(log, obj, platformAccessory) {
  if (!obj.id) {
    throw new Error("Missing mandatory id");
  }
  this.log = log;

  this.id = obj.id;
  this.uuid = UUIDGen.generate(this.id);
  this.prefix = "(" + this.id + ")" + " | ";
  
  this.emitter = new EventEmitter();
  this.emitter.on('received-object', this.objectRX.bind(this));

  this.platformAccessory = this.generatePlatformAccessory(platformAccessory);
  this.platformAccessory.reachable = true;

  this.services = {
    tempSensor: this.platformAccessory.
      getService(Service.TemperatureSensor, "Temperature"),
    humiditySensor: this.platformAccessory.
      getService(Service.HumiditySensor, "Humidity"),
    battery: this.platformAccessory.
      getService(Service.BatteryService, "Battery")
  };

  this.characteristics = {
    batteryLevel: this.services.battery.getCharacteristic(
      Characteristic.BatteryLevel),
    currentTemp: this.services.tempSensor.getCharacteristic(
      Characteristic.CurrentTemperature),
    currentHumidity: this.services.humiditySensor.getCharacteristic(
      Characteristic.CurrentRelativeHumidity)
  };

  this.characteristics.batteryLevel
  .on('get', this.getBatteryLevel.bind(this));

  this.characteristics.currentTemp
  .on('get', this.getTemperature.bind(this));

  this.characteristics.currentHumidity
  .on('get', this.getHumidity.bind(this));


  this.battery = 100;
  this.temperature = 0;
  this.humidity = 0;
  this.tempEpoch = 0;
  this.humidityEpoch = 0;
  this.objectRX(obj);
  return this;
}

MulticastThermometerAccessory.prototype.generatePlatformAccessory = function(accessory) {
  if (accessory !== undefined) {
    return accessory;
  }
  accessory = new Accessory(this.id, this.uuid);
  accessory.
    addService(Service.TemperatureSensor, "Temperature");
  accessory.
    addService(Service.HumiditySensor, "Humidity");
  accessory.
    addService(Service.BatteryService, "Battery");
  return accessory;
}

MulticastThermometerAccessory.prototype.asPlatformAccessory = function() {
  return this.platformAccessory;
}

MulticastThermometerAccessory.prototype.received = function(obj) {
  return this.emitter.emit('received-object', obj);
}

MulticastThermometerAccessory.prototype.objectRX = function(obj) {
  switch(obj.type) {
    case "temperature":
    this.updateTemperature(obj);
    break;
    case "humidity":
    this.updateHumidity(obj);
    break;
  }
};

MulticastThermometerAccessory.prototype.getServices = function() {
  var services = this.services;
  return Object.keys(services)
	 .map(function(key) {
	   return services[key];
	 });
};

MulticastThermometerAccessory.prototype.getBatteryLevel = function(callback) {
  callback(null, this.battery);
};

MulticastThermometerAccessory.prototype.updateBatteryLevel = function(level) {
};

MulticastThermometerAccessory.prototype.getTemperature = function(callback) {
  callback(null, this.temperature);
};

MulticastThermometerAccessory.prototype.updateTemperature = function(obj) {
  if (this.tempEpoch > obj.epoch) {
    return;
  }
  if (this.temperature === obj.data) {
    return;
  }
  this.temperature = obj.data;
  this.tempEpoch = obj.epoch;
  this.characteristics.currentTemp.updateValue(this.temperature);
};

MulticastThermometerAccessory.prototype.getHumidity = function(callback) {
  callback(null, this.humidity);
};

MulticastThermometerAccessory.prototype.updateHumidity = function(obj) {
  if (this.humidityEpoch > obj.epoch) {
    return;
  }
  if (this.humidity === obj.data) {
    return;
  }
  this.humidity = obj.data;
  this.humidityEpoch = obj.epoch;
  this.characteristics.currentHumidity.updateValue(this.humidity);
};

MulticastThermometerAccessory.prototype.identify = function(callback) {
  this.log(this.prefix, "Identify");
  callback();
};
