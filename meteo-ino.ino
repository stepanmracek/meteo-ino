#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WiFi.h>
#include <WEMOS_SHT3X.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>

#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

const int buttonPin = D3;
int buttonState = LOW;
int prevButtonState = LOW;

const char* ssid = "SSID";
const char* password = "PASSWORD";
const char* mqtt_server = "MQTT_SERVER";

float temp;
float hum;

char tempStr[10];
char humStr[10];

enum DisplayMode {
  DM_None, DM_Temp, DM_Humidity, DM_Connection, DM_size
};
DisplayMode displayMode = DM_None;

SHT3X sht30(0x45);

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  //pinMode(buttonPin, INPUT);
  
  Serial.begin(9600);
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

  client.setServer(mqtt_server, 1883);

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
    Serial.println(buttonState);
    pressed = true;
  }
  prevButtonState = buttonState;

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  int current = millis();
  if (current - lastMeasure > 5000 || pressed) {
    lastMeasure = current;
    if (sht30.get() == 0) {
      temp = sht30.cTemp;
      hum = sht30.humidity;
      Serial.println(sht30.cTemp);
    } else {
      Serial.println("SHT30 error");
    }
  }

  if (lastMeasure == current) {
    snprintf(tempStr, 9, "%f", temp);
    snprintf(humStr, 9, "%f", hum);

    client.publish("temperature", tempStr);
    client.publish("humidity", humStr);
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
    } else if (displayMode == DM_Connection) {
      display.println("IP:");
      display.println(WiFi.localIP());
    }

    display.display();
  }
}
