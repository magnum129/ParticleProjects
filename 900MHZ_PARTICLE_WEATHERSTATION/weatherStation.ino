#include "spark-dallas-temperature/spark-dallas-temperature.h"
#include "OneWire/OneWire.h"
#include "SparkFun_Photon_Weather_Shield_Library/SparkFun_Photon_Weather_Shield_Library.h"
#include "RFM69-Particle/RFM69-Particle.h"
#include "math.h"

SYSTEM_MODE(MANUAL);

//change this all you want!
const int submitEverySeconds = 5;
#define RADIOPOWERLEVEL 3

//Debug
bool debug = true;


//915Mhz Stuff
int16_t NETWORKID = 100;  //the same on all nodes that talk to each other
int16_t NODEID = 2;       //make sure NODEID differs between nodes
int16_t RECEIVER = 1;     //receiver's node ID
#define FREQUENCY      RF69_915MHZ
#define ENCRYPTKEY     "tacoTacoBurrito!" //exactly the same 16 characters/bytes on all nodes!
#define IS_RFM69HCW    true // set to 'true' if you are using an RFM69HCW module
#define RFM69_CS      A2
#define RFM69_IRQ     D5
#define RFM69_IRQN    D5 //On Photon it is the same unlike Arduino
#define RFM69_RST     D6
RFM69 radio = RFM69(RFM69_CS, RFM69_IRQ, IS_RFM69HCW, RFM69_IRQN);



//Soil Temp Probe, that I'm using as air temp
#define ONE_WIRE_BUS D4
#define TEMPERATURE_PRECISION 11
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress inSoilThermometer =
{0x28, 0x44, 0xAE, 0x0, 0x8, 0x0, 0x0, 0x3};//Waterproof temp sensor address

//Create Instance of HTU21D or SI7021 temp and humidity sensor and MPL3115A2 barometric sensor
Weather sensor;
void update18B20Temp(DeviceAddress deviceAddress, double &tempC);//predeclare to compile


//Pins for analog sensors
int WDIR = A0;
int RAIN = D2;
int WSPEED = D3;


//Global Weather Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond; //The millis counter to see when a second rolls by
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data

//We need to keep track of the following variables:
//Wind speed/dir each update (no storage)
//Wind gust/dir over the day (no storage)
//Wind speed/dir, avg over 2 minutes (store 1 per second)
//Wind gust/dir over last 10 minutes (store 1 per minute)
//Rain over the past hour (store 1 per minute)
//Total rain over date (store one per day)

byte windspdavg[120]; //120 bytes to keep track of 2 minute average
int winddiravg[120]; //120 ints to keep track of 2 minute average
float windgust_10m[10]; //10 floats to keep track of 10 minute max
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
int winddir = 0; // [0-360 instantaneous wind direction]
float windspeedmph = 0; // [mph instantaneous wind speed]
float windgustmph = 0; // [mph current wind gust, using software specific time period]
int windgustdir = 0; // [0-360 using software specific time period]
float windspdmph_avg2m = 0; // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0; // [0-360 2 minute average wind direction]
float windgustmph_10m = 0; // [mph past 10 minutes wind gust mph ]
int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]
float rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
long lastWindCheck = 0;
volatile float dailyrainin = 0; // [rain inches so far today in local time]
float humidity = 0;
float humTempF = 0;  //humidity sensor temp reading, fahrenheit
float humTempC = 0;  //humidity sensor temp reading, celsius
float baroTempF = 0; //barometer sensor temp reading, fahrenheit
float baroTempC = 0; //barometer sensor temp reading, celsius
float tempF = 0;     //Average of the sensors temperature readings, fahrenheit
float tempC = 0;     //Average of the sensors temperature readings, celsius
float dewptF = 0;
float dewptC = 0;
float pascals = 0;
float inches = 0;
double InTempC = 0;//original temperature in C from DS18B20
float soiltempf = 0;//converted temperature in F from DS18B20


// volatiles are subject to modification by IRQs
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;
volatile unsigned long raintime, rainlast, raininterval, rain;

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

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

unsigned int nextTime = 0;

//Poll weather data every second.
Timer get_weather_timer(1000, getWeather);

String data;

void setup() {
    Serial.begin(115200);
    //Dim the LED
    RGB.brightness(10);

    // Hard Reset the RFM module - Optional
    pinMode(RFM69_RST, OUTPUT);
    digitalWrite(RFM69_RST, HIGH);
    delay(100);
    digitalWrite(RFM69_RST, LOW);
    delay(100);

    radio.initialize(FREQUENCY,NODEID,NETWORKID);
    if (IS_RFM69HCW) {
      radio.setHighPower();    // Only for RFM69HCW & HW!
    }
    // To improve distance set a lower bit rate. Most libraries use 55.55 kbps as default
    // See https://lowpowerlab.com/forum/moteino/rfm69hw-bit-rate-settings/msg1979/#msg1979
    // Here we will set it to 9.6 kbps instead
    radio.writeReg(0x03,0x0D); //set bit rate to 9k6
    radio.writeReg(0x04,0x05);

    radio.setPowerLevel(RADIOPOWERLEVEL); // power output ranges from 0 (5dBm) to 31 (20dBm)
                        // Note at 20dBm the radio sources up to 130 mA!
                       // Selecting a power level between 10 and 15 will use ~30-44 mA which is generally more compatible with Photon power sources
                      // As reference, power level of 10 transmits successfully at least 300 feet with 0% packet loss right through a home, sufficient for most use

    radio.encrypt(ENCRYPTKEY);


    //DS18B20 external temp
    sensors.begin();
    sensors.setResolution(inSoilThermometer, TEMPERATURE_PRECISION);
    //Initialize the I2C sensors and ping them
    sensor.begin();
    //Analog nput from wind meters windspeed sensor
    pinMode(WSPEED, INPUT_PULLUP);
    //Analog input from wind meters rain gauge sensor
    pinMode(RAIN, INPUT_PULLUP);
    //Set to Barometer Mode
    sensor.setModeBarometer();
//    baro.setModeAltimeter();
    //These are additional MPL3115A2 functions the MUST be called for the sensor to work.
    //Call with a rate from 0 to 7. See page 33 for table of ratios.
    //Sets the over sample rate. Datasheet calls for 128 but you can set it
    //from 1 to 128 samples. The higher the oversample rate the greater
    //the time between data samples.
    sensor.setOversampleRate(7); // Set Oversample rate
    sensor.enableEventFlags(); //Necessary register calls to enble temp, baro ansd alt
    //Attach external interrupt pins to IRQ functions
    attachInterrupt(RAIN, rainIRQ, FALLING);
    attachInterrupt(WSPEED, wspeedIRQ, FALLING);
    //Turn on interrupts
    interrupts();

    //Have to start this after everything else is configured!
    get_weather_timer.start();

}

void loop() {
    // Restart once every 24 hours...
    if (nextTime > millis() && nextTime !=0) {
        return;
    }
    submitWeather();
    nextTime = millis() + (submitEverySeconds * 1000); //WiFi is already connected, hopefully...
}

void debugOutLn(String output)
{
  if(debug){
    Serial.println(output);
  }
}
void debugOut(String output)
{
  if(debug){
    Serial.print(output);
  }
}

void submitWeather()
{

    theData.soiltempf = soiltempf;
    theData.humidity = humidity;
    theData.inches = inches;
    theData.winddir = winddir;
    theData.windspeedmph = windspeedmph;
    theData.windgustdir = windgustdir;
    theData.windgustmph = windgustmph;
    theData.rainin = rainin;
    theData.dewptF = dewptF;
    theData.windspdmph_avg2m = windspdmph_avg2m;
    theData.winddir_avg2m = winddir_avg2m;
    theData.windgustmph_10m = windgustmph_10m;
    theData.windgustdir_10m = windgustdir_10m;
    theData.dailyrainin = dailyrainin;
    debugOut("Sending ");
    debugOut(String(sizeof(theData)));
    debugOutLn(" bytes.");
    radio.send(RECEIVER, (const void*)(&theData), sizeof(theData));
    radio.receiveDone(); //put radio in RX mode
    radio.sleep();
}

//---------------------------------------------------------------
void getWeather()
{
  // Measure Relative Humidity from the HTU21D or Si7021
  humidity = sensor.getRH();

  // Measure Temperature from the HTU21D or Si7021
  humTempC = sensor.getTemp();
  humTempF = (humTempC * 9)/5 + 32;
  // Temperature is measured every time RH is requested.
  // It is faster, therefore, to read it from previous RH
  // measurement with getTemp() instead with readTemp()

  getSoilTemp();//Read the DS18B20 waterproof temp sensor

  //Measure the Barometer temperature in F from the MPL3115A2
  baroTempC = sensor.readBaroTemp();
  baroTempF = (baroTempC * 9)/5 + 32; //convert the temperature to F

  //Measure Pressure from the MPL3115A2
  pascals = sensor.readPressure();
  inches = pascals * 0.0002953; // Calc for converting Pa to inHg (Wunderground expects inHg)

  //If in altitude mode, you can get a reading in feet with this line:
  //float altf = sensor.readAltitudeFt();

  //Average the temperature reading from both sensors
  tempC=((humTempC+baroTempC)/2);
  tempF=((humTempF+baroTempF)/2);

  //Calculate Dew Point
  dewptC = dewPoint(tempC, humidity);
  dewptF = (dewptC * 9.0)/ 5.0 + 32.0;

  //Calc winddir
  winddir = get_wind_direction();

  //Calc windspeed
  windspeedmph = get_wind_speed();

  //Calc windgustmph
  //Calc windgustdir
  //Report the largest windgust today
  windgustmph = 0;
  windgustdir = 0;

  //Calc windspdmph_avg2m
  float temp = 0;
  for(int i = 0 ; i < 120 ; i++)
    temp += windspdavg[i];
  temp /= 120.0;
  windspdmph_avg2m = temp;

  //Calc winddir_avg2m
  temp = 0; //Can't use winddir_avg2m because it's an int
  for(int i = 0 ; i < 120 ; i++)
    temp += winddiravg[i];
  temp /= 120;
  winddir_avg2m = temp;

  //Calc windgustmph_10m
  //Calc windgustdir_10m
  //Find the largest windgust in the last 10 minutes
  windgustmph_10m = 0;
  windgustdir_10m = 0;
  //Step through the 10 minutes
  for(int i = 0; i < 10 ; i++)
  {
    if(windgust_10m[i] > windgustmph_10m)
    {
      windgustmph_10m = windgust_10m[i];
      windgustdir_10m = windgustdirection_10m[i];
    }
  }

  //Total rainfall for the day is calculated within the interrupt
  //Calculate amount of rainfall for the last 60 minutes
  rainin = 0;
  for(int i = 0 ; i < 60 ; i++)
    rainin += rainHour[i];


    //Take a speed and direction reading every second for 2 minute average
    if(++seconds_2m > 119) seconds_2m = 0;

    //Calc the wind speed and direction every second for 120 second to get 2 minute average
    float currentSpeed = windspeedmph;
    //float currentSpeed = random(5); //For testing
    int currentDirection = winddir;
    windspdavg[seconds_2m] = (int)currentSpeed;
    winddiravg[seconds_2m] = currentDirection;
    //if(seconds_2m % 10 == 0) displayArrays(); //For testing

    //Check to see if this is a gust for the minute
    if(currentSpeed > windgust_10m[minutes_10m])
    {
      windgust_10m[minutes_10m] = currentSpeed;
      windgustdirection_10m[minutes_10m] = currentDirection;
    }

    //Check to see if this is a gust for the day
    if(currentSpeed > windgustmph)
    {
      windgustmph = currentSpeed;
      windgustdir = currentDirection;
    }

    if(++seconds > 59)
    {
      seconds = 0;

      if(++minutes > 59) minutes = 0;
      if(++minutes_10m > 9) minutes_10m = 0;

      rainHour[minutes] = 0; //Zero out this minute's rainfall amount
      windgust_10m[minutes_10m] = 0; //Zero out this minute's gust
    }

  printInfo();
}

//---------------------------------------------------------------
// dewPoint function from NOAA
// reference (1) : http://wahiduddin.net/calc/density_algorithms.htm
// reference (2) : http://www.colorado.edu/geography/weather_station/Geog_site/about.htm
//---------------------------------------------------------------
double dewPoint(double celsius, double humidity)
{
	// (1) Saturation Vapor Pressure = ESGG(T)
	double RATIO = 373.15 / (273.15 + celsius);
	double RHS = -7.90298 * (RATIO - 1);
	RHS += 5.02808 * log10(RATIO);
	RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1/RATIO ))) - 1) ;
	RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1) ;
	RHS += log10(1013.246);

  // factor -3 is to adjust units - Vapor Pressure SVP * humidity
	double VP = pow(10, RHS - 3) * humidity;

  // (2) DEWPOINT = F(Vapor Pressure)
	double T = log(VP/0.61078);   // temp var
	return (241.88 * T) / (17.558 - T);
}
//---------------------------------------------------------------
//Read the wind direction sensor, return heading in degrees
int get_wind_direction()
{
  unsigned int adc;
  adc = analogRead(WDIR); // get the current reading from the sensor

  // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
  // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
  // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

  //Wind Vains may vary in the values they return. To get exact wind direction,
  //it is recomended that you AnalogRead the Wind Vain to make sure the values
  //your wind vain output fall within the values listed below.
  if(adc > 2270 && adc < 2290) return (0);//North
  if(adc > 3220 && adc < 3299) return (45);//NE
  if(adc > 3890 && adc < 3999) return (90);//East
  if(adc > 3780 && adc < 3850) return (135);//SE
  if(adc > 3570 && adc < 3650) return (180);//South
  if(adc > 2790 && adc < 2850) return (225);//SW
  if(adc > 1580 && adc < 1610) return (270);//West
  if(adc > 1930 && adc < 1950) return (315);//NW
  return (0); // error, disconnected?
}
//---------------------------------------------------------------
//Returns the instataneous wind speed
float get_wind_speed()
{
  float tstamp = millis();
  float deltaTime = tstamp - lastWindCheck; //750ms
  deltaTime /= 1000.0; //Covert to seconds
  float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4
  windClicks = 0; //Reset and start watching for new wind
  lastWindCheck = tstamp;
  windSpeed *= 1.492; //4 * 1.492 = 5.968MPH
  return(windSpeed);
}

void getSoilTemp()
{
    //get temp from DS18B20
    sensors.requestTemperatures();
    update18B20Temp(inSoilThermometer, InTempC);
    //Every so often there is an error that throws a -127.00, this compensates
    if(InTempC < -100)
      soiltempf = soiltempf;//push last value so data isn't out of scope
    else
      soiltempf = (InTempC * 9)/5 + 32;//else grab the newest, good data
}

void update18B20Temp(DeviceAddress deviceAddress, double &tempC)
{
  tempC = sensors.getTempC(deviceAddress);
}

//This function prints the weather data out to the default Serial Port
void printInfo()
{
  debugOutLn("Been alive for: "+ String(millis()/1000));
  debugOutLn("******************************************");
  debugOut("Wind_Dir: ");
  switch (winddir)
  {
    case 0:
      debugOutLn("North");
      break;
    case 45:
      debugOutLn("NE");
      break;
    case 90:
      debugOutLn("East");
      break;
    case 135:
      debugOutLn("SE");
      break;
    case 180:
      debugOutLn("South");
      break;
    case 225:
      debugOutLn("SW");
      break;
    case 270:
      debugOutLn("West");
      break;
    case 315:
      debugOutLn("NW");
      break;
    default:
      debugOutLn("No Wind");
      // if nothing else matches, do the
      // default (which is optional)
  }

  debugOut("Wind_Speed: ");
  debugOut(String(windspeedmph, 1));
  debugOutLn(" mph");

  debugOut("Rain: ");
  debugOut(String(rainin,2));
  debugOutLn(" in.");

  debugOut("Enclousure Temp: ");
  debugOut(String(tempF));
  debugOutLn(" F");

  debugOut("Humidity: ");
  debugOut(String(humidity));
  debugOutLn("%");

  debugOut("Baro_Temp: ");
  debugOut(String(baroTempF));
  debugOutLn(" F");

  debugOut("Humid_Temp: ");
  debugOut(String(humTempF));
  debugOutLn(" F");

  debugOut("Pressure: ");
  debugOut(String(pascals/100));
  debugOut(" hPa ");
  debugOut(String(inches));
  debugOutLn("in.Hg");

  debugOut("Soil_Temp/ Temp: ");
  debugOut(String(soiltempf));
  debugOutLn(" F");

  //The MPL3115A2 outputs the pressure in Pascals. However, most weather stations
  //report pressure in hectopascals or millibars. Divide by 100 to get a reading
  //more closely resembling what online weather reports may say in hPa or mb.
  //Another common unit for pressure is Inches of Mercury (in.Hg). To convert
  //from mb to in.Hg, use the following formula. P(inHg) = 0.0295300 * P(mb)
  //More info on conversion can be found here:
  //www.srh.noaa.gov/images/epz/wxcalc/pressureConversion.pdf

  //If in altitude mode, print with these lines
  //Serial.print("Altitude:");
  //Serial.print(altf);
  //Serial.println("ft.");
  debugOutLn("******************************************");
  debugOutLn("");
}

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
  raintime = millis(); // grab current time
  raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
  {
    dailyrainin += 0.011; //Each dump is 0.011" of water
    rainHour[minutes] += 0.011; //Increase this minute's amount of rain

    rainlast = raintime; // set up for next event
  }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
  if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
  {
    lastWindIRQ = millis(); //Grab the current time
    windClicks++; //There is 1.492MPH for each click per second.
  }
}
