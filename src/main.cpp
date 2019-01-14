// SPI file System for config persistance
#include <FS.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>          
//Local DNS Server used for redirecting all requests to the configuration portal
#include <DNSServer.h>
//Local WebServer used to serve the configuration portal
#include <ESP8266WebServer.h>
// WiFi and paramters configuration portal
#include <WiFiManager.h>
// JSON serialization for config file        
#include <ArduinoJson.h>
// MQTT library
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
// LED Matrix display library  
#include <MD_MAX72xx.h>
#include <SPI.h>

// LED display setup, HW constants
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define MAX_DEVICES  4
////
#define CLK_PIN   D5  // or SCK
#define DATA_PIN  D7  // or MOSI
#define CS_PIN    D2

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Text parameter constants
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 1;

// global varialbes
char curMessage[MESG_SIZE];
char newMessage[MESG_SIZE];
bool newMessageAvailable = false;
int retry = 0;
bool shouldSaveConfig = false;
long lastReconnectAttempt = 0;

// Function Prototypes
void readConfigFile();
void writeConfigFile();
void enterConfigMode();
void MQTT_connect();
void onDemandPortal();
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
  strcpy(config.mqtt_srv, "MQTT.local");
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

// MQTT new message handling callback
//void callback(char* topic, byte* payload, unsigned int length) {
//  // handle message arrived
//  
//  Serial.print("Message arrived [");
//  Serial.print(topic);
//  Serial.print("] ");
//  
//  char* msg = "";
//  for (int i = 0; i < length; i++) {
//    Serial.print((char)payload[i]);
//    msg += (char)payload[i];
//    yield();
//  }
//  Serial.println();
//
//  strcpy(newMessage, msg);
//  newMessageAvailable = true;
//}

// MQTT client instance  
WiFiClient wifiClient;
//PubSubClient mqttClient(wifiClient);
// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&wifiClient, "test.mosquitto.org", 1883);
// Setup a feed called 'time' for subscribing to current time
Adafruit_MQTT_Subscribe incomingFeed = Adafruit_MQTT_Subscribe(&mqtt, "d3fum7ay9sl-incoming");
//Adafruit_MQTT_Publish satusFeed = Adafruit_MQTT_Publish(&mqtt, ""
//boolean reconnect() {
//  // connect 
//  bool connectionState;
//  if(!config.anonymous) {
//    Serial.print("MQTT with auth ");
//    Serial.print(config.mqtt_srv);
//    Serial.println();
//    mqttClient.setServer(config.mqtt_srv, strtol(config.mqtt_port, NULL, 0));
//    connectionState = mqttClient.connect("esp8266Client-hfd363d", config.mqtt_user, config.mqtt_key);
//  } else {
//    Serial.println("MQTT anonymous ");
//    Serial.print(config.mqtt_srv);
//    Serial.println();
//    mqttClient.setServer(config.mqtt_srv, strtol(config.mqtt_port, NULL, 0));
//    connectionState = mqttClient.connect("esp8266Client-hfd363d");
//  }
//  // subscribe/publish
//  if (connectionState) {
//    Serial.println("MQTT connected, publish");
//    // Once connected, publish an announcement...
//    mqttClient.publish("outTopic-47487","hello world");
//    // ... and resubscribe
//    Serial.println("MQTT connected, subscribe");
//    mqttClient.subscribe("inTopic-stan-47487");
//  }
//  return mqttClient.connected();
//}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t) {
// Callback function for data that is required for scrolling into the display
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char *p;
  static uint16_t curLen, showLen;
  static uint8_t  cBuf[8];
  uint8_t colData = 0;

  // finite state machine to control what we do on the callback
  switch (state)
  {
  case S_IDLE: // reset the message pointer and check for new message to load
    //PRINTS("\nS_IDLE");
    p = curMessage;      // reset the pointer to start of message
    if (newMessageAvailable)  // there is a new message waiting
    {
      strcpy(curMessage, newMessage); // copy it in
      newMessageAvailable = false;
    }
    state = S_NEXT_CHAR;
    break;

  case S_NEXT_CHAR: // Load the next character from the font table
    //PRINTS("\nS_NEXT_CHAR");
    if (*p == '\0')
      state = S_IDLE;
    else
    {
      showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
      curLen = 0;
      state = S_SHOW_CHAR;
    }
    break;

  case S_SHOW_CHAR: // display the next part of the character
    //PRINTS("\nS_SHOW_CHAR");
    colData = cBuf[curLen++];
    if (curLen < showLen)
      break;

    // set up the inter character spacing
    showLen = (*p != '\0' ? CHAR_SPACING : (MAX_DEVICES*COL_SIZE)/2);
    curLen = 0;
    state = S_SHOW_SPACE;
    // fall through

  case S_SHOW_SPACE:  // display inter-character spacing (blank column)
    //PRINT("\nS_ICSPACE: ", curLen);
    //PRINT("/", showLen);
    curLen++;
    if (curLen == showLen)
      state = S_NEXT_CHAR;
    break;

  default:
    state = S_IDLE;
  }

  return(colData);
}

void scrollText(void) {
  static uint32_t  prevTime = 0;
  // Is it time to scroll the text?
  if (millis() - prevTime >= SCROLL_DELAY)
  {
    mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
    prevTime = millis();      // starting point for next time
  }
}

void incomingCallback(char *data, uint16_t len) {
  Serial.println("Received: ");
  Serial.println(data);
  strcpy(newMessage, data);
  newMessageAvailable = true;
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("Config...");
  readConfigFile();
  
  enterConfigMode();
  Serial.println("MQTT setup...");
  // setup mqtt
  //mqttClient.setServer(config.mqtt_srv, strtol(config.mqtt_port, NULL, 0));
  //mqttClient.setCallback(callback);
  incomingFeed.setCallback(incomingCallback);
  mqtt.subscribe(&incomingFeed);
  lastReconnectAttempt = 0;
  // Display initialization
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  //mx.setShiftDataOutCallback(scrollDataSink);
  curMessage[0] = newMessage[0] = '\0';
}
uint32_t x=0;
void loop() {
  if (Serial.available() > 0) {
   char userInput = (char)Serial.read();
   if (userInput == 'c') {
       retry = 0;
       onDemandPortal();
   }
  }
//  if (!mqttClient.connected()) {
//    long now = millis();
//    if (now - lastReconnectAttempt > 5000) {
//      lastReconnectAttempt = now;
//      // Attempt to reconnect
//      Serial.print("MQTT reconnecting...");
//      Serial.println(retry);
//      yield();
//      retry++;
//      if (retry > 10) { 
//        retry = 0;
//        enterConfigMode();
//      }
//      if (reconnect()) {
//        lastReconnectAttempt = 0;
//        retry = 0;
//      }
//    }
//  } else {
//    // Client connected
//    yield();
//    delay(10);
//    mqttClient.loop();
//  }
  
  MQTT_connect();
  mqtt.processPackets(70);

  static uint32_t  prevTime = 0;
  // Is it time to scroll the text?
  if (millis() - prevTime >= 350000) {
    if(! mqtt.ping()) {
      mqtt.disconnect();
    }
  prevTime = millis();      // starting point for next time
  }

  scrollText();
}

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 10 seconds...");
       mqtt.disconnect();
       delay(10000);  // wait 10 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}

void enterConfigMode() {
  Serial.println("CONFIG MODE ACTIVATED...");
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
    Serial.println("failed to connected and hit timeout, will reset");
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

void onDemandPortal() {
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", config.mqtt_srv, 64);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", config.mqtt_port, 5);
    WiFiManagerParameter custom_mqtt_user("user", "mqtt user", config.mqtt_user, 20);
    WiFiManagerParameter custom_mqtt_key("password", "mqtt password", config.mqtt_key, 32);
    char customhtml[24] = "type=\"checkbox\"";
    if (config.anonymous) {
        strcat(customhtml, " checked");
    }
    WiFiManagerParameter custom_mqtt_anonymous("anonymous", "anonymous", "T", 2, customhtml);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //reset settings - for testing
    //wifiManager.resetSettings();

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep
    //in seconds
    //wifiManager.setTimeout(120);

    //it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration

    //WITHOUT THIS THE AP DOES NOT SEEM TO WORK PROPERLY WITH SDK 1.5 , update to at least 1.5.1
    //WiFi.mode(WIFI_STA);
     wifiManager.setConfigPortalTimeout(180);

     wifiManager.addParameter(&custom_mqtt_server);
     wifiManager.addParameter(&custom_mqtt_port);
     wifiManager.addParameter(&custom_mqtt_user);
     wifiManager.addParameter(&custom_mqtt_key);
     wifiManager.addParameter(&custom_mqtt_anonymous);
    
    if (!wifiManager.startConfigPortal("OnDemandAP")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
    strcpy(config.mqtt_srv, custom_mqtt_server.getValue());
    strcpy(config.mqtt_port, custom_mqtt_port.getValue()); 
    strcpy(config.mqtt_user, custom_mqtt_user.getValue());
    strcpy(config.mqtt_key, custom_mqtt_key.getValue());
    config.anonymous = (strncmp(custom_mqtt_anonymous.getValue(), "T", 1) == 0);
    writeConfigFile();
  
}
