#include <FS.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>

#include <PubSubClient.h>

#include <MD_MAX72xx.h>
#include <SPI.h>

// macro
#define PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }
// usage: PRINT("\nProcessing new message: ", message);

// LED display setup
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define MAX_DEVICES  4

#define CLK_PIN   14  // or SCK
#define DATA_PIN  13  // or MOSI
#define CS_PIN    5

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Text parameters
#define CHAR_SPACING  1 // pixels between characters

// Function Prototypes

void readConfigFile();
void writeConfigFile();
void enterConfigMode();
void printText(uint8_t modStart, uint8_t modEnd, char *pMsg);

//flag for saving data
bool shouldSaveConfig = false;
//config
struct Config {
    char mqtt_srv[64];
    char mqtt_port[5];
    char mqtt_user[20];
    char mqtt_key[32];
    bool anonymous;
} config;

void set_defaults(){
  Serial.println("Setting defaults");
  strcpy(config.mqtt_srv, "test.mosquitto.org");
  strcpy(config.mqtt_port, "1883");
  strcpy(config.mqtt_user, "admin");
  strcpy(config.mqtt_key, "admin");
  config.anonymous = true;
}


// callbacks
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// mqtt
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void mqtt_msg_callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  char* msg = "";
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    msg += (char)payload[i];
  }
  Serial.println();
  printText(0, MAX_DEVICES-1, msg);
}

long lastReconnectAttempt = 0;

boolean reconnect() {
  // connect 
  bool connectionState;
  if(!config.anonymous) {
    connectionState = mqttClient.connect("esp8266Client", config.mqtt_user, config.mqtt_key);
  } else {
    connectionState = mqttClient.connect("esp8266Client");
  }
  // subscribe/publish
  if (connectionState) {
    // Once connected, publish an announcement...
    mqttClient.publish("outTopic-47487","hello world");
    // ... and resubscribe
    mqttClient.subscribe("inTopic-stan-47487");
  }
  return mqttClient.connected();
}

void setup() {
  Serial.begin(115200);

  printText(0, MAX_DEVICES-1, "Conf");

  readConfigFile();
  
  enterConfigMode();

  printText(0, MAX_DEVICES-1, "Mqtt");

  // setup mqtt
  mqttClient.setServer(config.mqtt_srv, strtol(config.mqtt_port, NULL, 0));

  lastReconnectAttempt = 0;

  printText(0, MAX_DEVICES-1, ".....");
}

int retry = 0;

void loop() {
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      Serial.println("MQTT reconnecting...");
      if (reconnect()) {
        lastReconnectAttempt = 0;
        retry = 0;
      }
      retry++;
      if (retry > 10) {
        enterConfigMode();
      }
    }
  } else {
    // Client connected
    Serial.println("MQTT connected");
    mqttClient.loop();
  }

}

void enterConfigMode() {

  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", config.mqtt_srv, 64);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", config.mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", config.mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_key("password", "mqtt password", config.mqtt_key, 32);
  char customhtml[24] = "type=\"checkbox\"";
  if (config.anonymous) {
      strcat(customhtml, " checked");
  }
  WiFiManagerParameter custom_mqtt_anonymous("anonymous", "anonymous", "T", 2, customhtml);
  
  WiFiManager wifiManager;

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  // 3 min to configure AP
  wifiManager.setConfigPortalTimeout(180);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_key);
  wifiManager.addParameter(&custom_mqtt_anonymous);

  // try to connect to WiFi. If it fails it starts in Access Point mode.  
  // goes into blocking loop awaiting configuration
  if (!wifiManager.autoConnect("LEDConfig", "admin")) {
    Serial.println("failed to connected and hit timeout");
    delay(3000);
    // reset and try again or put into deep sleep
    ESP.reset();
    delay(5000); 
  }

  Serial.println("Connected to WiFi");
  // read updated parameters
  strcpy(config.mqtt_srv, custom_mqtt_server.getValue());
  strcpy(config.mqtt_port, custom_mqtt_port.getValue()); 
  strcpy(config.mqtt_user, custom_mqtt_user.getValue());
  strcpy(config.mqtt_key, custom_mqtt_key.getValue());
  config.anonymous = (strncmp(custom_mqtt_anonymous.getValue(), "T", 1) == 0);
  
  writeConfigFile();

  Serial.println("local ip:");
  Serial.println(WiFi.localIP());
}

void readConfigFile() {
    // load additional params from config
  if (SPIFFS.begin()) {
    Serial.println("mounted FS");
   if (SPIFFS.exists("/config.json")){
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        //allocate buffer for the file
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(config.mqtt_srv, json["mqtt_srv"]);
          strcpy(config.mqtt_port, json["mqtt_port"]);
          strcpy(config.mqtt_user, json["mqtt_user"]);
          strcpy(config.mqtt_key, json["mqtt_key"]);
          if (json["anonymous"] == "T") {
            config.anonymous =  true;
          } else {
            config.anonymous =  false;
          }
          Serial.println("updated runtime config");
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
    // set
    set_defaults();
  }
}

void writeConfigFile() {
  // save custom parameters to FS
  if(shouldSaveConfig){
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    // if (json.containsKey("mqtt_srv")) {     
    // }
    json["mqtt_srv"] = config.mqtt_srv;
 
    // if (json.containsKey("mqtt_port")) {   
    // }
    json["mqtt_port"] = config.mqtt_port;
    // if (json.containsKey("mqtt_user")) {  
    // }
    json["mqtt_user"] = config.mqtt_user;
    // if (json.containsKey("mqtt_key")) {
    // }
    json["mqtt_key"] = config.mqtt_key;
    // if (json.containsKey("anonymous")) {
    // }

    if(config.anonymous){
      json["anonymous"] = "T";
    } else {
      json["anonymous"] = "F";
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile){
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
}

void printText(uint8_t modStart, uint8_t modEnd, char *pMsg)
// Print the text string to the LED matrix modules specified.
// Message area is padded with blank columns after printing.
{
  uint8_t   state = 0;
  uint8_t   curLen;
  uint16_t  showLen;
  uint8_t   cBuf[8];
  int16_t   col = ((modEnd + 1) * COL_SIZE) - 1;

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  do     // finite state machine to print the characters in the space available
  {
    switch(state)
    {
      case 0: // Load the next character from the font table
        // if we reached end of message, reset the message pointer
        if (*pMsg == '\0')
        {
          showLen = col - (modEnd * COL_SIZE);  // padding characters
          state = 2;
          break;
        }

        // retrieve the next character form the font file
        showLen = mx.getChar(*pMsg++, sizeof(cBuf)/sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state++;
        // !! deliberately fall through to next state to start displaying

      case 1: // display the next part of the character
        mx.setColumn(col--, cBuf[curLen++]);

        // done with font character, now display the space between chars
        if (curLen == showLen)
        {
          showLen = CHAR_SPACING;
          state = 2;
        }
        break;

      case 2: // initialize state for displaying empty columns
        curLen = 0;
        state++;
        // fall through

      case 3:	// display inter-character spacing or end of message padding (blank columns)
        mx.setColumn(col--, 0);
        curLen++;
        if (curLen == showLen)
          state = 0;
        break;

      default:
        col = -1;   // this definitely ends the do loop
    }
  } while (col >= (modStart * COL_SIZE));

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}
