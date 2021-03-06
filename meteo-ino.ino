#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WEMOS_SHT3X.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include "MHZ19_uart.h"

#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

const int buttonPin = D3;
int buttonState = LOW;
int prevButtonState = LOW;

const char* ssid = "SSID";
const char* password = "PASSWORD";
const char* mqttServerIp = "MQTT_SERVER";
bool enableMqtt = false;
const int rx_pin = D7;
const int tx_pin = D8;
MHZ19_uart mhz19;

const char* deviceName = "d1-shield";
const char* infoStr = "temperature humidity temperature2 co2";

float temp = 20.0f;
char tempStr[10];
char tempTopic[50];

float hum = 50.0f;
char humStr[10];
char humTopic[50];

int temp2 = 20;
char temp2Str[10];
char temp2Topic[50];

int co2 = 400;
char co2Str[10];
char co2Topic[50];

char infoTopic[50];

enum DisplayMode {
  DM_None, DM_Temp, DM_Humidity, DM_CO2, DM_Temp2, DM_Connection, DM_size
};
DisplayMode displayMode = DM_None;

SHT3X sht30(0x45);

WiFiClient wifiClient;
WiFiServer telnetServer(4949);
PubSubClient mqttClient(wifiClient);
ESP8266WebServer webServer(80);

void setup() {
  Serial.begin(9600);

  snprintf(infoTopic, 49, "device/%s/list", deviceName);
  Serial.println(infoTopic);
  Serial.println(infoStr);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("connecting");
  display.display();

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  telnetServer.begin();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("connected");
  display.display();

  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (enableMqtt) mqttClient.setServer(mqttServerIp, 1883);

  mhz19.begin(rx_pin, tx_pin);
  mhz19.setAutoCalibration(true);

  Serial.println("Configuring HTTP server");
  webServer.on("/", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", deviceName);
  });
  webServer.on("/list", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", infoStr);
  });
  webServer.on("/temperature", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", tempStr);
  });
  webServer.on("/humidity", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", humStr);
  });
  webServer.on("/co2", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", co2Str);
  });
  webServer.on("/temperature2", []() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/plain", temp2Str);
  });
  webServer.begin();

  Serial.println("leaving setup()");
}

void mqttReconnect() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  while (!mqttClient.connected()) {
    display.println("mqtt...");
    display.display();
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("ESP8266Client")) {
      Serial.println("connected");
      mqttClient.publish(infoTopic, infoStr, true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void muninConfig(WiFiClient &client, const char* title, const char *label) {
  /* graph_title {{title}}
   * graph_vlabel {{label}}
   * {{label}}.label {{label}} */
  client.print("graph_title ");
  client.print(title);
  client.print("\n");

  client.print("graph_vlabel ");
  client.print(label);
  client.print("\n");

  client.print(label);
  client.print(".label ");
  client.print(label);
  client.print("\n.\n");
}

void muninFetch(WiFiClient &client, const char *label, const char *value) {
  /* {{label}}.value {{value}} */
  client.print(label);
  client.print(".value ");
  client.print(value);
  client.print("\n.\n");
}

void handleTelnetClient() {
  WiFiClient client = telnetServer.available();
  if (client) {
    Serial.println("client connected");
    client.setTimeout(100);
    client.print("# munin node at ");
    client.print(deviceName);
    client.print("\n");
    while (client.connected()) {
      if (client.available()) {
        String command = client.readString();
        Serial.print("command: ");
        Serial.println(command);
        if (command.startsWith("quit")) break;
        if (command.startsWith("version")) {
          client.print("munin node on ");
          client.print(deviceName);
          client.print(" version: 1.0.0\n");
          continue;
        }
        if (command.startsWith("nodes")) {
          client.print(deviceName);
          client.print("\n.\n");
        }
        if (command.startsWith("list")) {
          client.print(infoStr);
          client.print("\n");
          continue;
        }
        if (command.startsWith("config")) {
          if (command.startsWith("config temperature2")) muninConfig(client, "Temperature (from CO_2 sensor)", "temperature2");
          else if (command.startsWith("config temperature")) muninConfig(client, "Temperature", "temperature");
          else if (command.startsWith("config humidity")) muninConfig(client, "Humidity", "humidity");
          else if (command.startsWith("config co2")) muninConfig(client, "CO_2", "co2");
          else {
            client.print("# Not supported plugin. Try: ");
            client.print(infoStr);
            client.print("\n");
          }
          continue;
        }
        if (command.startsWith("fetch")) {
          if (command.startsWith("fetch temperature2")) muninFetch(client, "temperature2", temp2Str);
          else if (command.startsWith("fetch temperature")) muninFetch(client, "temperature", tempStr);
          else if (command.startsWith("fetch humidity")) muninFetch(client, "humidity", humStr);
          else if (command.startsWith("fetch co2")) muninFetch(client, "co2", co2Str);
          else {
            client.print("# Not supported plugin. Try: ");
            client.print(infoStr);
            client.print("\n");
          }
          continue;
        }
        client.print("# Unknown command. Try list, config, fetch, version or quit");
        client.print("\n");
      }
    }
    Serial.println("client disconnected");
    delay(1);
    client.stop();
  }
}

int lastMeasure = 0;
void loop() {
  bool pressed = false;
  buttonState = digitalRead(buttonPin);
  if (prevButtonState != buttonState && buttonState == LOW) {
    pressed = true;
  }
  prevButtonState = buttonState;

  if (enableMqtt) {
    if (!mqttClient.connected()) mqttReconnect();
    mqttClient.loop();
  }

  int current = millis();
  bool error = false;
  if (current - lastMeasure > 5000 || pressed) {
    lastMeasure = current;
    if (sht30.get() == 0) {
      temp = sht30.cTemp - 3;
      hum = sht30.humidity;
    } else {
      Serial.println("SHT30 error");
      error = true;
    }

    int status = mhz19.getStatus();
    if (status >= 0) {
      co2 = mhz19.getPPM();
      temp2 = mhz19.getTemperature() - 3;
    } else {
      Serial.println("MH-Z19 error");
      error = true;
    }
  }

  webServer.handleClient();
  handleTelnetClient();

  if (lastMeasure == current && !error) {
    snprintf(tempTopic, 49, "device/%s/temperature", deviceName);
    snprintf(tempStr, 9, "%.2f", temp);
    if (enableMqtt) mqttClient.publish(tempTopic, tempStr);

    snprintf(humTopic, 49, "device/%s/humidity", deviceName);
    snprintf(humStr, 9, "%.2f", hum);
    if (enableMqtt) mqttClient.publish(humTopic, humStr);

    snprintf(co2Topic, 49, "device/%s/co2", deviceName);
    snprintf(co2Str, 9, "%d", co2);
    if (enableMqtt) mqttClient.publish(co2Topic, co2Str);

    snprintf(temp2Topic, 49, "device/%s/temperature2", deviceName);
    snprintf(temp2Str, 9, "%d", temp2);
    if (enableMqtt) mqttClient.publish(temp2Topic, temp2Str);
  }

  if (pressed) {
    displayMode = (DisplayMode)((displayMode + 1) % DM_size);
  }

  if (pressed || lastMeasure == current) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    if (displayMode == DM_Temp) {
      display.println();
      display.println("temp (C)");
      display.println();
      display.setTextSize(2);
      display.print(temp);
    } else if (displayMode == DM_Humidity) {
      display.println();
      display.println("hum (%)");
      display.println();
      display.setTextSize(2);
      display.print(hum);
    } else if (displayMode == DM_CO2) {
      display.println();
      display.println("CO2 (ppm)");
      display.println();
      display.setTextSize(2);
      display.print(co2);
    } else if (displayMode == DM_Temp2) {
      display.println();
      display.println("temp2 (C)");
      display.println();
      display.setTextSize(2);
      display.print(temp2);
    } else if (displayMode == DM_Connection) {
      display.println("IP:");
      display.println(WiFi.localIP());
      if (enableMqtt) {
        display.println("mqtt:");
        display.println(mqttServerIp);
      }
    }
    display.display();
  }
}

