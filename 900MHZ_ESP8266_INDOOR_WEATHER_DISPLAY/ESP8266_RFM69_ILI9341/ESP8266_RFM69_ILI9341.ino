#include <RFM69.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>

#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "password.h"

// For the Adafruit shield, these are the default.
#define TFT_DC 5
#define TFT_CS 2

#define TFT_DIM_PIN 16

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);



//*********************************************************************************************
// *********** IMPORTANT SETTINGS - YOU MUST CHANGE/CONFIGURE TO FIT YOUR HARDWARE *************
//*********************************************************************************************
int16_t NETWORKID = 100;  //the same on all nodes that talk to each other
int16_t NODEID = 1;


int nextTime = 0;

//Match frequency to the hardware version of the radio on your Feather
//#define FREQUENCY     RF69_433MHZ
//#define FREQUENCY     RF69_868MHZ
#define FREQUENCY      RF69_915MHZ
#define ENCRYPTKEY     "tacoTacoBurrito!" //exactly the same 16 characters/bytes on all nodes!
#define IS_RFM69HCW    true // set to 'true' if you are using an RFM69HCW module
#define promiscuousMode true
//*********************************************************************************************

#define RFM69_CS      15
#define RFM69_IRQ     4
#define RFM69_IRQN    4 //digitalPinToInterrupt(RFM69_IRQ)
//#define RFM69_RST     16

int16_t packetnum = 0;  // packet counter, we increment per xmission

RFM69 radio = RFM69(RFM69_CS, RFM69_IRQ, IS_RFM69HCW, RFM69_IRQN);  //initialize radio with potential custom pin outs; otherwise you may use for default: RFM69 radio;


// Data Structure
typedef struct {
  float         soiltempf;
  float         humidity;
  float         inches;
  int           winddir;

  float         windspeedmph;
  int           windgustdir;
  float         windgustmph;
  float         rainin;

  float         dewptF;
  float         windspdmph_avg2m;
  int           winddir_avg2m;
  float         windgustmph_10m;

  int           windgustdir_10m;
  float         dailyrainin;
} Payload;
Payload theData;



//UI
bool dataReceived = false;
int timer = 0;
int brightness = 1023;



// Button(s)
#define buttonOnePin 0

int buttonOneState;
int buttonOnePrevState;
int buttonOnePushedTime;

#define buttonTwoPin A0

int buttonTwoState;
int buttonTwoPrevState;
int buttonTwoPushedTime;

//WIFI
const char *ssid     = WIFISSID;
const char *password = WIFIPASSWORD;

// NTP Servers:
//static const char ntpServerName[] = "us.pool.ntp.org";
static const char ntpServerName[] = "time.nist.gov";


const int timeZone = 0; // UTC

TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);



WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

void setup()
{
wdt_disable();
    Serial.begin(115200); // Open serial monitor at 115200 baud to see ping results.
    Serial.println("RFM69 Based Receiver");

 /*   // Hard Reset the RFM module - Optional
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, HIGH);
  delay(100);
  digitalWrite(RFM69_RST, LOW);
  delay(100);*/

    // Initialize radio
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  if (IS_RFM69HCW) {
    radio.setHighPower();    // Only for RFM69HCW & HW!
  }

//  radio.promiscuous(promiscuousMode);

  // To improve distance set a lower bit rate. Most libraries use 55.55 kbps as default
  // See https://lowpowerlab.com/forum/moteino/rfm69hw-bit-rate-settings/msg1979/#msg1979
  // Here we will set it to 9.6 kbps instead
  radio.writeReg(0x03,0x0D); //set bit rate to 9k6
  radio.writeReg(0x04,0x05);

  radio.setPowerLevel(2); // power output ranges from 0 (5dBm) to 31 (20dBm)
                          // Note at 20dBm the radio sources up to 130 mA!
                         // Selecting a power level between 10 and 15 will use ~30-44 mA which is generally more compatible with Photon power sources
                        // As reference, power level of 10 transmits successfully at least 300 feet with 0% packet loss right through a home, sufficient for most use

  radio.encrypt(ENCRYPTKEY);

  Serial.print("\nListening at ");
  Serial.print(FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  Serial.println(" MHz");
  tft.begin();
  tft.setRotation(3);

  
  analogWrite(TFT_DIM_PIN, brightness);
 /*pinMode(TFT_DIM_PIN, OUTPUT);
  digitalWrite(TFT_DIM_PIN, LOW);*/
 
  pinMode(buttonOnePin, INPUT);
  pinMode(buttonTwoPin, INPUT);


WiFi.begin(ssid, password);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }


  Udp.begin(localPort);
  Serial.print("Local UDP port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(7200);
 
}

int ntpUpdateTimer;

//=========================MAIN LOOP===========================================
void loop() {

  if (timeNotSet && ntpUpdateTimer < millis()) {
   Serial.println("Time not set!");
   getNtpTime();
   ntpUpdateTimer = millis() + 10000;
  }
 
  if(millis() > timer)
  {
    timer = millis() + 1000;     
  }

  buttonOneState = digitalRead(buttonOnePin);
      
  buttonTwoState = analogRead(buttonTwoPin)/1024;

 
  if (buttonOneState == HIGH) {
    if(buttonOnePrevState < millis() && brightness < 1020)
    {
      Serial.println("Button 1 Pushed");
      buttonOnePrevState = millis() + 1000;
      brightness = brightness + 100;
      Serial.print("Brightness: ");
      Serial.println(brightness);
      analogWrite(TFT_DIM_PIN, brightness);
    } else if (brightness >= 1020) 
    {
      Serial.print("Brightness already at max.");
    }
  }
  if (buttonTwoState == 1) {
    if(buttonTwoPrevState < millis() && brightness > 10)
    {
      Serial.println("Button 2 Pushed");
      buttonTwoPrevState = millis() + 1000;
      brightness = brightness - 100;
      Serial.print("Brightness: ");
      Serial.println(brightness);
      analogWrite(TFT_DIM_PIN, brightness);
    } else if (brightness <= 10) 
    {
      Serial.print("Brightness already at min.");
    }
  }
  
if (!dataReceived)
  {
   tft.fillScreen(ILI9341_BLACK);
   tft.setTextSize(3);
   tft.setCursor(0,0);
   tft.setTextColor(ILI9341_RED);
   tft.println("OUTSIDE UNIT ON?");
   tft.println("WAITING FOR DATA");
   tft.setTextColor(ILI9341_WHITE);
   dataReceived = true;
  }
  

if (radio.receiveDone())
  {
    Serial.println();
    Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
    Serial.print(" [RX_RSSI:");Serial.print(radio.readRSSI());Serial.println("]:");


    if (radio.DATALEN != sizeof(Payload))
      Serial.println("Invalid payload received, not matching Payload struct!");
    else
    {
      tft.fillScreen(ILI9341_BLACK);
      theData = *(Payload*)radio.DATA; //assume radio.DATA actually contains our struct and not something else












float Windchill = 35.74 + 0.6215*theData.soiltempf - 35.75*pow(theData.windspeedmph,0.16) + 0.4275*theData.soiltempf*pow(theData.windspeedmph,0.16);



  tft.setTextSize(2);
  tft.setCursor(0,226);


  time_t utc = now();    //current time from the Time Library
  time_t eastern = usET.toLocal(utc);
  
  char dateTimeStamp [80];
  String dayAbbreviation;
  switch (weekday(eastern)) {
    case 1:
      dayAbbreviation="SUN";
      break;
    case 2:
      dayAbbreviation="MON";
      break;
    case 3:
      dayAbbreviation="TUE";
      break;
    case 4:
      dayAbbreviation="WED";
      break;
    case 5:
      dayAbbreviation="THU";
      break;
    case 6:
      dayAbbreviation="FRI";
      break;
    case 7:
      dayAbbreviation="SAT";
      break;
      
  }

  String amOrPm;
  if(isAM(eastern)){
    amOrPm="A";
  } else {
    amOrPm="P";
  }
  
  sprintf(dateTimeStamp, "(%s) %02d/%02d/%02d %02d:%02d:%02d%s",
   dayAbbreviation.c_str(), month(eastern), day(eastern), year(eastern), hourFormat12(eastern), minute(eastern), second(eastern), amOrPm.c_str());
  
  tft.print(dateTimeStamp);
  


  tft.setTextSize(2);
  tft.setCursor(0,0);
  tft.println("Feels Like:   Temperature:");

  tft.setCursor(0,20);
  tft.setTextSize(6);



  if(round(Windchill) <= 32) {
    tft.setTextColor(ILI9341_BLUE);
    tft.print(round(Windchill));
  } else if (round(Windchill) > 32 && round(Windchill) <= 88) {
    tft.setTextColor(ILI9341_WHITE);
    tft.print(round(Windchill));
  } else if (round(Windchill) > 88) {
    tft.setTextColor(ILI9341_RED);
    tft.print(round(Windchill));
  } else {
    tft.setTextColor(ILI9341_YELLOW);
    tft.print("err");
  }



  
  
  
  
  tft.setTextSize(2);
  tft.println((char)247);
  

  

  tft.setTextSize(8);
  
  switch (String(round(theData.soiltempf)).length()) {
    case 1:
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(218,20);
      tft.print(round(theData.soiltempf));
      tft.setTextSize(3);
      tft.println((char)247);
      break;
    case 2:
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(183,20);
      tft.print(round(theData.soiltempf));
      tft.setTextSize(3);
      tft.println((char)247);
      break;
    case 3:
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(157,20);
      tft.print(round(theData.soiltempf));
      tft.setTextSize(3);
      tft.println((char)247);
      
      break;
    default:
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(0,0);
      tft.print("err");
    break;
  }
/////////////////////////////////
//                 left top
//      tft.setCursor(0,0);
/////////////////////////////////
  tft.setCursor(185,89);
  tft.setTextSize(2);
  tft.print("Rain (24h):");
  tft.setCursor(183,110);
  tft.setTextSize(3);
  if(theData.dailyrainin >= 10) {
    theData.dailyrainin=theData.dailyrainin+.05;
    tft.print(theData.dailyrainin, 1);
  } else {
    theData.dailyrainin=theData.dailyrainin+.005;
    tft.print(theData.dailyrainin, 2);
  }
  tft.print("in.");

  tft.setCursor(0,80);
  tft.setTextSize(2);
  tft.print("Wind Speed:");
  tft.setTextSize(4);
  tft.setCursor(0,100);
  if(theData.windspeedmph >= 100) {
    tft.print(round(theData.windspeedmph));
  } else if (theData.windspeedmph >= 10) {
    theData.windspeedmph=theData.windspeedmph+.05;
    tft.print(theData.windspeedmph, 1);
  } else {
    theData.windspeedmph=theData.windspeedmph+.005;
    tft.print(theData.windspeedmph, 2);
  }
  tft.print("mph");

  
  
  
  
  
  tft.setCursor(0,140);
  tft.setTextSize(2);
  tft.print("W Dir:");
  tft.setTextSize(4);
  
  
  switch (theData.winddir)
  {
    case 0:
    tft.setCursor(20,160);
      tft.print("N");
      break;
    case 45:
    tft.setCursor(5,160);
      tft.print("NE");
      break;
    case 90:
    tft.setCursor(20,160);
      tft.print("E");
      break;
    case 135:
    tft.setCursor(5,160);
      tft.print("SE");
      break;
    case 180:
    tft.setCursor(20,160);
      tft.print("S");
      break;
    case 225:
    tft.setCursor(5,160);
      tft.print("SW");
      break;
    case 270:
    tft.setCursor(20,160);
      tft.print("W");
      break;
    case 315:
    tft.setCursor(0,160);
      tft.print("NW");
      break;
    default:
    tft.setCursor(0,160);
      tft.print("eR");
      // if nothing else matches, do the
      // default (which is optional)
  }
  
  tft.setCursor(90,140);
  tft.setTextSize(2);
  tft.print("Forcast:");
  tft.setCursor(90,165);
  tft.setTextSize(3);
  tft.print("H:");
//  tft.print(round(fHgh));
  tft.setTextSize(2);
  tft.print((char)247);
  tft.setTextSize(3);
  tft.print(" L:");
//  tft.print(round(fLow));
  tft.setTextSize(2);
  tft.println((char)247);
  tft.setCursor(90,190);
  tft.print("Rain Today");

  //tft.setCursor(0,207);
  //tft.print("Some extra text goes here!"); 



























 /*     Serial.print("soiltempf: ");
      Serial.println(theData.soiltempf);

      Serial.print("humidity: ");
      Serial.println(theData.humidity);

      Serial.print("inches: ");
      Serial.println(theData.inches);

      Serial.print("winddir: ");
      Serial.println(theData.winddir);

      Serial.print("windspeedmph: ");
      Serial.println(theData.windspeedmph);

      Serial.print("windgustdir: ");
      Serial.println(theData.windgustdir);

      Serial.print("windgustmph: ");
      Serial.println(theData.windgustmph);

      Serial.print("rainin: ");
      Serial.println(theData.rainin);

      Serial.print("dewptF: ");
      Serial.println(theData.dewptF);

      tft.setCursor(0, 0);
      tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(3);
      tft.print("DewPtF: ");
      tft.println(String(theData.dewptF));

      Serial.print("windspdmph_avg2m: ");
      Serial.println(theData.windspdmph_avg2m);

      Serial.print("winddir_avg2m: ");
      Serial.println(theData.winddir_avg2m);

      Serial.print("windgustmph_10m: ");
      Serial.println(theData.windgustmph_10m);

      Serial.print("windgustdir_10m: ");
      Serial.println(theData.windgustdir_10m);

      Serial.print("dailyrainin: ");
      Serial.println(theData.dailyrainin);
*/
    }
  }

  radio.receiveDone(); //put radio in RX mode

  if (nextTime > millis() && nextTime !=0) {
      return;
  }
  nextTime = millis() + 1000;
  Serial.print("."); //THis gives us a neat visual indication of time between messages received

} //end loop




/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
