#include "main.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
AsyncWebServer server(80); // Create a webserver object that listens for HTTP request on port 80
Ticker ticker;

volatile unsigned int ticks = 0;
volatile unsigned int totalTicks = 0;
volatile unsigned long int epoch = 0;
volatile unsigned long int lastSerialCallAt = 0;
volatile bool bmsInit = false;
volatile unsigned int loops = 0;
String sv;
String sb;

void initArray(Array *a, unsigned int initialSize) {
	a->used = 0;
	a->array = (char*) malloc(initialSize * sizeof(char));
	a->size = initialSize;
}

void insertArray(Array *a, char element) {
	if (a->used == a->size) {
		a->size *= 2;
		a->array = (char*) realloc(a->array, a->size * sizeof(char));
	}
	a->array[a->used++] = element;
}

void freeArray(Array *a) {
	printCharArrayHex(a);
	free(a->array);
	a->array = NULL;
	a->used = a->size = 0;
}

void ISRwatchdog() {
	++totalTicks;
	if (++ticks > 30) {
		ESP.restart();
	}
}

void setup(void) {
	Serial.begin(115200);
	delay(100);
	Serial.println("Starting");
	ticker.attach(1, ISRwatchdog);
	Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
	setupPins();
	initBms();
	if (!SPIFFS.begin()) {
		Serial.println("Error setting up SPIFFS!");
	}
	if (setupWiFi()) {
		initBms();
		setupWebServer();
		setupNTPClient();
		setupOTA();
		Serial.print("Connected to ");
		Serial.println(WiFi.SSID());  // Tell us what network we're connected to
		Serial.print("IP address:\t");
		Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
		initBms();
	}
}

void loop(void) {
	ticks = 0;
	ArduinoOTA.handle();
	if (timeClient.getEpochTime() - epoch > 20) {
		Serial.println("Checking WiFi connectivity.");
		epoch = timeClient.getEpochTime();
		if (!WiFi.isConnected()) {
			ESP.restart();
		}
		if (!timeClient.update()) {
			ESP.restart();
		}
	}
	if (bmsInit) {
		bmsInit = false;
		bmsv();
		bmsb();
	}
	if (loops % 1000 == 1) {
		bmsInit = true;
	}
	loops ++;
}

void setupPins() {
}

String readProperty(String props, String key) {
	int index = props.indexOf(key, 0);
	if (index != -1) {
		int start = index + key.length();
		start = props.indexOf('=', start);
		if (start != -1) {
			start = start + 1;
			int end = props.indexOf('\n', start);
			String value = props.substring(start,
					end == -1 ? props.length() : end);
			value.trim();
			return value;
		}
	}
	return "";
}

bool setupWiFi() {

	configureWiFi();
	if (SPIFFS.exists("/firmware.properties")) {
		File f = SPIFFS.open("/firmware.properties", "r");
		if (f && f.size()) {
			Serial.println("Reading firmware.properties");
			String props = "";
			while (f.available()) {
				props += char(f.read());
			}
			f.close();

			String ssid = readProperty(props, "wifi.ssid");
			String password = readProperty(props, "wifi.password");
			Serial.printf(
					"firmware properties wifi.ssid: %s; wifi.password: %s\r\n",
					ssid.c_str(), password.c_str());
			WiFi.begin(ssid.c_str(), password.c_str());

			Serial.printf("Connecting");
			while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
				ticks = 0;
				Serial.print('.');
				delay(40);
			}
			Serial.println();
			return true;
		}
	}
	return false;
}

void configureWiFi() {
	WiFi.persistent(false);
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
	},
	WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);

	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		WiFi.persistent(false);
		WiFi.disconnect(true);
		ESP.restart();
	},
	WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);
}

void setupWebServer() {
	sv = "";
	sb = "";

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/index.htm");
	});
	server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/index.js");
	});
	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/favicon.ico");
	});
	server.on("/v", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse(200,
		CONTENT_TYPE_APPLICATION_JSON, sv.c_str());
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
	server.on("/b", HTTP_GET, [](AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = request->beginResponse(200,
		CONTENT_TYPE_APPLICATION_JSON, sb.c_str());
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});

	server.onNotFound(notFound);

	server.begin();
	Serial.println("HTTP server started");
}

void notFound(AsyncWebServerRequest *request) {
	request->send(404, "text/plain", "Not found");
}

void setupNTPClient() {
	Serial.println("Synchronizing time with NTP");
	timeClient.begin();
	timeClient.setTimeOffset(39600);
	if (!timeClient.update()) {
		Serial.println("NTP update failed");
	}
	Serial.println(timeClient.getFormattedDate());
}

void setupOTA() {

	Serial.println("Setting up OTA");
	ArduinoOTA.setPassword("Sup3rSecr3t");
	ArduinoOTA.onStart([]() {
		Serial.println("Start");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR)
			Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR)
			Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR)
			Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR)
			Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR)
			Serial.println("End Failed");
	});
	ArduinoOTA.begin();
}

void bmsv() {

	char *buf = (char*) malloc(72 * sizeof(char));
	char data[] = { 0xdd, 0xa5, 0x04, 0x00, 0xff, 0xfc, 0x77 };
	Array av;
	initArray(&av, 35);
	bmsWrite(data, 7);
	bmsRead(&av);
	if (av.used < 5 || av.array[0] != 0xdd || av.array[av.used - 1] != 0x77) {
		freeArray(&av);
		return;
	}
	unsigned int dataLen = av.array[3];
	dataLen |= av.array[2] << 8;
	if (dataLen > 64) {
		freeArray(&av);
		return;
	}
	buf[71] = 0;
	int cells[14];
	for (int x = 0; x < 14; x++) {
		cells[x] = (av.array[(x * 2) + 4] << 8) | av.array[(x * 2) + 5];
	}

	for (int x = 0; x < 14; x++) {
		if (cells[x] > 5000 || cells[x] < 2500) {
			return;
		}
	}
	sprintf(buf, FMT_VOLTAGES, cells[0], cells[1], cells[2], cells[3], cells[4],
			cells[5], cells[6], cells[7], cells[8], cells[9], cells[10],
			cells[11], cells[12], cells[13]);
	freeArray(&av);
	sv = String(buf);
	free(buf);
	buf = NULL;
}

void bmsb() {

	char *buf = (char*) malloc(100 * sizeof(char));
	char data[] = { 0xdd, 0xa5, 0x03, 0x00, 0xff, 0xfd, 0x77 };
	Array ab;
	initArray(&ab, 34);
	bmsWrite(data, 7);
	bmsRead(&ab);
	if (ab.used < 5 || ab.array[0] != 0xdd || ab.array[ab.used - 1] != 0x77) {
		freeArray(&ab);
		return;
	}
	unsigned int dataLen = ab.array[3];
	dataLen |= ab.array[2] << 8;
	if (dataLen > 64) {
		freeArray(&ab);
		return;
	}
	unsigned int voltage = (ab.array[4] << 8) | ab.array[5];
	unsigned int remainingCapacity = (ab.array[8] << 8) | ab.array[9];
	unsigned int nominalCapacity = (ab.array[10] << 8) | ab.array[11];
	if (voltage < MIN_BATT_VOLTAGE || voltage > MAX_BATT_VOLTAGE
			|| nominalCapacity > MAX_NOMINAL_CAPACITY
			|| remainingCapacity > MAX_NOMINAL_CAPACITY) {
		freeArray(&ab);
		return;
	}
	buf[99] = 0;
	sprintf(buf, FMT_BASIC_INFO,
			voltage, // Voltage
			(ab.array[6] << 8) | ab.array[7],   // Current
			remainingCapacity, nominalCapacity,
			(ab.array[12] << 8) | ab.array[13], // cycle times
			(ab.array[14] << 8) | ab.array[15], // date of manufacture
			ab.array[16] << 8 | ab.array[17], // cell balance state 1s-16s
			ab.array[18], ab.array[19], // cell balance state 17s-32s
			ab.array[20], ab.array[21], // protection state
			ab.array[22], // software version
			ab.array[23], // Percentage of remaining capacity
			ab.array[24], // MOSFET control status
			ab.array[25], // battery serial number
			ab.array[26], // Number of NTCs
			((ab.array[27] << 8) | ab.array[28]), // NTC 0, high bit first, Using absolute temperature transmission, 2731+ (actual temperature *10), 0 degrees = 2731, 25 degrees = 2731+25*10 = 2981
			((ab.array[29] << 8) | ab.array[30])); // NTC 1, high bit first, Using absolute temperature transmission, 2731+ (actual temperature *10), 0 degrees = 2731, 25 degrees = 2731+25*10 = 2981
	freeArray(&ab);
	sb = String(buf);
	free(buf);
	buf = NULL;
}

void bmsWrite(char *data, int len) {
	int x = 0;
	while (x < len) {
		Serial2.print((char) data[x++]);
	}
	lastSerialCallAt = timeClient.getEpochTime();
}

void bmsRead(Array *a) {
	while (!Serial2.available()) {
		// wait until serial data is available
	}
	while (Serial2.available()) {
		insertArray(a, Serial2.read());
	}
}

void initBms() {
	Serial.println("==== BMS Init...");
	char buf0[7] = { 0xdd, 0xa5, 0x04, 0x00, 0xff, 0xfc, 0x77 };

	initBmsStub(buf0, 7);
	Serial.println("==== BMS init done.\r\n");
}

void initBmsStub(char *data, int len) {
	bmsWrite(data, len);
	Array a;
	initArray(&a, 8);
	bmsRead(&a);
	Serial.printf("==== a.used: %d\r\n", a.used);
	freeArray(&a);
}

void bmsDrainSerial() {
	Array a;
	initArray(&a, 32);
	bmsRead(&a);
	Serial.printf("drain: ");
	freeArray(&a);
}

void printCharArrayHex(Array *a) {
	unsigned int x = 0;
	while (x < a->used) {
		Serial.printf("0x%02x, ", a->array[x++]);
	}
	Serial.println();
}
