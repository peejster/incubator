// Please use an Arduino IDE 1.6.8 or greater

#include <OneWire.h>             // for reading data from DS18B20 sensors
#include <ESP8266WiFi.h>         // for WiFi connnectivity
#include <PubSubClient.h>        // for MQTT connectivity; documentation at http://pubsubclient.knolleary.net/api.html#PubSubClient1
#include <ArduinoJson.h>         // for JSON encoding of MQTT message

const int sensorCount = 2;       // define the number of DS18B20 sensors
const int tempPin = 2;           // DS18B20 temp sensors are connected to digital pin 2
const int lm35Pin = 0;           // LM35 temp sensor is connected to A0
const int heaterPin1 = 15;       // first heater switched via digital pin 15
const int heaterPin2 = 16;       // second heater switched via digital pin 15

OneWire ds(tempPin);             // create an instance of the temp sensors
WiFiClient espClient;            // create an instance of the wifi client
PubSubClient client(espClient);  // create an ESP8266 compatible client instance of the MQTT client

float temperature[sensorCount];  // store the temp reading from each DS18B20 sensor
float lm35Temperature;           // store the temp reaing from the LM35 temp sensor

char ssid[] = "<your_network_name>";              // your network SSID (name)
char pass[] = "<your_network_password>";          // your network password (use for WPA, or use as key for WEP)
char mqtt_server[] = "mqtt://things.ubidots.com"; // the ubidots.com MQTT server
int mqtt_port = 1883;                             // the ubidots.com MQTT port

long lastMsg = 0;
char msg[80];
bool heaterOn = false;

StaticJsonBuffer<80> jsonBuffer;
JsonObject& message = jsonBuffer.createObject();

void setup()
{
  Serial.begin(115200);          // initialize the serial monitor

  connectWifi();                 // establish wifi connection
  client.setServer(mqtt_server, mqtt_port);  // configure the MQTT client with the MQTT server address and port

  pinMode(heaterPin1, OUTPUT);    // initialize the first heater pin
  pinMode(heaterPin2, OUTPUT);    // initialize the second heater pin
  digitalWrite(heaterPin1, LOW);  // set heater pin to low (off)
  digitalWrite(heaterPin2, LOW);  // set heater pin to low (off)
}

void loop()
{
  // make sure you are connected to the wifi network
  if (WiFi.status() != WL_CONNECTED)
    connectWifi();

  // make sure you are connected to the MQTT server
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 60000)     // take temp readings and post to cloud every 60 seconds
  {
    lastMsg = now;
    
    // take reading from temperature sensors
    getDS18B20Temp();
    getLM35Temp();

    // check temperature in incubator
    if (temperature[0] < 32.5)
    {
      // turn on heater
      Serial.println("Turning heater on");
      digitalWrite(heaterPin1, HIGH);
      digitalWrite(heaterPin2, HIGH);
      heaterOn = true;
    }
    else if (temperature [0] > 33.5)
    {
      // turn off heater
      Serial.println("Turning heater off");
      digitalWrite(heaterPin1, LOW);
      digitalWrite(heaterPin2, LOW);
      heaterOn = false;
    }

    sendToCloud();
  }
}

// connect to wifi network
void connectWifi()
{
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected to wifi");
}

// record the temperature from all the DS18B20 sensors
void getDS18B20Temp()
{
  byte data[12];
  byte addr[sensorCount][8];
  
  // send a reset pulse
  ds.reset_search();
  
  // then receive presence pulse from the devices
  for (int i = 0; i < sensorCount; i++)
  {
    if ( !ds.search(addr[i]))
    {
      Serial.println("No devices.");
      ds.reset_search();
      temperature[i] = -1000;
      return;
    }
  }

  // get readings from the devices
  for (int i = 0; i < sensorCount; i++)
  {
    // verify the device's CRC
    if (OneWire::crc8(addr[i], 7) != addr[i][7])
    {
      Serial.println("CRC is not valid!");
      temperature[i] = -1000;
      return;
    }

    // check the device type
    // Serial.print("Device ");
    // Serial.print(i);

    if (!((addr[i][0] == 0x10) || (addr[i][0] == 0x28)))
    {
      Serial.print(" is not recognized: 0x");
      Serial.println(addr[i][0],HEX);
      temperature[i] = -1000;
      return;
    }

    // Initiate a conversion on the temp sensor  
    ds.reset();
    ds.select(addr[i]);
    ds.write(0x44,1);

    // Wait for the conversion to complete
    delay(850);

    // Read from the scratchpad
    ds.reset();
    ds.select(addr[i]);
    ds.write(0xBE);

    for (int j = 0; j < 9; j++)
    {
      data[j] = ds.read();
      // Serial.print(data[j], HEX);
      // Serial.print(" ");
    }

    // Serial.println("");

    // calculate temp
    temperature[i] = (((data[1] << 8) + data[0] )*0.0625);

    Serial.print("DS18B20 sensor ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(temperature[i]);
  }

  return;
}

void getLM35Temp()
{
  lm35Temperature = analogRead(lm35Pin) / 10.24;
  Serial.print("LM35 sensor: ");
  Serial.println(lm35Temperature);
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // Boolean connect(const char* clientId, const char* username, const char* password);
    if (client.connect("mqtt://things.ubidots.com", "<your_ubidots_token>", ""))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      // client.publish("/users/peejster/temp/ambient", "0");
      // ... and resubscribe
      //client.subscribe("inTopic");
    }
      else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void sendToCloud()
{
  // construct the JSON object
  message["amb"] = lm35Temperature;
  message["bot"] = temperature[0];
  message["top"] = temperature[1];
  if (heaterOn)
    message["heat"] = 1;
  else
    message["heat"] = 0;

  // convert JSON object to string
  message.printTo(msg, sizeof(msg));

  Serial.print("Publish message: ");
  Serial.println(msg);
  if (client.publish("/v1.6/devices/incubator", msg))
    Serial.println("Publish successful");
  else
    Serial.println("Publush failed");
}

