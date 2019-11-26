#ifndef W_NETWORK_H
#define W_NETWORK_H

//#if defined(ESP32) || defined(ESP8266)

#include <Arduino.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#ifdef ESP8266
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#endif
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <StreamString.h>
#include "WHtmlPages.h"
#include "WAdapterMqtt.h"
#include "WStringStream.h"
#include "WDevice.h"
#include "WLed.h"
#include "WSettings.h"
#include "WJsonParser.h"

#define SIZE_MQTT_PACKET 512
#define SIZE_JSON_PACKET 1280
#define NO_LED -1
const String CONFIG_PASSWORD = "12345678";
const String APPLICATION_JSON = "application/json";

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
WiFiClient wifiClient;
WAdapterMqtt *mqttClient;

class WNetwork {
public:
	typedef std::function<void(void)> THandlerFunction;
	WNetwork(bool debug, String applicationName, String firmwareVersion,
			bool startWebServerAutomaticly, int statusLedPin) {
		WiFi.mode(WIFI_STA);
		this->applicationName = applicationName;
		this->firmwareVersion = firmwareVersion;
		this->startWebServerAutomaticly = startWebServerAutomaticly;
		this->webServer = nullptr;
		this->dnsApServer = nullptr;
		this->debug = debug;
		this->updateRunning = false;
		this->restartFlag = "";
		this->deepSleepFlag = nullptr;
		this->deepSleepSeconds = 0;
		settings = new WSettings(debug);
		settingsFound = loadSettings();
		this->mqttClient = nullptr;
		lastMqttConnect = lastWifiConnect = 0;
		gotIpEventHandler = WiFi.onStationModeGotIP(
				[this](const WiFiEventStationModeGotIP &event) {
					log(
							"Station connected, IP: "
									+ this->getDeviceIpAsString());
					//Connect, if webThing supported and Wifi is connected as client
					if ((this->isSupportingWebThing()) && (isWifiConnected())) {
						this->startWebServer();
					}
					this->notify(false);
				});
		disconnectedEventHandler = WiFi.onStationModeDisconnected(
				[this](const WiFiEventStationModeDisconnected &event) {
					log("Station disconnected");
					this->disconnectMqtt();
					this->lastMqttConnect = 0;
					this->notify(false);
				});
		if (this->isSupportingMqtt()) {
			this->mqttClient = new WAdapterMqtt(debug, wifiClient,
					SIZE_MQTT_PACKET);
			mqttClient->setCallback(
					[this](char *topic, byte *payload, unsigned int length) {
						this->mqttCallback(topic, payload, length);
					});
		}
		if (statusLedPin != NO_LED) {
			statusLed = new WLed(debug, statusLedPin);
			statusLed->setOn(true, 500);
		} else {
			statusLed = nullptr;
		}

	}

	//returns true, if no configuration mode and no own ap is opened
	bool loop(unsigned long now) {
		bool result = true;
		bool waitForWifiConnection = (deepSleepSeconds > 0);
		if ((!settingsFound) && (startWebServerAutomaticly)) {
			this->startWebServer();
		}
		if (!isWebServerRunning()) {
			if (!getSsid().equals("")) {
				//WiFi connection
				if ((WiFi.status() != WL_CONNECTED)
						&& ((lastWifiConnect == 0)
								|| (now - lastWifiConnect > 300000))) {
					log("Connecting to '" + getSsid() + "'");
					//Workaround: if disconnect is not called, WIFI connection fails after first startup
					WiFi.disconnect();
					String hostName = getIdx();
					hostName.replace(".", "-");
					hostName.replace(" ", "-");
					if (hostName.equals("")) {
						hostName = getClientName(false);
					}
					WiFi.hostname(hostName);
					WiFi.begin(getSsid().c_str(), getPassword().c_str());
					while ((waitForWifiConnection)
							&& (WiFi.status() != WL_CONNECTED)) {
						delay(500);
						if (millis() - now >= 5000) {
							break;
						}
					}
					//WiFi.waitForConnectResult();
					lastWifiConnect = now;
				}
			}
		} else {
			if (isSoftAP()) {
				dnsApServer->processNextRequest();
			}
			webServer->begin();
			webServer->handleClient();
			result = ((!isSoftAP()) && (!isUpdateRunning()));
		}
		//MQTT connection
		if ((isWifiConnected()) && (isSupportingMqtt())
				&& (!mqttClient->connected())
				&& ((lastMqttConnect == 0) || (now - lastMqttConnect > 300000))
				&& (!getMqttServer().equals(""))) {
			mqttReconnect();
			lastMqttConnect = now;
		}
		if ((!isUpdateRunning()) && (this->isMqttConnected())) {
			mqttClient->loop();
		}
		//Loop led
		if (statusLed != nullptr) {
			statusLed->loop(now);
		}
		//Loop Devices
		WDevice *device = firstDevice;
		while (device != nullptr) {
			device->loop(now);
			if ((this->isMqttConnected()) && (this->isSupportingMqtt())
					&& ((device->lastStateNotify == 0)
							|| ((device->stateNotifyInterval > 0)
									&& (now - device->lastStateNotify
											> device->stateNotifyInterval)))
					&& (device->isDeviceStateComplete())) {
				handleDeviceStateChange(device);
			}
			device = device->next;
		}
		//WebThingAdapter
		if ((!isUpdateRunning()) && (this->isSupportingWebThing())
				&& (isWifiConnected())) {
			MDNS.update();
		}
		//Restart required?
		if (!restartFlag.equals("")) {
			this->updateRunning = false;
			stopWebServer();
			delay(1000);
			ESP.restart();
			delay(2000);
		} else if (deepSleepFlag != nullptr) {
			if (deepSleepFlag->off()) {
				//Deep Sleep
				log("Go to deep sleep. Bye...");
				this->updateRunning = false;
				stopWebServer();
				delay(500);
				ESP.deepSleep(deepSleepSeconds * 1000 * 1000);
			} else {
				deepSleepFlag = nullptr;
			}
		}
		return result;
	}

	WSettings* getSettings() {
		return this->settings;
	}

	void setOnNotify(THandlerFunction onNotify) {
		this->onNotify = onNotify;
	}

	void setOnConfigurationFinished(THandlerFunction onConfigurationFinished) {
		this->onConfigurationFinished = onConfigurationFinished;
	}

	/*bool publishMqtt(String topic, JsonObject& json) {
	 return publishMqttImpl(getMqttTopic() + (topic != "" ? "/" + topic : ""), json);
	 }

	 bool publishMqtt(String topic, String payload) {
	 return publishMqttImpl(getMqttTopic() + (topic != "" ? "/" + topic : ""), payload);
	 }*/

	bool publishMqtt(String topic, String key, String value) {
		if (this->isSupportingMqtt()) {
			if (isMqttConnected()) {
				int length = topic.length() + key.length() + value.length() + 10;
				mqttClient->beginPublish(topic.c_str(), length, false);
				mqttClient->print(SBEGIN);
				mqttClient->print(QUOTE);
				mqttClient->print(key);
				mqttClient->print(QUOTE);
				mqttClient->print(DPOINT);
				mqttClient->print(QUOTE);
				mqttClient->print(value);
				mqttClient->print(QUOTE);
				mqttClient->print(SEND);
				if (mqttClient->endPublish()) {
					return true;
				} else {
					log("Sending MQTT message failed, rc=" + String(mqttClient->state()));
					this->disconnectMqtt();
					return false;
				}
			} else {
				if (!getMqttServer().equals("")) {
					log("Can't send MQTT. Not connected to server: " + getMqttServer());
				}
				return false;
			}
		}
	}

	// Creates a web server. If Wifi is not connected, then an own AP will be created
	void startWebServer() {
		if (!isWebServerRunning()) {
			String apSsid = getClientName(false);
			webServer = new ESP8266WebServer(80);
			if (WiFi.status() != WL_CONNECTED) {
				//Create own AP
				log(
						"Start AccessPoint for configuration. SSID '" + apSsid
								+ "'; password '" + CONFIG_PASSWORD + "'");
				dnsApServer = new DNSServer();
				WiFi.softAP(apSsid.c_str(), CONFIG_PASSWORD.c_str());
				dnsApServer->setErrorReplyCode(DNSReplyCode::NoError);
				dnsApServer->start(53, "*", WiFi.softAPIP());
			} else {
				log(
						"Start web server for configuration. IP "
								+ this->getDeviceIpAsString());
			}
			webServer->onNotFound(std::bind(&WNetwork::handleUnknown, this));
			if ((WiFi.status() != WL_CONNECTED)
					|| (!this->isSupportingWebThing())) {
				webServer->on("/", HTTP_GET, std::bind(&WNetwork::handleHttpRootRequest, this));
			}
			webServer->on("/config", HTTP_GET, std::bind(&WNetwork::handleHttpRootRequest, this));
			WDevice *device = this->firstDevice;
			while (device != nullptr) {
				if (device->isProvidingConfigPage()) {
					String deviceBase = "/device_" + device->getId();
					log("on " + deviceBase);
					webServer->on(deviceBase.c_str(), HTTP_GET,	std::bind(&WNetwork::handleHttpDeviceConfiguration, this, device));
					webServer->on(String("/saveDeviceConfiguration_" + device->getId()).c_str(),
							HTTP_GET,
							std::bind(&WNetwork::handleHttpSaveDeviceConfiguration, this, device));
				}
				device = device->next;
			}
			webServer->on("/wifi", HTTP_GET,
					std::bind(&WNetwork::handleHttpNetworkConfiguration, this));
			webServer->on("/saveConfiguration", HTTP_GET,
					std::bind(&WNetwork::handleHttpSaveConfiguration, this));
			webServer->on("/info", HTTP_GET,
					std::bind(&WNetwork::handleHttpInfo, this));
			webServer->on("/reset", HTTP_ANY,
					std::bind(&WNetwork::handleHttpReset, this));

			//firmware update
			webServer->on("/firmware", HTTP_GET,
					std::bind(&WNetwork::handleHttpFirmwareUpdate, this));
			webServer->on("/firmware", HTTP_POST,
					std::bind(&WNetwork::handleHttpFirmwareUpdateFinished, this),
					std::bind(&WNetwork::handleHttpFirmwareUpdateProgress, this));

			//WebThings
			if ((this->isSupportingWebThing()) && (this->isWifiConnected())) {
				//Make the thing discoverable
				if (MDNS.begin(this->getDeviceIpAsString())) {
					MDNS.addService("http", "tcp", 80);
					MDNS.addServiceTxt("http", "tcp", "url", "http://" + this->getDeviceIpAsString() + "/");
					MDNS.addServiceTxt("http", "tcp", "webthing", "true");
					log("MDNS responder started at " + this->getDeviceIpAsString());
				}
				webServer->on("/", HTTP_GET, std::bind(&WNetwork::sendDevicesStructure, this));
				WDevice *device = this->firstDevice;
				log("devices...");
				while (device != nullptr) {
					bindWebServerCalls(device);
					device = device->next;
				}
				log("devices finished.");
			}
			//Start http server
			webServer->begin();
			log("webServer started.");
			this->notify(true);
		}
	}

	void stopWebServer() {
		if ((isWebServerRunning()) && (!this->isSupportingWebThing()) && (!this->updateRunning)) {
			log("Close web configuration.");
			delay(100);
			//apServer->client().stop();
			webServer->stop();
			webServer = nullptr;
			if (onConfigurationFinished) {
				onConfigurationFinished();
			}
			this->notify(true);
		}
	}

	void enableWebServer(bool startWebServer) {
		if (startWebServer) {
			this->startWebServer();
		} else {
			this->stopWebServer();
		}
	}

	bool isWebServerRunning() {
		return (webServer != nullptr);
	}

	bool isUpdateRunning() {
		return this->updateRunning;
	}

	bool isSoftAP() {
		return ((isWebServerRunning()) && (dnsApServer != nullptr));
	}

	bool isWifiConnected() {
		return ((!isSoftAP()) && (!isUpdateRunning())
				&& (WiFi.status() == WL_CONNECTED));
	}

	bool isMqttConnected() {
		return ((this->isSupportingMqtt()) && (this->mqttClient != nullptr)
				&& (this->mqttClient->connected()));
	}

	void disconnectMqtt() {
		if (this->mqttClient != nullptr) {
			this->mqttClient->disconnect();
		}
	}

	IPAddress getDeviceIp() {
		return (isSoftAP() ? WiFi.softAPIP() : WiFi.localIP());
	}

	String getDeviceIpAsString() {
		return getDeviceIp().toString();
	}

	bool isSupportingWebThing() {
		return this->supportingWebThing->getBoolean();
	}

	bool isSupportingMqtt() {
		return this->supportingMqtt->getBoolean();
	}

	String getIdx() {
		return this->idx->getString();
	}

	String getSsid() {
		return this->ssid->getString();
	}

	String getPassword() {
		return settings->getString("password");
	}

	String getMqttServer() {
		return settings->getString("mqttServer");
	}

	String getMqttTopic() {
		return this->mqttTopic->getString();
	}

	String getMqttUser() {
		return settings->getString("mqttUser");
	}

	String getMqttPassword() {
		return settings->getString("mqttPassword");
	}

	void addDevice(WDevice *device) {
		if (statusLed == nullptr) {
			statusLed = device->getStatusLed();
			if (statusLed != nullptr) {
				statusLed->setOn(true, 500);
			}
		}
		if (this->lastDevice == nullptr) {
			this->firstDevice = device;
			this->lastDevice = device;
		} else {
			this->lastDevice->next = device;
			this->lastDevice = device;
		}

		/*ToDo
		AsyncWebSocket *webSocket = new AsyncWebSocket("/things/" + device->getId());
		device->setWebSocket(webSocket);
		*/
		bindWebServerCalls(device);
	}

	void setDeepSleepSeconds(int dsp) {
		this->deepSleepSeconds = dsp;
	}

	/*StaticJsonDocument<SIZE_MQTT_PACKET>* getDynamicJsonDocument(String pl) {
		StaticJsonDocument<SIZE_MQTT_PACKET>* jsonDocument = getDynamicJsonDocument();
		String payload = pl;
		auto error = deserializeJson(*jsonDocument, payload);
		if (error) {
			log("Can't parse json of time zone request result: " + payload);
			return nullptr;
		} else {
			//JsonObject json = jsonDocument->as<JsonObject>();
			return jsonDocument;
		}

	}*/

	void log(String debugMessage) {
		if (debug) {
			Serial.println(debugMessage);
		}
	}

private:
	WDevice *firstDevice = nullptr;
	WDevice *lastDevice = nullptr;
	THandlerFunction onNotify;
	THandlerFunction onConfigurationFinished;
	bool debug, updateRunning, startWebServerAutomaticly;
	//int jsonBufferSize;
	String restartFlag;
	DNSServer *dnsApServer;
	ESP8266WebServer *webServer;
	int networkState;
	String applicationName, firmwareVersion;
	String firmwareUpdateError;
	WProperty *supportingWebThing;
	WProperty *supportingMqtt;
	WProperty *ssid;
	WProperty *idx;
	WProperty *mqttTopic;
	WAdapterMqtt *mqttClient;
	long lastMqttConnect, lastWifiConnect;
	WStringStream* responseStream = nullptr;
	WLed *statusLed;
	WSettings *settings;
	bool settingsFound;
	WDevice *deepSleepFlag;
	int deepSleepSeconds;

	/*StaticJsonDocument<SIZE_MQTT_PACKET>* getDynamicJsonDocument() {
		return new StaticJsonDocument<SIZE_MQTT_PACKET>();
	}*/

	WStringStream* getResponseStream() {
		if (responseStream == nullptr) {
			responseStream = new WStringStream(SIZE_JSON_PACKET);
		}
		responseStream->flush();
		return responseStream;
	}

	void handleDeviceStateChange(WDevice *device) {
		String topic = getMqttTopic() + "/things/" + device->getId() + "/properties";
		mqttSendDeviceState(topic, device);
	}

	void mqttSendDeviceState(String topic, WDevice *device) {
		if ((this->isMqttConnected()) && (isSupportingMqtt())
				&& (device->isDeviceStateComplete())) {
			log("Send actual device state via MQTT");
			mqttClient->beginPublish(topic.c_str(), SIZE_MQTT_PACKET, false);
			WJson* json = new WJson(mqttClient);
			device->toJsonValues(json, MQTT);
			mqttClient->endPublish();

			device->lastStateNotify = millis();
			if ((deepSleepSeconds > 0)	&& ((!this->isSupportingWebThing())	|| (device->areAllPropertiesRequested()))) {
				deepSleepFlag = device;
			}

			/*DynamicJsonDocument* jsonDocument = getJsonDocument();
			 JsonObject json = jsonDocument->to<JsonObject>();
			 json["idx"] = this->getIdx();
			 json["ip"] = this->getDeviceIpAsString();
			 json["firmware"] = this->firmwareVersion;
			 json["webServerRunning"] = this->isWebServerRunning();
			 JsonObject& refJson = json;
			 device->toJson(refJson);
			 device->lastStateNotify = millis();
			 this->publishMqttImpl(topic, refJson);

			 if ((deepSleepSeconds > 0)
			 && ((!this->isSupportingWebThing()) || (device->areAllPropertiesRequested()))) {
			 deepSleepFlag = device;
			 }*/
		}
	}

	void mqttCallback(char *ptopic, byte *payload, unsigned int length) {
		//create character buffer with ending null terminator (string)
		/*char message_buff[this->mqttClient->getMaxPacketSize()];
		for (unsigned int i = 0; i < length; i++) {
			message_buff[i] = payload[i];
		}
		message_buff[length] = '\0';*/
		//forward to serial port
		log("Received MQTT callback: " + String(ptopic) + "/{"+ String((char *) payload) + "}");
		String topic = String(ptopic).substring(getMqttTopic().length() + 1);
		log("Topic short '" + topic + "'");
		if (topic.startsWith("things/")) {
			topic = topic.substring(String("things/").length());
			int i = topic.indexOf("/");
			if (i > -1) {
				String deviceId = topic.substring(0, i);
				log("look for device id '" + deviceId + "'");
				WDevice *device = this->getDeviceById(deviceId);
				if (device != nullptr) {
					topic = topic.substring(i + 1);
					if (topic.startsWith("properties")) {
						topic = topic.substring(String("properties").length() + 1);
						if (topic.equals("")) {
							if (length > 0) {
								WJsonParser* parser = new WJsonParser();
								if (parser->parse(device, (char *) payload) == nullptr) {
									//If payload is not parseable, send device state
									mqttSendDeviceState(String(ptopic), device);
								}
							} else {
								//If payload is empty, send device state
								mqttSendDeviceState(String(ptopic), device);
							}
						} else {
							WProperty* property = device->getPropertyById(topic);
							if ((property != nullptr) && (property->isVisible(MQTT))) {
								//Set Property
								property->parse((char *) payload);
								//DynamicJsonDocument* getJson(32);
								//JsonVariant value = doc.to<JsonVariant>();
								//value.set(message_buff);
								//JsonVariant value = message_buff;
								//property->setFromJson(value);
							}
						}
					}
				}
			}
		} else if (topic.equals("webServer")) {
			enableWebServer(String((char *) payload).equals("true"));
		}
	}

	/*bool publishMqttImpl(String absolutePath, JsonObject& json) {
	 if (this->isSupportingMqtt()) {
	 if (isMqttConnected()) {
	 //char payloadBuffer[this->mqttClient->getMaxPacketSize()];
	 char payloadBuffer[SIZE_MQTT_PACKET];
	 serializeJson(json, payloadBuffer);
	 //json.printTo(payloadBuffer, sizeof(payloadBuffer));
	 if (mqttClient->publish(absolutePath.c_str(), payloadBuffer)) {
	 return true;
	 } else {
	 log("Sending MQTT message failed, rc=" + String(mqttClient->state()));
	 this->disconnectMqtt();
	 return false;
	 }
	 return true;
	 } else {
	 if (!getMqttServer().equals("")) {
	 log("Can't send MQTT. Not connected to server: " + getMqttServer());
	 }
	 return false;
	 }
	 }
	 }*/

	bool publishMqttImpl(String absolutePath, String payload) {
		if (this->isSupportingMqtt()) {
			if (isMqttConnected()) {
				char payloadBuffer[this->mqttClient->getMaxPacketSize()];
				payload.toCharArray(payloadBuffer, sizeof(payloadBuffer));
				if (mqttClient->publish(absolutePath.c_str(), payloadBuffer)) {
					return true;
				} else {
					log(
							"Sending MQTT message failed, rc="
									+ String(mqttClient->state()));
					this->disconnectMqtt();
					return false;
				}
			} else {
				if (!getMqttServer().equals("")) {
					log(
							"Can't send MQTT. Not connected to server: "
									+ getMqttServer());
				}
				return false;
			}
		}
	}

	bool mqttReconnect() {
		if (this->isSupportingMqtt()) {
			log(
					"Connect to MQTT server: " + getMqttServer() + "; user: '"
							+ getMqttUser() + "'; password: '"
							+ getMqttPassword() + "'; clientName: '"
							+ getClientName(true) + "'");
			// Attempt to connect
			this->mqttClient->setServer(getMqttServer(), 1883);
			if (mqttClient->connect(getClientName(true).c_str(),
					getMqttUser().c_str(), //(mqttUser != "" ? mqttUser.c_str() : NULL),
					getMqttPassword().c_str())) { //(mqttPassword != "" ? mqttPassword.c_str() : NULL))) {
				log("Connected to MQTT server.");
				if (this->deepSleepSeconds == 0) {
					//Send device structure and status
					mqttClient->subscribe("devices/#");
					WDevice *device = this->firstDevice;
					while (device != nullptr) {
						//Send device structure
						//To minimize message size, every property as single message
						String deviceHRef = getMqttTopic() + "/things/"
								+ device->getId();
						WProperty *property = device->firstProperty;
						while (property != nullptr) {
							if (property->isVisible(MQTT)) {
								String topic = deviceHRef + "/properties/"
										+ property->getId();
								mqttClient->beginPublish(topic.c_str(),	SIZE_MQTT_PACKET, false);
								WJson* json = new WJson(mqttClient);
								property->toJsonStructure(json, deviceHRef);
								mqttClient->endPublish();
							}
							property = property->next;
						}
						device = device->next;
					}
					mqttClient->unsubscribe("devices/#");
				}
				//Subscribe to device specific topic
				mqttClient->subscribe(String(getMqttTopic() + "/#").c_str());
				notify(false);
				return true;
			} else {
				log(
						"Connection to MQTT server failed, rc="
								+ String(mqttClient->state()));
				if (startWebServerAutomaticly) {
					this->startWebServer();
				}
				notify(false);
				return false;
			}
		}
	}

	void notify(bool sendState) {
		if (statusLed != nullptr) {
			if (isWifiConnected()) {
				statusLed->setOn(false);
			} else if (isSoftAP()) {
				statusLed->setOn(true, 0);
			} else {
				statusLed->setOn(true, 500);
			}
		}
		if (sendState) {
			WDevice *device = this->firstDevice;
			while (device != nullptr) {
				handleDeviceStateChange(device);
				device = device->next;
			}
		}
		if (onNotify) {
			onNotify();
		}
	}

	void handleHttpRootRequest() {
		if (isWebServerRunning()) {
			if (restartFlag.equals("")) {
				String page = FPSTR(HTTP_HEAD_BEGIN);
				page.replace("{v}", applicationName);
				page += FPSTR(HTTP_SCRIPT);
				page += FPSTR(HTTP_STYLE);
				//page += _customHeadElement;
				page += FPSTR(HTTP_HEAD_END);
				page += getHttpCaption();
				WDevice *device = firstDevice;
				while (device != nullptr) {
					if (device->isProvidingConfigPage()) {
						page += FPSTR(HTTP_BUTTON_DEVICE);
						page.replace("{di}", device->getId());
						page.replace("{dn}", device->getName());
					}
					device = device->next;
				}
				page += FPSTR(HTTP_PAGE_ROOT);
				page += FPSTR(HTTP_BODY_END);
				webServer->send(200, "text/html", page);
			} else {
				String page = FPSTR(HTTP_HEAD_BEGIN);
				page.replace("{v}", F("Info"));
				page += FPSTR(HTTP_SCRIPT);
				page += FPSTR(HTTP_STYLE);
				page += F("<meta http-equiv=\"refresh\" content=\"10\">");
				page += FPSTR(HTTP_HEAD_END);
				page += restartFlag;
				page += F("<br><br>");
				page += F("Module will reset in a few seconds...");
				page += FPSTR(HTTP_BODY_END);
				webServer->send(200, "text/html", page);
			}
		}
	}

	void handleHttpDeviceConfiguration(WDevice *&device) {
		if (isWebServerRunning()) {
			log("Device config page");
			String page = FPSTR(HTTP_HEAD_BEGIN);
			page.replace("{v}", "Device Configuration");
			page += FPSTR(HTTP_SCRIPT);
			page += FPSTR(HTTP_STYLE);
			page += FPSTR(HTTP_HEAD_END);
			page += getHttpCaption();
			page += device->getConfigPage();
			page += FPSTR(HTTP_BODY_END);
			webServer->send(200, "text/html", page);
		}

	}

	void handleHttpNetworkConfiguration() {
		if (isWebServerRunning()) {
			log("Network config page");
			String page = FPSTR(HTTP_HEAD_BEGIN);
			page.replace("{v}", "Network Configuration");
			page += FPSTR(HTTP_SCRIPT);
			page += FPSTR(HTTP_STYLE);
			page += FPSTR(HTTP_HEAD_END);
			page += getHttpCaption();
			page += FPSTR(HTTP_PAGE_CONFIGURATION);
			page += FPSTR(HTTP_BODY_END);

			page.replace("{i}", getIdx());
			page.replace("{s}", getSsid());
			page.replace("{p}", getPassword());
			page.replace("{wt}",
					(this->isSupportingWebThing() ? "checked" : ""));
			page.replace("{mq}", (this->isSupportingMqtt() ? "checked" : ""));
			page.replace("{mqg}",
					(this->isSupportingMqtt() ? "block" : "none"));
			page.replace("{ms}", getMqttServer());
			page.replace("{mu}", getMqttUser());
			page.replace("{mp}", getMqttPassword());
			page.replace("{mt}", getMqttTopic());
			webServer->send(200, "text/html", page);
		}
	}

	void handleHttpSaveConfiguration() {
		if (isWebServerRunning()) {
			this->idx->setString(webServer->arg("i"));
			this->ssid->setString(webServer->arg("s"));
			settings->setString("password", webServer->arg("p"));
			this->supportingWebThing->setBoolean(webServer->arg("wt") == "true");
			this->supportingMqtt->setBoolean(webServer->arg("mq") == "true");
			settings->setString("mqttServer", webServer->arg("ms"));
			settings->setString("mqttUser", webServer->arg("mu"));
			settings->setString("mqttPassword", webServer->arg("mp"));
			this->mqttTopic->setString(webServer->arg("mt"));
			if ((startWebServerAutomaticly) && (!isSupportingWebThing())
					&& ((!isSupportingMqtt()) || (getMqttServer().equals(""))
							|| (getMqttTopic().equals("")))) {
				//if mqqt is completely unspecified, activate webthings
				this->supportingWebThing->setBoolean(true);
			}
			this->saveSettings();
			this->restart(F("Settings saved."));
		}
	}

	void handleHttpSaveDeviceConfiguration(WDevice *&device) {
		if (isWebServerRunning()) {
			log("handleHttpSaveDeviceConfiguration " + device->getId());
			device->saveConfigPage(webServer);
			this->saveSettings();
			this->restart(F("Device settings saved."));
		}
	}

	void handleHttpInfo() {
		if (isWebServerRunning()) {
			String page = FPSTR(HTTP_HEAD_BEGIN);
			page.replace("{v}", "Info");
			page += FPSTR(HTTP_SCRIPT);
			page += FPSTR(HTTP_STYLE);
			page += FPSTR(HTTP_HEAD_END);
			page += getHttpCaption();
			page += "<table>";
			page += "<tr><th>Chip ID:</th><td>";
			page += ESP.getChipId();
			page += "</td></tr>";
			page += "<tr><th>Flash Chip ID:</th><td>";
			page += ESP.getFlashChipId();
			page += "</td></tr>";
			page += "<tr><th>IDE Flash Size:</th><td>";
			page += ESP.getFlashChipSize();
			page += "</td></tr>";
			page += "<tr><th>Real Flash Size:</th><td>";
			page += ESP.getFlashChipRealSize();
			page += "</td></tr>";
			page += "<tr><th>IP address:</th><td>";
			page += this->getDeviceIpAsString();
			page += "</td></tr>";
			page += "<tr><th>MAC address:</th><td>";
			page += WiFi.macAddress();
			page += "</td></tr>";
			page += "<tr><th>Current sketch size:</th><td>";
			page += ESP.getSketchSize();
			page += "</td></tr>";
			page += "<tr><th>Available sketch size:</th><td>";
			page += ESP.getFreeSketchSpace();
			page += "</td></tr>";
			page += "<tr><th>EEPROM size:</th><td>";
			page += EEPROM.length();
			page += "</td></tr>";
			page += "</table>";
			page += FPSTR(HTTP_BODY_END);
			webServer->send(200, "text/html", page);
		}
	}

	/** Handle the reset page */
	void handleHttpReset() {
		if (isWebServerRunning()) {
			this->restart(F("Resetting was caused manually by web interface. "));
		}
	}

	String getHttpCaption() {
		return "<h2>" + applicationName + "</h2><h3>Revision " + firmwareVersion
				+ (debug ? " (debug)" : "") + "</h3>";
	}

	String getClientName(bool lowerCase) {
		String result = (applicationName.equals("") ? "ESP" : applicationName);
		result.replace(" ", "-");
		if (lowerCase) {
			result.replace("-", "");
			result.toLowerCase();
		}
		//result += "_";
		String chipId = String(ESP.getChipId());
		int resLength = result.length() + chipId.length() + 1 - 32;
		if (resLength > 0) {
			result.substring(0, 32 - resLength);
		}
		return result + "_" + chipId;
	}

	void handleHttpFirmwareUpdate() {
		if (isWebServerRunning()) {
			String page = FPSTR(HTTP_HEAD_BEGIN);
			page.replace("{v}", "Firmware update");
			page += FPSTR(HTTP_SCRIPT);
			page += FPSTR(HTTP_STYLE);
			page += FPSTR(HTTP_HEAD_END);
			page += getHttpCaption();
			page += FPSTR(HTTP_FORM_FIRMWARE);
			page += FPSTR(HTTP_BODY_END);
			webServer->send(200, "text/html", page);
		}
	}

	void handleHttpFirmwareUpdateFinished() {
		if (isWebServerRunning()) {
			if (Update.hasError()) {
				this->restart(String(F("Update error: ")) + firmwareUpdateError);
			} else {
				this->restart(F("Update successful."));
			}
		}
	}

	void handleHttpFirmwareUpdateProgress() {
		if (isWebServerRunning()) {

			HTTPUpload& upload = webServer->upload();
			//Start firmwareUpdate
			this->updateRunning = true;
			//Close existing MQTT connections
			this->disconnectMqtt();

			if (upload.status == UPLOAD_FILE_START){
				firmwareUpdateError = "";
				uint32_t free_space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
				log("Update starting: " + upload.filename);
				//Update.runAsync(true);
				if (!Update.begin(free_space)) {
					setFirmwareUpdateError("Can't start update (" + String(free_space) + "): ");
				}
			} else if (upload.status == UPLOAD_FILE_WRITE) {
			    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
			    	setFirmwareUpdateError("Can't upload file: ");
			    }
			} else if (upload.status == UPLOAD_FILE_END) {
			    if (Update.end(true)) { //true to set the size to the current progress
			    	log("Update complete: ");
			    } else {
			    	setFirmwareUpdateError("Can't finish update: ");
			    }
			}
		}
	}

	String getFirmwareUpdateErrorMessage() {
		switch (Update.getError()) {
		case UPDATE_ERROR_OK:
			return "No Error";
		case UPDATE_ERROR_WRITE:
			return "Flash Write Failed";
		case UPDATE_ERROR_ERASE:
			return "Flash Erase Failed";
		case UPDATE_ERROR_READ:
			return "Flash Read Failed";
		case UPDATE_ERROR_SPACE:
			return "Not Enough Space";
		case UPDATE_ERROR_SIZE:
			return "Bad Size Given";
		case UPDATE_ERROR_STREAM:
			return "Stream Read Timeout";
		case UPDATE_ERROR_MD5:
			return "MD5 Failed: ";
		case UPDATE_ERROR_SIGN:
			return "Signature verification failed";
		case UPDATE_ERROR_FLASH_CONFIG:
			return "Flash config wrong.";
		case UPDATE_ERROR_NEW_FLASH_CONFIG:
			return "New Flash config wrong.";
		case UPDATE_ERROR_MAGIC_BYTE:
			return "Magic byte is wrong, not 0xE9";
		case UPDATE_ERROR_BOOTSTRAP:
			return "Invalid bootstrapping state, reset ESP8266 before updating";
		default:
			return "UNKNOWN";
		}
	}

	void setFirmwareUpdateError(String msg) {
		firmwareUpdateError = getFirmwareUpdateErrorMessage();
		log(msg + firmwareUpdateError);
	}

	void restart(String reasonMessage) {
		this->restartFlag = reasonMessage;
		webServer->send(302, "text/html", reasonMessage);
		webServer->sendHeader("Location", "/config",true);
		webServer->send(302, "text/plain", "");
		//webServer->redirect("/config");
	}

	bool loadSettings() {
		this->idx = settings->registerString("idx", 32,
				this->getClientName(true));
		this->ssid = settings->registerString("ssid", 32, "");
		settings->registerString("password", 64, "");
		this->supportingWebThing = settings->registerBoolean(
				"supportingWebThing", true);
		this->supportingMqtt = settings->registerBoolean("supportingMqtt",
				false);
		settings->registerString("mqttServer", 32, "");
		settings->registerString("mqttUser", 32, "");
		settings->registerString("mqttPassword", 64, "");
		this->mqttTopic = settings->registerString("mqttTopic", 64, getIdx());
		bool settingsStored = settings->existsSettings();
		if (settingsStored) {
			if (getMqttTopic().equals("")) {
				this->mqttTopic->setString(this->getClientName(true));
			}
			if ((isSupportingMqtt()) && (this->mqttClient != nullptr)) {
				this->disconnectMqtt();
			}
			settingsStored = ((!getSsid().equals(""))
					&& (((isSupportingMqtt()) && (!getMqttServer().equals("")))
							|| (isSupportingWebThing())));
			if (settingsStored) {
				log("Settings loaded successfully:");
			} else {
				log("Settings are not complete:");
			}
			log(
					"SSID '" + getSsid() + "'; MQTT is "
							+ (isSupportingMqtt() ?
									" enabled; MQTT server '" + getMqttServer()
											+ "'" :
									"disabled") + "; Mozilla WebThings is "
							+ (isSupportingWebThing() ? "enabled" : "disabled"));
		}
		EEPROM.end();
		return settingsStored;
	}

	void saveSettings() {
		settings->save();
	}

	void handleUnknown() {
		webServer->send(404);
	}

	void sendDevicesStructure() {
		log("Send description for all devices... ");
		WStringStream* response = getResponseStream();
		WJson* json = new WJson(response);
		json->beginArray();
		WDevice *device = this->firstDevice;
		while (device != nullptr) {
			if (device->isVisible(WEBTHING)) {
				device->toJsonStructure(json, "", WEBTHING);
			}
			device = device->next;
		}
		json->endArray();
		log("Send description for all devices collected. ");
		webServer->send(200, APPLICATION_JSON, response->c_str());
		log("Send description for all devices sended. ");
	}

	void sendDeviceStructure(WDevice *&device) {
		log("Send description for device: " + device->getId());
		WStringStream* response = getResponseStream();
		WJson* json = new WJson(response);
		device->toJsonStructure(json, "", WEBTHING);
		webServer->send(200, APPLICATION_JSON, response->c_str());
	}

	void sendDeviceValues(WDevice *&device) {
		log("Send all properties for device: " + device->getId());
		WStringStream* response = getResponseStream();//request->beginResponseStream("application/json");
		WJson* json = new WJson(response);
		device->toJsonValues(json, WEBTHING);
		webServer->send(200, APPLICATION_JSON, response->c_str());
	}

	void getPropertyValue(WProperty *property) {
		WStringStream* response = getResponseStream();
		WJson* json = new WJson(response);
		json->beginObject();
		property->toJsonValue(json);
		json->endObject();
		property->setRequested(true);
		log("getPropertyValue " + String(response->c_str()));
		webServer->send(200, APPLICATION_JSON, response->c_str());

		if (deepSleepSeconds > 0) {
			WDevice *device = firstDevice;
			while ((device != nullptr) && (deepSleepFlag == nullptr)) {
				if ((!this->isSupportingWebThing()) || (device->areAllPropertiesRequested())) {
					deepSleepFlag = device;
				}
				device = device->next;
			}
		}
	}

	void setPropertyValue(WDevice *device) {
		if (webServer->hasArg("plain") == false) {
			webServer->send(422);
			return;
		}

		WJsonParser* parser = new WJsonParser();
		WProperty* property = parser->parse(device, webServer->arg("plain").c_str());
		if (property != nullptr) {
			//response new value
			log("Set property value: " + property->getId() + " (web request)" + webServer->arg("plain"));
			WStringStream* response = getResponseStream();
			WJson* json = new WJson(response);
			json->beginObject();
			property->toJsonValue(json);
			json->endObject();
			webServer->send(200, APPLICATION_JSON, response->c_str());
		} else {
			// unable to parse json
			log("unable to parse json: " + webServer->arg("plain"));
			webServer->send(500);
		}


		/*StaticJsonDocument<SIZE_MQTT_PACKET>* jsonDoc = getDynamicJsonDocument(webServer->arg("plain"));
		if (jsonDoc != nullptr) {
			JsonObject jsonOld = jsonDoc->as<JsonObject>();
			//set value
			JsonVariant newValue = jsonOld[property->getId()];
			jsonDoc->clear();
			log("Set property value: " + property->getId() + " to " + newValue.as<String>());
			property->setFromJson(newValue);
			//response new value
			WStringStream* response = getResponseStream();//request->beginResponseStream("application/json");
			WJson* json = new WJson(response);
			json->beginObject();
			property->toJsonValue(json);
			json->endObject();
			webServer->send(200, APPLICATION_JSON, response->c_str());
		} else {
			// unable to parse json
			webServer->send(500);
		}*/
	}

	void sendErrorMsg(int status, const char *msg) {
		WStringStream* response = getResponseStream();
		WJson* json = new WJson(response);
		json->beginObject();
		json->property("error", msg);
		json->property("status", status);
		json->endObject();
		webServer->send(200, APPLICATION_JSON, response->c_str());
	}

	//ToDo
	/*
	void handleThingWebSocket(AwsEventType type, void *arg, uint8_t *rawData, size_t len, WDevice *device) {
		// Ignore all except data packets
		if (type != WS_EVT_DATA)
			return;
		// Only consider non fragmented data
		AwsFrameInfo *info = (AwsFrameInfo*) arg;
		if (!info->final || info->index != 0 || info->len != len)
			return;
		// Web Thing only specifies text, not binary websocket transfers
		if (info->opcode != WS_TEXT)
			return;
		// In theory we could just have one websocket for all Things and react on the server->url() to route data.
		// Controllers will however establish a separate websocket connection for each Thing anyway as of in the
		// spec. For now each Thing stores its own Websocket connection object therefore.
		// Parse request
		StaticJsonDocument<SIZE_MQTT_PACKET>* jsonDoc = getDynamicJsonDocument((char *) rawData);
		if (jsonDoc == nullptr) {
			sendErrorMsg(*jsonDoc, *client, 400, "Invalid json");
			return;
		}
		JsonObject json = jsonDoc->as<JsonObject>();
		String messageType = json["messageType"].as<String>();
		const JsonVariant &dataVariant = json["data"];
		if (!dataVariant.is<JsonObject>()) {
			sendErrorMsg(*jsonDoc, *client, 400, "data must be an object");
			return;
		}
		const JsonObject data = dataVariant.as<JsonObject>();
		if (messageType == "setProperty") {
			for (auto kv : data) {
				WProperty *property = device->getPropertyById(kv.key().c_str());
				if ((property != nullptr) && (property->isVisible(WEBTHING))) {
					JsonVariant newValue = json[property->getId()];
					property->setFromJson(newValue);
				}
			}
			jsonDoc->clear();
			//ToDo
			// Send confirmation by sending back the received property object
			String jsonStr;
			serializeJson(data, jsonStr);
			//data.printTo(jsonStr);
			client->text(jsonStr.c_str(), jsonStr.length());
		} else if (messageType == "requestAction") {
			jsonDoc->clear();
			sendErrorMsg(*jsonDoc, *client, 400, "Not supported yet");
		} else if (messageType == "addEventSubscription") {
			// We report back all property state changes. We'd require a map
			// of subscribed properties per websocket connection otherwise
			jsonDoc->clear();
		}
	}
	*/

	void bindWebServerCalls(WDevice *device) {
		if (this->isWebServerRunning()) {
			log("bind webServer calls for webthings");
			String deviceBase = "/things/" + device->getId();
			WProperty *property = device->firstProperty;
			while (property != nullptr) {
				if (property->isVisible(WEBTHING)) {
					String propertyBase = deviceBase + "/properties/" + property->getId();
					webServer->on(propertyBase.c_str(), HTTP_GET, std::bind(&WNetwork::getPropertyValue, this, property));
					webServer->on(propertyBase.c_str(), HTTP_PUT, std::bind(&WNetwork::setPropertyValue, this, device));
				}
				property = property->next;
			}
			String propertiesBase = deviceBase + "/properties";
			webServer->on(propertiesBase.c_str(), HTTP_GET,	std::bind(&WNetwork::sendDeviceValues, this, device));
			webServer->on(deviceBase.c_str(), HTTP_GET,	std::bind(&WNetwork::sendDeviceStructure, this, device));

			//ToDo
			/*
			device->getWebSocket()->onEvent(
					std::bind(&WNetwork::handleThingWebSocket, this,
							std::placeholders::_1, std::placeholders::_2,
							std::placeholders::_3, std::placeholders::_4,
							std::placeholders::_5, std::placeholders::_6,
							device));
			webServer->addHandler(device->getWebSocket());
			*/
		}
	}

	WDevice* getDeviceById(String deviceId) {
		WDevice *device = this->firstDevice;
		while (device != nullptr) {
			if (device->getId().equals(deviceId)) {
				return device;
			}
		}
		return nullptr;
	}

};

//#endif    // ESP

#endif