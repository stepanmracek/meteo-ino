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
const char* mqtt_server = "MQTT_SERVER";
bool enableMqtt = false;
const int rx_pin = D7;
const int tx_pin = D8;
MHZ19_uart mhz19;

const char* deviceName = "d1-shield";
const char* infoStr = "temperature:humidity:temperature2:co2";

float temp;
char tempStr[10];
char tempTopic[50];

float hum;
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

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

void setup() {
  Serial.begin(9600);

  snprintf(infoTopic, 49, "device/%s/info", deviceName);
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
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("connected");
  display.display();
  
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (enableMqtt) client.setServer(mqtt_server, 1883);

  mhz19.begin(rx_pin, tx_pin);
  mhz19.setAutoCalibration(true);

  Serial.println("Configuring HTPP server");
  server.on("/", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", deviceName);
  });
  server.on("/info", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", infoStr);
  });
  server.on("/temperature", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", tempStr);
  });
  server.on("/humidity", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", humStr);
  });
  server.on("/co2", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", co2Str);
  });
  server.on("/temperature2", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", temp2Str);
  });
  server.begin();

  Serial.println("leaving setup()");
}

void reconnect() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  while (!client.connected()) {
    display.println("mqtt...");
    display.display();
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      client.publish(infoTopic, infoStr, true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
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

  if (enableMqtt && !client.connected()) {
    reconnect();
  }
  if (enableMqtt) client.loop();

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

  server.handleClient();

  if (lastMeasure == current && !error) {
    snprintf(tempTopic, 49, "device/%s/temperature", deviceName);
    snprintf(tempStr, 9, "%f", temp);
    if (enableMqtt) client.publish(tempTopic, tempStr);

    snprintf(humTopic, 49, "device/%s/humidity", deviceName);
    snprintf(humStr, 9, "%f", hum);
    if (enableMqtt) client.publish(humTopic, humStr);

    snprintf(co2Topic, 49, "device/%s/co2", deviceName);
    snprintf(co2Str, 9, "%d", co2);
    if (enableMqtt) client.publish(co2Topic, co2Str);

    snprintf(temp2Topic, 49, "device/%s/temperature2", deviceName);
    snprintf(temp2Str, 9, "%d", temp2);
    if (enableMqtt) client.publish(temp2Topic, temp2Str);
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
        display.println(mqtt_server);
      }
    }
    display.display();
  }
}

