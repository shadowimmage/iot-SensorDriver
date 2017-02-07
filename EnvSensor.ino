/** EnvSensor.ino - arduino firmware file for the ESP8266 embedded computing plaltform/microcontroller
 * Author: Chase Sawyer
 * February 2017
 *
 * TODO: Iron out bugs with connections being dropped / what to do when it can't connect to wifi
 * TODO: 
 */


#include <ESP8266WiFi.h> // base Wifi library
#include <WiFiUdp.h> //for ntp
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include <Wire.h> //I2C

/* library from  https://github.com/squix78/esp8266-oled-ssd1306 */
#include "SSD1306.h" // runs 128x64 OLED display via i2c

/* Library to read BME 280 sensor */
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>


/************************* SSD1306  *************************************/
SSD1306 display(0x3C, SDA, SCL);

/************************* BME 280  *************************************/
#define SEALEVELPRESSURE_HPA (1013.25) // Look this up online for your area
Adafruit_BME280 bme; //i2c

/************************* WiFi Access Point(s) ********************************/

#define WLAN_SSID       "YOUR SSID"
#define WLAN_PASS       "SSIDPASSWORD"

/************************* Adafruit.io  *********************************/

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  8883                   // 8883 for MQTTS
#define AIO_USERNAME    "[username]"
#define AIO_KEY         "[key from io.adafruit.com]"

/************ Global State (you don't need to change this!) ******************/

// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// io.adafruit.com SHA1 fingerprint
const char* fingerprint = "26 96 1C 2A 51 07 FD 15 80 96 93 AE F7 32 CE B9 0D 01 55 C4";

/************************* MQTT Feeds ***************************************/

// Setup feeds for data publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
const char UNIT_ID[] = "1"; // change depending on unit we're flashing (for multiple identical hardware)
// const char TEMPFEED[] = AIO_USERNAME "/feeds/temp1";
// const char HUMFEED[] = AIO_USERNAME "/feeds/hum1";
// const char PRESFEED[] = AIO_USERNAME "/feeds/pres1";
Adafruit_MQTT_Publish mqttFeedTemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temp1");
Adafruit_MQTT_Publish mqttFeedHumidity = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/hum1");
Adafruit_MQTT_Publish mqttFeedPressure = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/pres1");
Adafruit_MQTT_Publish mqttFeedDebug = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/unitLogs");
// Setup feeds for Data Subscription - NOTE: Only relevant for Main unit / unit with screen.
// COMMENT OUT THESE LINES FOR NON-SUBSCRIBING UNITS
Adafruit_MQTT_Subscribe mqttSubTemp2 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/temp2");
Adafruit_MQTT_Subscribe mqttSubHum2 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/hum2");
Adafruit_MQTT_Subscribe mqttSubTemp3 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/temp3");
Adafruit_MQTT_Subscribe mqttSubHum3 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/hum3");
const boolean subscriptions = true;

/*************************** NTP Setup  ************************************/
unsigned int localPortUDP = 2390;
// IPAddress timeServerIP;
const char* ntpServerName = "time.nist.gov";
const uint8_t NTP_PACKET_SIZE = 48;
byte packetBuffer[ NTP_PACKET_SIZE]; // hold incomming and outgoing packets
WiFiUDP udp; // allow udp datagrams

/*************************** Local Data Stores ************************************/
const uint8_t HIST_BUF_SIZE = 120;
int8_t tempHistory[HIST_BUF_SIZE];
uint8_t humidHistory[HIST_BUF_SIZE];
uint8_t graph_pointer = 0; // allows history to be used as FIFO buffers
uint8_t graph_storedCount = 0; // allows graphs to only display real data

float minTemp = 500; // initial values guarantee start condition will set min/max properly.
float maxTemp = -500;
float minHum = 200;
float maxHum = -200;
float currentTemp;
float currentHumidity;
float currentPressure;
uint8_t remoteTemps[2];
uint8_t remoteHumidity[2];
unsigned long currentMillis;
unsigned long millisSinceNTP = 0;
unsigned long globalEpoch = 0;
unsigned long local_lastMillis = 0;
unsigned long page_lastMillis = 0;
unsigned long mqtt_lastMillis = 0;
unsigned long ntp_lastMillis = 0;
unsigned long graph_lastMillis = 0;

//DISPLAY STORAGE / SETTINGS:
uint8_t disp_page = 1;
boolean disp_RSSI = false;
unsigned long disp_rssiMillis = 0;

//SETTINGS / FLAGS:
const long NTP_OFFSET = -288000; // timezone correction (PST = -8hrs)
const boolean SERIAL_ENABLE = true; // enable/disable serial print strings
const boolean DEBUG = false; // enable/disable granular debug messages.
const unsigned long NTP_RATE = 150000; // 2.5 minutes
const unsigned long LOCAL_RATE = 1000; // 1 second
const unsigned long MQTT_RATE = 5000; // 5 seconds
const unsigned long GRAPH_SAV_RATE = 720000; // 12 minutes
// const unsigned long GRAPH_SAV_RATE = 30000; // 30 sec (for testing)
const unsigned long PAGE_FLIP = 10000; // 10 seconds
const unsigned long DISP_RSSI_SWITCH_RATE = 5000; 
const uint8_t DISP_PAGE_MAIN = 1;
const uint8_t DISP_PAGE_HIGHS = 2;
const uint8_t DISP_PAGE_LOWS = 3;
const uint8_t DISP_PAGE_GRAPHS = 4;
const boolean FARENHEIGHT = true;

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
// void MQTT_connect();
// void verifyFingerprint();

/*************************** Sketch Code ************************************/

void setup() {
  // Set builtin LED to be used as feedback
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // on for startup
  delay(250);

  // Setup OLED display
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.flipScreenVertically();
  display.clear(); // just to be sure.

  if (SERIAL_ENABLE) {
    Serial.begin(115200);
  }

  // wait for BME 280 to be ready / connect
  if (!bme.begin()) {
    String errorBME = F("Could not find a valid BME280 sensor, check wiring!");
    serialPrint(errorBME);
    display.drawStringMaxWidth(0, 0, 128, errorBME);
    display.display();
    while (1);
  }

  // Connect to WiFi access point
  String connecting = "\n\nConnecting to ";
  connecting.concat(String(WLAN_SSID));
  connecting.concat("\n");
  serialPrint(connecting);
  display.drawString(0, 0, connecting);
  display.display();

  delay(1000);

  //TODO: try 10 times, with ever incereasing delays....
  //TODO: Ensure system doesn't lock up needing to be reset when it can't connect
  // for (int i = 1; i < 101; i += 10) {}
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  delay(2000);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    serialPrint(".");
  }

  String connected = "\nWiFi connected.\n";
  connected.concat("IP address: "); 
  connected.concat(String(WiFi.localIP()));
  connected.concat("\n");
  serialPrint(connected);
  display.clear();
  display.drawStringMaxWidth(0, 0, 127, connected);
  display.display();

  // check the fingerprint of io.adafruit.com's SSL cert
  verifyFingerprint();
  // start UDP for NTP server connection
  udp.begin(localPortUDP);
  sendNTPpacket();

  // Setup intial MQTT Connection to MQTT Broker/Server
  MQTT_connect();

  // Setup Subscriptions:
  if (subscriptions) {
    ////// pick up here.
    mqtt.subscribe(&mqttSubTemp2);
    mqtt.subscribe(&mqttSubHum2);
    mqtt.subscribe(&mqttSubTemp3);
    mqtt.subscribe(&mqttSubHum3);
  }
  

  // initalize storage buffers
  for  (int i = 0; i < 119; i++) {
    tempHistory[i] = 0;
    humidHistory[i] = 0;
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  /**
   * Program flow:
   * 1. Check/update BME-280
   * 1.1. Update Graph value buffer
   * 2. Update MQTT:
   * 2.1. Send Updates - publish debug, temp, humidity
   * 2.2. Get subscriptions - other room data (4 values)
   * 3. Display Update - Data
   * 3.1. Clock
   * 3.1.1. check for update / save
   * 3.2. IP / RSSI
   * 3.3. Main screen values
   * 3.4. Graph
   *
   * Display update DETAIL:
   * - screen switches between main page (clock, RSSI/IP, Current Values)
   * - and the second page (temp/Humidity graph)
   */
  
  currentMillis = millis();

  // -------- UPDATE SENSOR AND DISPLAY ------------
  if ((currentMillis - local_lastMillis) >= LOCAL_RATE) {
    local_lastMillis = currentMillis; // update timer
    debugPrint(F("LOCAL_RATE\n"));
    // Read sensor
    getBME280();
    // Pick up / process any waiting NTP response packets
    readNTP();
    // Update Display
    updateDisplay(currentMillis);
  }

  // ------- PAGE FLIP -------
  if ((currentMillis - page_lastMillis) >= PAGE_FLIP) {
    page_lastMillis = currentMillis; // update timer
    debugPrint(F("PAGE_FLIP"));
    debugPrint(String(disp_page));
    debugPrint(F("\n\n"));
    // Flip Display Page setting -- display will update on next cycle
    if (disp_page == DISP_PAGE_MAIN) {
      disp_page = DISP_PAGE_HIGHS;
    } else if (disp_page == DISP_PAGE_HIGHS) {
      disp_page = DISP_PAGE_LOWS;
    } else if (disp_page == DISP_PAGE_LOWS) {
      disp_page = DISP_PAGE_GRAPHS;
    } else {
      disp_page = DISP_PAGE_MAIN;
    }
  }

  // ------- MQTT ---------
  if ((currentMillis - mqtt_lastMillis) >= MQTT_RATE) {
    mqtt_lastMillis = currentMillis; // update timer
    debugPrint(F("MQTT_RATE\n"));
    updateMqtt();
  }

  // ------- NTP ----------
  if ((currentMillis - ntp_lastMillis) >= NTP_RATE) {
    ntp_lastMillis = currentMillis; // update timer
    debugPrint(F("NTP_RATE\n"));
    // Update time with NTP
    sendNTPpacket();
  }

  // ------- GRAPH --------
  if ((currentMillis - graph_lastMillis) >= GRAPH_SAV_RATE) {
    graph_lastMillis = currentMillis; // update timer
    debugPrint(F("GRAPH_SAV_RATE\n"));

    // Update graph buffers, using the moving pointer to make the arrays FIFO buffers.
    tempHistory[graph_pointer] = constrain(currentTemp, -128, 127);
    humidHistory[graph_pointer] = constrain(currentHumidity, 0, 100);
    if (graph_pointer == HIST_BUF_SIZE - 1) {
      graph_pointer = 0;
    } else {
      graph_pointer++;
    }
    if (graph_storedCount < HIST_BUF_SIZE) {
      graph_storedCount++;
    }
  }

}

void updateDisplay(unsigned long currentMillis) {
  // lastY to store the Y-position of the drawing point, to make layout easier.
  uint8_t lastY = 0;
  debugPrint(F("enter-updateDisplay()\n"));
  display.clear();

  // ALWAYS DISPLAY CLOCK/RSSI/IP
  
  display.setFont(ArialMT_Plain_10);
  // Clock (top left)
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, lastY, getTimeString());
  // RSSI/IP (top Right)
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  if (!disp_RSSI) {
    // debugPrint(F("WiFI IP: "));
    // debugPrint((WiFi.localIP().toString()));
    display.drawString(127, lastY, WiFi.localIP().toString());
    if ((currentMillis - disp_rssiMillis) >= DISP_RSSI_SWITCH_RATE) {
      disp_rssiMillis = currentMillis;
      disp_RSSI = true;
    }
  } else {
    String rssi = F("Signal: ");
    rssi.concat(WiFi.RSSI());
    rssi.concat(F(" db"));
    display.drawString(127, lastY, rssi);
    if ((currentMillis - disp_rssiMillis) >= DISP_RSSI_SWITCH_RATE) {
      disp_rssiMillis = currentMillis;
      disp_RSSI = false;
    }
  }
  display.drawHorizontalLine(0, lastY += 12, 128);
  // PAGE FLIPPING Affects everything displayed below here:
  // 
  // DEBUG: draw a pixel-point ruler
  // for (int i = 0; i < 64; i += 4) {
  //   display.setPixel(0, i);
  // }
  switch (disp_page) {
    case DISP_PAGE_HIGHS:
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_16);
      display.drawString(64, lastY += 2, "Today's Highs");

      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(6, lastY += 16, "Temp (F)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(4, lastY + 9, String(maxTemp, 1));

      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(125, lastY, "Humidity (%)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(120, lastY += 9, String(maxHum, 1));
      break;
    case DISP_PAGE_LOWS:
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_16);
      display.drawString(64, lastY += 2, "Today's Lows");

      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(6, lastY += 16, "Temp (F)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(4, lastY + 9, String(minTemp, 1));

      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(125, lastY, "Humidity (%)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(120, lastY += 9, String(minHum, 1));

      break;
    case DISP_PAGE_GRAPHS:
      {
        debugPrint(F("DISP_PAGE_GRAPHS\n"));
        display.drawVerticalLine(3, lastY += 4, 21);
        display.drawHorizontalLine(4, lastY += 20, 120);
        display.drawVerticalLine(3, lastY += 4, 21);
        display.drawHorizontalLine(4, lastY += 20, 120);

        // Display the data from the buffers on to the charts.
        uint8_t xpos = 123;
        for (int i = 0; i < graph_storedCount; i++) {
          int valIndex = int(graph_pointer) - (i+1);
          if (valIndex < 0) {
            valIndex += graph_storedCount;
          }
          uint8_t tVal = map(tempHistory[valIndex], minTemp-1, maxTemp+1, 0, 20);
          uint8_t hVal = map(humidHistory[valIndex], 0, 100, 0, 20);

          display.setPixel(xpos-i, lastY-24-tVal);
          display.setPixel(xpos-i, lastY-hVal);
        }

        break;
      }
    default: // main
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(6, lastY += 2, "Temp (F)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(4, lastY + 8, String(currentTemp, 1));

      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(125, lastY, "Humidity (%)");
      display.setFont(ArialMT_Plain_24);
      display.drawString(120, lastY += 8, String(currentHumidity, 1));

      display.drawHorizontalLine(0, lastY += 24, 128);

      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(63, lastY += 2, "2:32/50  3:32/50");
      break;
  }
  // finally, draw the screen.
  display.display();
}

/**
 * Ensure the connection to the MQTT server is alive, reconnecting if disconnected.
 * then publishes data to the MQTT Server
 */
void updateMqtt() {
  debugPrint(F("enter-updateMqtt()\n"));
  MQTT_connect();
  digitalWrite(LED_BUILTIN, LOW);
  if (!mqttFeedTemp.publish(currentTemp)) {
    serialPrint(F("Temp publish failed.\n"));
  }
  if (!mqttFeedHumidity.publish(currentHumidity)) {
    serialPrint(F("Humidity publish failed.\n"));
  }
  if (!mqttFeedPressure.publish(currentPressure)) {
    serialPrint(F("Pressure publish failed.\n"));
  }
  String mqttDebug = F("Unit ");
  mqttDebug.concat(UNIT_ID);
  mqttDebug.concat(F(" data: T:"));
  mqttDebug.concat(currentTemp);
  mqttDebug.concat(F(" H:"));
  mqttDebug.concat(currentHumidity);
  mqttDebug.concat(F(" P:"));
  mqttDebug.concat(currentPressure);
  mqttDebug.concat(F(" - millis:"));
  mqttDebug.concat(currentMillis);
  mqttDebug.concat(F(" - time:"));
  mqttDebug.concat(getTimeString());
  if (!mqttFeedDebug.publish(mqttDebug.c_str())) {
    serialPrint(F("MQTT debug string publish failed"));
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

void getBME280() {
  debugPrint(F("enter-getBME280()\n"));
  currentPressure = bme.readPressure() / 100.0F;
  currentTemp = bme.readTemperature();
  if (FARENHEIGHT) {
    currentTemp = currentTemp * 1.8 + 32;
  }
  currentHumidity = bme.readHumidity();
  // do max/min storage:
  if (currentTemp < minTemp) {
    minTemp = currentTemp;
  }
  if (currentTemp > maxTemp) {
    maxTemp = currentTemp;
  }
  if (currentHumidity < minHum) {
    minHum = currentHumidity;
  }
  if (currentHumidity > maxHum) {
    maxHum = currentHumidity;
  }
}

/**
 * Resets the min/max values (should be called daily (around midnight))
 */
void initMinMaxTemp() {
  minTemp = 500;
  maxTemp = -500;
  minHum = 200;
  maxHum = -200;
}

/**
 * Function verifies that the security fingetprint of Adafruit's MQTT server 
 * is the same as is expected.
 */
void verifyFingerprint() {
  const char* host = AIO_SERVER;
  serialPrint("Connecting to " + String(host) + "\n");
  if (! client.connect(host, AIO_SERVERPORT)) {
    serialPrint(F("Connection failed. Halting execution."));
    while(1);
  }

  if (client.verify(fingerprint, host)) {
    serialPrint(F("Connection secure."));
  } else {
    serialPrint(F("Connection insecure! Halting execution."));
    while(1);
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  serialPrint(F("Connecting to MQTT... "));

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    serialPrint(mqtt.connectErrorString(ret));
    serialPrint(F("Retrying MQTT connection in 5 seconds...\n"));
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      // basically die and wait for WDT to reset me
      while (1);
    }
  }

  serialPrint(F("MQTT Connected!"));
}

void readNTP() {
  debugPrint(F("enter-readNTP()\n"));
  if(udp.parsePacket()) {
    digitalWrite(LED_BUILTIN, LOW);
    debugPrint(F("udp.parsePacket\n"));
    udp.read(packetBuffer, NTP_PACKET_SIZE);

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    // serialPrint(F("Seconds since Jan 1 1900 = "));
    // serialPrint(String(secsSince1900));
    // serialPrint("\n");


    // // now convert NTP time into everyday time:
    // serialPrint("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    // const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years: // just put int the number, not 'seventyYears'
    unsigned long epoch = secsSince1900 - 2208988800UL;
    // print Unix time:
    // serialPrint(String(epoch));
    // serialPrint("\n");

    // store the NTP epoch to the global epoch and the time that it was gathered.
    globalEpoch = epoch;

    // print the hour, minute and second:
    serialPrint(F("The UTC time is "));       // UTC is the time at Greenwich Meridian (GMT)
    serialPrint(getTimeString());
    serialPrint("\n");
    digitalWrite(LED_BUILTIN, HIGH);

    //ALSO! Reset the day's High/Low cache around midnight-ish.
    if (epoch % 86400UL < 300) {
      initMinMaxTemp();
    }
  }
}

/**
 * use to get a string representation of the current time.
 * Uses global variables to retrieve the time that was last retrieved from the NTP server,
 * plus the time elapsed since then.
 * Adds an offset (see global settings) in seconds to compensate for time zones.
 * @return A String object representing the current time in HH:MM:SS format, 24 hour clock.
 */
String getTimeString() {
  // get a local epoch time, plus seconds since update last NTP update.
  unsigned long epoch = globalEpoch + ((millis() - millisSinceNTP) / 1000);
  epoch = epoch + NTP_OFFSET;
  String timeString = String((epoch % 86400L) / 3600); // hour (86400 equals secs per day)
  timeString.concat(":");
  if (((epoch % 3600) / 60) < 10) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    timeString.concat("0");
  }
  timeString.concat((epoch  % 3600) / 60); // append the minute (3600 equals secs per minute)
  timeString.concat(":");
  if ((epoch % 60) < 10) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    timeString.concat("0");
  }
  timeString.concat(epoch % 60); // append the second
  return timeString;
}


/**
 * Sends an NTP request packet to the time server 
 * @return         [description]
 */
void sendNTPpacket() {
  digitalWrite(LED_BUILTIN, LOW);
  debugPrint(F("enter-sendNTPpacket()..."));
  //get a random server from the pool
  // WiFiUDP.hostByName(ntpServerName, timeServerIP);
  serialPrint(F("sending NTP packet...\n"));
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(ntpServerName, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  serialPrint(F("NTP Sent\n"));

  digitalWrite(LED_BUILTIN, HIGH);
  // Store the time that the packet was sent, use for clock offset upon return.
  millisSinceNTP = millis();
}

void debugPrint(String msg) {
  if(DEBUG) {
    serialPrint(msg);
  }
}

void serialPrint(String msg) {
  if (SERIAL_ENABLE) {
    Serial.print(msg);
  }
}
