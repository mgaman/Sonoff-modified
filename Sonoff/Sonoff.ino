/*
 *   Replacement software for the Sonoff WiFi operated switch
 *   No need to use their suspect app, just any MQTT client available
 *   
 */
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "ESP8266WiFi.h"
#include <WiFiClient.h>
#include "APNManage.h"
#include "FS.h"

//#define NODEMCU
//#define FORMAT_FILE_SYSTEM

#define SECURE_MQTT
// default SSID and password upon reset
#define DEFAULT_SSID "Henry"
#define DEFAULT_PWD "9876543210"

#define BROKER_ADDRESS    "test.mosquitto.org"
#ifdef SECURE_MQTT
#define BROKER_PORT 8883
WiFiClientSecure client;
#else
#define BROKER_PORT 1883
WiFiClient client;
#endif
#define WORST_RSSI -120
#define COMMAND_TOPIC     "unique/1/apn/do"
#define GENERAL_REPLY_TOPIC   "unique/1/apn/replies"


// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, BROKER_ADDRESS, BROKER_PORT);

// COMMAND_TOPIC/i  - initial SSID
// COMMAND_TOPIC/dssid   delete ssid
// COMMAND_TOPIC/?  - list all ssid's on disk
// COMMAND_TOPIC/l  - list all local networks
// COMMAND_TOPIC/a"ssid","password"  add new pair
// COMMAND_TOPIC/ron (roff) relay on/off  or report current state
// COMMAND_TOPIC/gon (goff) gpio14 on/off  or report current state

Adafruit_MQTT_Subscribe accesspointcontrol = Adafruit_MQTT_Subscribe(&mqtt,COMMAND_TOPIC );
Adafruit_MQTT_Publish apnreplies = Adafruit_MQTT_Publish(&mqtt,GENERAL_REPLY_TOPIC );

#define MAX_SSID_LENGTH 50
#define MAX_PASSWORD_LENGTH 50
char ssid[MAX_SSID_LENGTH], pwd[MAX_PASSWORD_LENGTH];
char bestssid[MAX_SSID_LENGTH], bestpwd[MAX_PASSWORD_LENGTH];
int bestrssi;
bool relaystate;
bool extrastate;

#define RELAY_PIN 12
#define LED_PIN 13
#define EXTRA_PIN 14

bool ssidFound = false;
bool wifiConnected = false;

void setup() {
  pinMode(RELAY_PIN,OUTPUT);
  RelayControl(false);
  pinMode(LED_PIN,OUTPUT);
  LedControl(false);
  pinMode(EXTRA_PIN,OUTPUT);
  extracontrol(false);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nSonoff code");
  SPIFFS.begin();
#ifdef FORMAT_FILE_SYSTEM
  Serial.print("Formatting FFS, takes around 30 secs...");
  SPIFFS.format();
  Serial.println("... Done");
#endif
  // 1st time create new file with default entry, else do nothing
  bool initrc = SPIFFS.exists(APN_FILENAME) ?  APNInit() : APNInit(DEFAULT_SSID,DEFAULT_PWD);
  if (!initrc)
  {
    Serial.println("Could not init APN file");
    LedBlink(10,100,100);
  }
  else
  {
    APNDump();
    int n = WiFi.scanNetworks();
    bestrssi = WORST_RSSI;
    for (int i = 0; i < n; i++)
    {
      if (APNFind(WiFi.SSID(i).c_str(),ssid,pwd))
      {
        ssidFound = true;
        if (WiFi.RSSI(i) > bestrssi)
        {
          bestrssi = WiFi.RSSI(i);
          strcpy(bestssid,ssid);
          strcpy(bestpwd,pwd);
        }
      }
    }
    if (ssidFound)
    {
      Serial.print("Best ");Serial.print(bestssid);Serial.print(" ");Serial.println(bestpwd);
      WiFi.begin(bestssid, bestpwd);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println();
      Serial.println("WiFi connected");
      Serial.print("IP address: "); Serial.println(WiFi.localIP());
      wifiConnected = true;
      // Setup MQTT subscriptions.
      Serial.println(mqtt.subscribe(&accesspointcontrol) ? "control added" : "control not added");
      LedBlink(5,1000,1000);
    }
    else
      LedBlink(10,100,500);
  }
}

void loop()
{
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  if (wifiConnected)
  {
    MQTT_connect();
  
    // this is our 'wait for incoming subscription packets' busy subloop
    // try to spend your time here
  
    Adafruit_MQTT_Subscribe *subscription;
    while ((subscription = mqtt.readSubscription(5000)))
    {
      if (subscription == &accesspointcontrol)
        accesspointcontrolhandler((char *)accesspointcontrol.lastread);
    }     
  }

  // ping the server to keep the mqtt connection alive
  // NOT required if you are publishing once every KEEPALIVE seconds

  if (! mqtt.ping())
    mqtt.disconnect();
}
// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() 
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
    return;

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}
extern char dumpbuff[];

void listlocalnetworks()
{
  strcpy(dumpbuff,"Local networks: ");
  for (int i=0; i<WiFi.scanNetworks();i++)
  {
    strcat(dumpbuff,WiFi.SSID(i).c_str());
    strcat(dumpbuff,",");
  }
  Serial.println(dumpbuff);
  apnreplies.publish(dumpbuff);
}
void relayonoffhandler(char message[])
{
  Serial.print(F("Got: "));
  Serial.println(message);
  if (stricmp(message,"on") == 0)
  {
    LedControl(true); // led on
    RelayControl(true);
  }
  else if (stricmp(message,"off") == 0)
  {
    LedControl(false); // led off
    RelayControl(false);
  }
  apnreplies.publish(relaystate ? "relay on" : "relay off");
}

void accesspointcontrolhandler(char message[])
{
  bool rc;
  Serial.print(F("Got: "));
  Serial.println(message);
  switch (*message)
  {
    case '?':
      APNDump();
      break;
    case 'd':
    case 'D':
      Serial.print("Delete ");
      Serial.print(message+1);
      rc = APNDelete(message+1);
      sprintf(dumpbuff,"Delete %s %s",message+1,rc ? " succeeded" : " failed");
      Serial.println(dumpbuff);
      apnreplies.publish(dumpbuff);
      break;
    case 'i':
    case 'I':
      rc = APNInit(DEFAULT_SSID,DEFAULT_PWD);
      sprintf(dumpbuff,"Reset %s",rc ? " succeeded" : " failed");
      Serial.println(dumpbuff);
      apnreplies.publish(dumpbuff);
      break; 
    case 'l':
    case 'L':
      listlocalnetworks();
      break;   
    case 'a':
    case 'A':
      accesspointaddhandler(message+1);
      break; 
    case 'r':
    case 'R':
      relayonoffhandler(message+1);
      break;
    case 'g':
    case 'G':
      extrahandler(message+1);
      break;     
  } 
}
void accesspointaddhandler(char message[])
{
  // check format OK, if OK append to the list
  bool legal = false;
  bool rc;
  char *ssid,*password;
  if (*message == '"')
  {
    // looking for "ssid","password"
    ssid = message + 1;
    char *c = strchr(1+message,'"');
    if (c)
    {
      *c++ = 0; // endmarker for ssid
      if (*c++ == ',' && *c++ == '"')
      {
        password = c;
        c = strchr(c,'"');
        if (c)
        {
          *c = 0; // endmarker for password
          legal = true;
        }
      }
    }
  }
  if (legal)
  {
   // Serial.print("Good ");Serial.print(ssid);Serial.print(' ');Serial.println(password);  
    rc = APNAppend(ssid,password);
    sprintf(dumpbuff,"Add %s %s",ssid,rc ? "succeeded" : "failed");
  }
  else
    sprintf(dumpbuff,"Add badly formed %s",message);
  Serial.println(dumpbuff);
  apnreplies.publish(dumpbuff);
}

void LedControl(bool state)
{
  digitalWrite(LED_PIN, state ?
#ifdef NODEMCU
                            HIGH : LOW);
#else
                            LOW : HIGH);
#endif
}
void LedBlink(int count,int intervalon,int intervaloff)
{
  for (int i=0; i< count; i++)
  {
    LedControl(true);
    delay(intervalon);
    LedControl(false);
    delay(intervaloff);
  }
}
void RelayControl(bool state)
{
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  relaystate = state;
}
void extracontrol(bool state)
{
  digitalWrite(EXTRA_PIN, state ? HIGH : LOW);
  extrastate = state;
}
void extrahandler(char message[])
{
  Serial.print(F("Got: "));
  Serial.println(message);
  if (stricmp(message,"on") == 0)
    extracontrol(true);
  else if (stricmp(message,"off") == 0)
    extracontrol(false);
  apnreplies.publish(extrastate? "extra on" : "extra off");
}


