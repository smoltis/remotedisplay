#include <FS.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <PubSubClient.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ArduinoJson.h>


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
  strcpy(config.mqtt_srv, "test.mosquitto.org");
  strcpy(config.mqtt_port, "1883");
  strcpy(config.mqtt_user, "admin");
  strcpy(config.mqtt_key, "admin");
  config.anonymous = false;
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

void setup() {
  Serial.begin(115200);
  Serial.println("Setting defaults");
  //custom parameters
  set_defaults();

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
          config.anonymous = (bool)json["anonymous"];
          Serial.println("updated runtime config");
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }


  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  // 3 min to configure AP
  wifiManager.setConfigPortalTimeout(180);

  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", config.mqtt_srv, 64);
  wifiManager.addParameter(&custom_mqtt_server);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", config.mqtt_port, 5);
  wifiManager.addParameter(&custom_mqtt_port);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", config.mqtt_user, 20);
  wifiManager.addParameter(&custom_mqtt_user);
  WiFiManagerParameter custom_mqtt_key("password", "mqtt password", config.mqtt_key, 32);
  wifiManager.addParameter(&custom_mqtt_key);
  WiFiManagerParameter custom_mqtt_anonymous("anonymous", "mqtt anonymous", (char*)config.anonymous, 1);
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
  config.anonymous = custom_mqtt_anonymous.getValue();

  // save custom parameters to FS
  if(shouldSaveConfig){
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_srv"] = config.mqtt_srv;
    json["mqtt_port"] = config.mqtt_port;
    json["mqtt_user"] = config.mqtt_user;
    json["mqtt_key"] = config.mqtt_key;
    json["anonymous"] = (bool)config.anonymous;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile){
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
  Serial.println("local ip:");
  Serial.println(WiFi.localIP());

}

void loop() {
  // put your main code here, to run repeatedly:

}