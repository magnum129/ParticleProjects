// This #include statement was automatically added by the Particle IDE.
#include "RFM69-Particle/RFM69-Particle.h"

SYSTEM_MODE(MANUAL);

// Adjustments to library to work with Particle Photon including in Web IDE by Jurie Pieterse
// Forked library for Photon at https://github.com/bloukingfisher/RFM69/
// Serial NOT required to confirm working - you can watch your Particle Console logs!

/* RFM69 library and code by Felix Rusu - felix@lowpowerlab.com
// Get libraries at: https://github.com/LowPowerLab/
// Make sure you adjust the settings in the configuration section below !!!
// **********************************************************************************
// Copyright Felix Rusu, LowPowerLab.com
// Library and code by Felix Rusu - felix@lowpowerlab.com
// **********************************************************************************
// License
// **********************************************************************************
// This program is free software; you can redistribute it
// and/or modify it under the terms of the GNU General
// Public License as published by the Free Software
// Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General
// Public License along with this program.
// If not, see <http://www.gnu.org/licenses></http:>.
//
// Licence can be viewed at
// http://www.gnu.org/licenses/gpl-3.0.txt
//
// Please maintain this license information along with authorship
// and copyright notices in any redistribution of this code
// **********************************************************************************/


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

//*********************************************************************************************

#define RFM69_CS      A2
#define RFM69_IRQ     D5
#define RFM69_IRQN    D5 //On Photon it is the same unlike Arduino
#define RFM69_RST     D6

int16_t packetnum = 0;  // packet counter, we increment per xmission

RFM69 radio = RFM69(RFM69_CS, RFM69_IRQ, IS_RFM69HCW, RFM69_IRQN);  //initialize radio with potential custom pin outs; otherwise you may use for default: RFM69 radio;

//*********************************************************************************************
// ***********                  Wiring the RFM69 Radio to Photon                  *************
//*********************************************************************************************
/* Arduino wiring provided for reference, color
    Photon      Arduino	    RFM69	Color
    GND         GND	        GND	    Black
    3V3         3.3V	    VCC	    Red
    A2          10	        NSS	    Yellow
    A3          13	        SCK	    Green
    A5          11	        MOSI	Blue
    A4          12	        MISO	Violet
    D2          2	        DI00	Gray
                            ANA	    Antenna
    D6                      RST     Optional
*/

void setup()
{

    Serial.begin(115200); // Open serial monitor at 115200 baud to see ping results.
//    Particle.publish("RFM69 RX Startup setup","Completed",360,PRIVATE);
//    Particle.publish("WiFi signal",String(WiFi.RSSI()),360,PRIVATE);
    Serial.println("RFM69 Based Receiver");

    // Hard Reset the RFM module - Optional
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, HIGH);
  delay(100);
  digitalWrite(RFM69_RST, LOW);
  delay(100);

    // Initialize radio
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  if (IS_RFM69HCW) {
    radio.setHighPower();    // Only for RFM69HCW & HW!
  }

  // To improve distance set a lower bit rate. Most libraries use 55.55 kbps as default
  // See https://lowpowerlab.com/forum/moteino/rfm69hw-bit-rate-settings/msg1979/#msg1979
  // Here we will set it to 9.6 kbps instead
  radio.writeReg(0x03,0x0D); //set bit rate to 9k6
  radio.writeReg(0x04,0x05);

  radio.setPowerLevel(10); // power output ranges from 0 (5dBm) to 31 (20dBm)
                          // Note at 20dBm the radio sources up to 130 mA!
                         // Selecting a power level between 10 and 15 will use ~30-44 mA which is generally more compatible with Photon power sources
                        // As reference, power level of 10 transmits successfully at least 300 feet with 0% packet loss right through a home, sufficient for most use

  radio.encrypt(ENCRYPTKEY);

  Serial.print("\nListening at ");
  Serial.print(FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  Serial.println(" MHz");
}


//=========================MAIN LOOP===========================================
void loop() {

if (radio.receiveDone())
  {
    Serial.println();
    Serial.print('[');Serial.print(radio.SENDERID, DEC);Serial.print("] ");
    Serial.print(" [RX_RSSI:");Serial.print(radio.readRSSI());Serial.println("]:");


    if (radio.DATALEN != sizeof(Payload))
      Serial.println("Invalid payload received, not matching Payload struct!");
    else
    {
      theData = *(Payload*)radio.DATA; //assume radio.DATA actually contains our struct and not something else


      Serial.print("soiltempf: ");
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

    }
  }

  radio.receiveDone(); //put radio in RX mode

  if (nextTime > millis() && nextTime !=0) {
      return;
  }
  nextTime = millis() + 1000;
  Serial.print("."); //THis gives us a neat visual indication of time between messages received

} //end loop
