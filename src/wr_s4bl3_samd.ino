//  --------------------------------------------------------------------------
//  WaterRower S4 BLE Interface
//  Hardware: Using GATT Fitness Machine Service & Rower Data Characteristics 
//  Date: 2020/10/10
//  Author: Vincent Besson
//  Note: GATT implemntation on Adafruit Feather ATMEL Micro SAMD21 MO BLE
//  ---------------------------------------------------------------------------

#include <cdcacm.h>
#include <usbhub.h>

#include "pgmstrings.h"

#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "Adafruit_BluefruitLE_UART.h"
#include "Adafruit_BLEGatt.h"
#include "BluefruitConfig.h"

// Global Define 
#define _VERSION          0.40
#define BLE_SERVICE_NAME "WR S4BL3"           // name of the Bluetooth Service 
#define REFRESH_DATA_TIME 100                 // ms cycle before gathering 

//#define USE_FAKE_DATA                         // Simulate BLE service

//#define DEBUG                               // activate verbose debug on SerialDebug port
//#define DEEPTRACE                           // enable start and end of fucntion to trace any core location
#define _BUFFSIZE 64                          // Max buffer len to read/write Usb Cdc Acm byte queue
#define _S4_PORT_SPEED 57600                  // do not change that it has been tuned for SAMD21

#define VBATPIN A7                            // aka D9 chip pint with a double 100k reistance (already included)

#define SerialDebug Serial1                   // Usb is used by S4, additionnal port on SAMD21
      
#define FitnessMachineService       0x1826
#define FitnessMachineControlPoint  0x2AD9    // Beta Implementation
#define FitnessMachineFeature       0x2ACC    // CX Not implemented yet
#define FitnessMachineStatus        0x2ADA    // Beta Implementation
#define FitnessMachineRowerData     0x2AD1    // CX Main cx implemented

#define batteryService              0x180F    // Additionnal battery service
#define batteryLevel                0x2A19    // Additionnal cx to battery service

class ACMAsyncOper : public CDCAsyncOper{
  public:
    uint8_t OnInit(ACM *pacm);
};

uint8_t ACMAsyncOper::OnInit(ACM *pacm){
  uint8_t rcode;
  // Set DTR = 1 RTS=1
  rcode = pacm->SetControlLineState(3);

  if (rcode){
    ErrorMessage<uint8_t>(PSTR("SetControlLineState"), rcode);
    return rcode;  
  }

  LINE_CODING lc;
  lc.dwDTERate  = _S4_PORT_SPEED;
  lc.bCharFormat  = 0;
  lc.bParityType  = 0;
  lc.bDataBits  = 8;

  rcode = pacm->SetLineCoding(&lc);

  if (rcode)
    ErrorMessage<uint8_t>(PSTR("SetLineCoding"), rcode);

  return rcode;
}

USBHost     UsbH;
ACMAsyncOper  AsyncOper;
ACM           AcmSerial(&UsbH, &AsyncOper);

// BLE PART 

// Create the bluefruit object, either software serial...uncomment these lines
/*
SoftwareSerial bluefruitSS = SoftwareSerial(BLUEFRUIT_SWUART_TXD_PIN, BLUEFRUIT_SWUART_RXD_PIN);

Adafruit_BluefruitLE_UART ble(bluefruitSS, BLUEFRUIT_UART_MODE_PIN,
                      BLUEFRUIT_UART_CTS_PIN, BLUEFRUIT_UART_RTS_PIN);
*/

/* ...or hardware serial, which does not need the RTS/CTS pins. Uncomment this line */
// Adafruit_BluefruitLE_UART ble(BLUEFRUIT_HWSERIAL_NAME, BLUEFRUIT_UART_MODE_PIN);

/* ...hardware SPI, using SCK/MOSI/MISO hardware SPI pins and then user selected CS/IRQ/RST */
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

/* ...software SPI, using SCK/MOSI/MISO user-defined SPI pins and then user selected CS/IRQ/RST */
//Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_SCK, BLUEFRUIT_SPI_MISO,
//                             BLUEFRUIT_SPI_MOSI, BLUEFRUIT_SPI_CS,
//                             BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);


Adafruit_BLEGatt gatt(ble);

// A small helper
void error(const __FlashStringHelper*err) {
  Serial1.println(err);
  while (1);
}

// Service
int32_t fitnessMachineServiceId;
int32_t batteryServiceId;

// Cx
int32_t fitnessMachineControlPointId;
int32_t fitnessMachineFeatureId;
int32_t fitnessMachineStatusId;
int32_t fitnessMachineRowerDataId;
int32_t batteryLevelId;

// The BLE Stack can only send 20 Bytes at a time
// We need to split the BLE Message in 2 pieces
// The Bitfield has been manage accordingly
// Warining Reading the GATT BLE Specification the 0 at the end of Part 1 means includes the field <!>

uint16_t  rowerDataFlagsP1=0b0000011111110;
uint16_t  rowerDataFlagsP2=0b1111100000001;

  //P1
  // 0000000000001 - 1   - 0x001 - More Data 0 <!> WARNINNG <!> This Bit is working the opposite way, 0 means field is present, 1 means not present
  // 0000000000010 - 2   - 0x002 - Average Stroke present
  // 0000000000100 - 4   - 0x004 - Total Distance Present
  // 0000000001000 - 8   - 0x008 - Instantaneous Pace present
  // 0000000010000 - 16  - 0x010 - Average Pace Present
  // 0000000100000 - 32  - 0x020 - Instantaneous Power present
  // 0000001000000 - 64  - 0x040 - Average Power present
  // 0000010000000 - 128 - 0x080 - Resistance Level present
  //P2
  // 0000100000000 - 256 - 0x080 - Expended Energy present
  // 0001000000000 - 512 - 0x080 - Heart Rate present
  // 0010000000000 - 1024- 0x080 - Metabolic Equivalent present
  // 0100000000000 - 2048- 0x080 - Elapsed Time present
  // 1000000000000 - 4096- 0x080 - Remaining Time present

  //  C1  Stroke Rate             uint8     Position    2 (After the Flag 2bytes)
  //  C1  Stroke Count            uint16    Position    3 
  //  C2  Average Stroke Rate     uint8     Position    5
  //  C3  Total Distance          uint24    Position    6
  //  C4  Instantaneous Pace      uint16    Position    9
  //  C5  Average Pace            uint16    Position    11
  //  C6  Instantaneous Power     sint16    Position    13
  //  C7  Average Power           sint16    Position    15
  //  C8  Resistance Level        sint16    Position    17
  //  C9  Total Energy            uint16    Position    19
  //  C9  Energy Per Hour         uint16    Position    21
  //  C9  Energy Per Minute       uint8     Position    23
  //  C10 Heart Rate              uint8     Position    24
  //  C11 Metabolic Equivalent    uint8     Position    25
  //  C12 Elapsed Time            uint16    Position    26
  //  C13 Remaining Time          uint16    Position    28

struct rowerDataKpi{
  int bpm; // Start of Part 1
  int strokeCount;
  int tmpstrokeRate;
  int strokeRate;
  int averageStokeRate;
  int totalDistance;
  int instantaneousPace;
  int tmpinstantaneousPace;
  int averagePace;
  int instantaneousPower;
  int averagePower;
  int resistanceLevel;
  int totalEnergy; // Start of Part 2
  int energyPerHour;
  int energyPerMinute;
  int heartRate;
  int metabolicEquivalent;
  int elapsedTime;
  int elapsedTimeSec;
  int elapsedTimeMin;
  int elapsedTimeHour;
  int remainingTime;
};

struct rowerDataKpi rdKpi;

bool s4InitFlag=false;
bool s4SendUsb=false;
bool bleInitFlag=false;

bool bleConnectionStatus=false;

int s4KpiTurn=0;
int usbCounterCycle=0;

unsigned long currentTime=0;
unsigned long previousTime=0;
unsigned long battPreviousTime=0;

struct s4MemoryMap{
  char  desc[32];
  char  addr[4]; // 3+1
  char  msize[2]; //1+1
  int * kpi; // void cause can be in or lon int
  int base; // 10 for Decimal, 16 for Hex used by strtol
};

#define S4SIZE 9
struct s4MemoryMap s4mmap[S4SIZE]; 

void setup(){

  SerialDebug.begin(19200);
  delay(25);

  SerialDebug.println("/************************************");
  SerialDebug.println(" * Starting");
  SerialDebug.println(" * WaterRower S4 BLE Module ");
  SerialDebug.println(" * Vincent Besson vincent(at)besson.be");
  SerialDebug.println(" * Date 2020/10/18");
  SerialDebug.print  (" * Version ");
  SerialDebug.println(_VERSION);
  SerialDebug.println(" ***********************************/");
 
  sprintf(s4mmap[0].desc,"instantaneousPower");
  sprintf(s4mmap[0].addr,"088");
  sprintf(s4mmap[0].msize,"D");
  s4mmap[0].kpi=&rdKpi.instantaneousPower;
  s4mmap[0].base=16;

  sprintf(s4mmap[1].desc,"totalDistance");
  sprintf(s4mmap[1].addr,"057");
  sprintf(s4mmap[1].msize,"D");
  s4mmap[1].kpi=&rdKpi.totalDistance;
  s4mmap[1].base=16;

  sprintf(s4mmap[2].desc,"strokeCount");
  sprintf(s4mmap[2].addr,"140");
  sprintf(s4mmap[2].msize,"D");
  s4mmap[2].kpi=&rdKpi.strokeCount;
  s4mmap[2].base=16;

  sprintf(s4mmap[3].desc,"strokeRate");
  sprintf(s4mmap[3].addr,"1A9");
  sprintf(s4mmap[3].msize,"S");
  s4mmap[3].kpi=&rdKpi.tmpstrokeRate;
  s4mmap[3].base=16;

  sprintf(s4mmap[4].desc,"instantaneousPace m/s");
  sprintf(s4mmap[4].addr,"14A");
  sprintf(s4mmap[4].msize,"D");
  s4mmap[4].kpi=&rdKpi.tmpinstantaneousPace;
  s4mmap[4].base=16;

  sprintf(s4mmap[5].desc,"elapsedTimeSec");
  sprintf(s4mmap[5].addr,"1E1");
  sprintf(s4mmap[5].msize,"S");
  s4mmap[5].kpi=&rdKpi.elapsedTimeSec;
  s4mmap[5].base=10;

  sprintf(s4mmap[6].desc,"elapsedTimeMin");
  sprintf(s4mmap[6].addr,"1E2");
  sprintf(s4mmap[6].msize,"S");
  s4mmap[6].kpi=&rdKpi.elapsedTimeMin;
  s4mmap[6].base=10;

  sprintf(s4mmap[7].desc,"elapsedTimeHour");
  sprintf(s4mmap[7].addr,"1E3");
  sprintf(s4mmap[7].msize,"S");
  s4mmap[7].kpi=&rdKpi.elapsedTimeHour;
  s4mmap[7].base=10;

  sprintf(s4mmap[8].desc,"elapsedTime");
  sprintf(s4mmap[8].addr,"1E8");
  sprintf(s4mmap[8].msize,"D");
  s4mmap[8].kpi=&rdKpi.elapsedTime;
  s4mmap[8].base=16;
  
  /*
   * Wipe Clean the Bluetooth Module
   */ 

  if ( !ble.begin(VERBOSE_MODE) ){
    error(F("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }

  ble.factoryReset(true); // Complete Wipeclean the Bluetooth Module
  SerialDebug.println("BLE init OK");
  
  if (UsbH.Init())
    SerialDebug.println("USB host failed to initialize");
  
  SerialDebug.println("USB Host init OK"); 
  initBLE(); // Todo: to be removed, Activate for testing to be removed Move after USB Reset
  
  currentTime=millis();
  previousTime=millis();
  
#ifdef USE_FAKE_DATA
  initFakeBleData();
#else
  initBleData();
#endif
  
}

/*
 * InitBLE Will init the BLE module of M0 Express
 */

void initBLE(){
  randomSeed(micros());

  /* Initialise the module */
  SerialDebug.print(F("Init BLE:"));

  if ( !ble.begin(VERBOSE_MODE) ){
    error(F("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }
  SerialDebug.println( F("OK!") );

  /* Perform a factory reset to make sure everything is in a known state */
  SerialDebug.println(F("Performing a factory reset: "));
  if (! ble.factoryReset() ){
       error(F("Couldn't factory reset"));
  }

  /* Disable command echo from Bluefruit */
  ble.echo(false);

  SerialDebug.println("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();

  /* Change the device name to make it easier to find */
  SerialDebug.print("Setting device name to:");
  SerialDebug.println(BLE_SERVICE_NAME);
  
  char atCmd[128];
  sprintf(atCmd,"AT+GAPDEVNAME=%s",BLE_SERVICE_NAME);
  if (! ble.sendCommandCheckOK(F(atCmd)) ) {
    error(F("Could not set device name?"));
  }

  /* Add the Fitness Machine Service definition */
  /* Service ID should be 1 */
  SerialDebug.println(F("Adding the Fitness Machine Service definition (UUID = 0x1826): "));
  fitnessMachineServiceId = gatt.addService(FitnessMachineService);
  if (fitnessMachineServiceId == 0) {
    error(F("Could not add Fitness Machine Service"));
  }
  
  /* Add the Fitness Machine Rower Data characteristic */
  /* Chars ID for Measurement should be 1 */
  fitnessMachineRowerDataId = gatt.addCharacteristic(FitnessMachineRowerData, GATT_CHARS_PROPERTIES_NOTIFY, 1, 30, BLE_DATATYPE_BYTEARRAY);
  if (fitnessMachineRowerDataId == 0) {
    error(F("Could not add Fitness Machine Rower Data characteristic"));
  }

  /* Add the Fitness Machine Control Point characteristic */
  fitnessMachineControlPointId = gatt.addCharacteristic(FitnessMachineControlPoint, GATT_CHARS_PROPERTIES_WRITE, 1, 10, BLE_DATATYPE_BYTEARRAY);
  if (fitnessMachineControlPointId == 0) {
    error(F("Could not add Fitness Machine Control Point characteristic"));
  }
  /* Add the Fitness Machine Status characteristic */
  fitnessMachineStatusId = gatt.addCharacteristic(FitnessMachineStatus, GATT_CHARS_PROPERTIES_NOTIFY, 1, 10, BLE_DATATYPE_BYTEARRAY);
  if (fitnessMachineStatusId == 0) {
    error(F("Could not add Fitness Machine Status characteristic"));
  }
  
  SerialDebug.println(F("Adding Fitness Machine Service UUID to the advertising payload "));
  uint8_t advdata[] { 0x02, 0x01, 0x06, 0x05, 0x02, 0x26, 0x18, 0x0a, 0x18 };
  ble.setAdvData( advdata, sizeof(advdata) );

  SerialDebug.println(F("Adding the Battery Service definition (UUID = 0x180F): "));
  batteryServiceId = gatt.addService(batteryService);
  if (batteryServiceId == 0) {
    error(F("Could not add Battery Service"));
  }

  batteryLevelId = gatt.addCharacteristic(batteryLevel, GATT_CHARS_PROPERTIES_NOTIFY, 1, 1, BLE_DATATYPE_BYTEARRAY);
  if (batteryLevelId == 0) {
    error(F("Could not add Battery characteristic"));
  }
  
  /* Reset the device for the new service setting changes to take effect */
  SerialDebug.println(F("Performing a SW reset (service changes require a reset)"));
  ble.reset();

  bleInitFlag=true;

  SerialDebug.println();
}

void initFakeBleData(){
  
  rdKpi.bpm=59;
  rdKpi.strokeCount=0;
  rdKpi.strokeRate=0;
  rdKpi.averageStokeRate=25;
  rdKpi.totalDistance=1000;
  rdKpi.instantaneousPace=90;
  rdKpi.tmpinstantaneousPace=0;
  rdKpi.averagePace=120;
  rdKpi.instantaneousPower=70;
  rdKpi.averagePower=100;
  rdKpi.resistanceLevel=17;
  rdKpi.totalEnergy=100;
  rdKpi.energyPerHour=210;
  rdKpi.energyPerMinute=300;
  rdKpi.heartRate=120;
  rdKpi.metabolicEquivalent=10;
  rdKpi.elapsedTime=100;
  rdKpi.elapsedTimeSec=4;
  rdKpi.elapsedTimeMin=5;
  rdKpi.elapsedTimeHour=1;
  rdKpi.remainingTime=600;
}

void setFakeCxRowerDataP1(){

  unsigned char cRower[30];
      
  cRower[0]=rowerDataFlagsP1 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP1 & 0x0000FF00) >> 8;
         
  rdKpi.strokeRate=13;
  cRower[2] = rdKpi.strokeRate & 0x000000FF;
  
  rdKpi.strokeCount++;
  cRower[3] = rdKpi.strokeCount & 0x000000FF;
  cRower[4] = (rdKpi.strokeCount & 0x0000FF00) >> 8;
  
  rdKpi.averageStokeRate=(rdKpi.averageStokeRate+1);
  cRower[5] = rdKpi.averageStokeRate & 0x000000FF;
  
  rdKpi.totalDistance=rdKpi.totalDistance+10;
  cRower[6] = rdKpi.totalDistance &  0x000000FF;
  cRower[7] = (rdKpi.totalDistance & 0x0000FF00) >> 8;
  cRower[8] = (rdKpi.totalDistance & 0x00FF0000) >> 16;
  
  cRower[9] = rdKpi.instantaneousPace & 0x000000FF;
  cRower[10] = (rdKpi.instantaneousPace & 0x0000FF00) >> 8;
  
  cRower[11] = rdKpi.averagePace & 0x000000FF;
  cRower[12] = (rdKpi.averagePace & 0x0000FF00) >> 8;

  cRower[13] = rdKpi.instantaneousPower & 0x000000FF;
  cRower[14] = (rdKpi.instantaneousPower & 0x0000FF00) >> 8;

  cRower[15] = rdKpi.averagePower & 0x000000FF;
  cRower[16] = (rdKpi.averagePower & 0x0000FF00) >> 8;

  cRower[17] = rdKpi.resistanceLevel & 0x000000FF;
  cRower[18] = (rdKpi.resistanceLevel & 0x0000FF00) >> 8;

  gatt.setChar(fitnessMachineRowerDataId, cRower, 19);
  
}

void setFakeCxRowerDataP2(){
 
  unsigned char cRower[13];
      
  cRower[0]=rowerDataFlagsP2 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP2 & 0x0000FF00) >> 8;
         
  rdKpi.totalEnergy++;
  cRower[2] = rdKpi.totalEnergy & 0x000000FF;
  cRower[3] = (rdKpi.totalEnergy & 0x0000FF00) >> 8;

  cRower[4] = rdKpi.energyPerHour & 0x000000FF;
  cRower[5] = (rdKpi.energyPerHour & 0x0000FF00) >> 8;

  cRower[6] = rdKpi.energyPerMinute & 0x000000FF;

  cRower[7] = rdKpi.bpm & 0x000000FF;

  cRower[8] = rdKpi.metabolicEquivalent& 0x000000FF;

  rdKpi.elapsedTime++;
  cRower[9] =   rdKpi.elapsedTime & 0x000000FF;
  cRower[10] = (rdKpi.elapsedTime & 0x0000FF00) >> 8;

  rdKpi.remainingTime--;
  cRower[11] = rdKpi.remainingTime & 0x000000FF;
  cRower[12] = (rdKpi.remainingTime & 0x0000FF00) >> 8;
  
  gatt.setChar(fitnessMachineRowerDataId, cRower, 13);
  
}
void initBleData(){
  
  rdKpi.bpm=0;
  rdKpi.strokeCount=0;
  rdKpi.tmpstrokeRate=0;
  rdKpi.strokeRate=0;
  rdKpi.averageStokeRate=0;
  rdKpi.totalDistance=0;
  rdKpi.tmpinstantaneousPace=0;
  rdKpi.instantaneousPace=0;
  rdKpi.averagePace=0;
  rdKpi.instantaneousPower=0;
  rdKpi.averagePower=0;
  rdKpi.resistanceLevel=0;
  rdKpi.totalEnergy=0;
  rdKpi.energyPerHour=0;
  rdKpi.energyPerMinute=0;
  rdKpi.heartRate=0;
  rdKpi.metabolicEquivalent=0;
  rdKpi.elapsedTime=0;
  rdKpi.elapsedTimeSec=0;
  rdKpi.elapsedTimeMin=0;
  rdKpi.elapsedTimeHour=0;
  rdKpi.remainingTime=0;
}
 /*
  *  Read Cx fitnessMachineControlPointId
  */
void setCxFitnessStatus(uint8_t data[],int len){

// OPCODE     DESCRIPTION                                             PARAMETERS
// 0x01       Reset                                                   N/A
// 0x02       Fitness Machine Stopped or Paused by the User
// 0x03       Fitness Machine Stopped by Safety Key                   N/A
// 0x04       Fitness Machine Started or Resumed by the User          N/A
// 0x05       Target Speed Changed
// 0x06       Target Incline Changed
// 0x07       Target Resistance Level Changed
// 0x08       Target Power Changed
// 0x09       Target Heart Rate Changed
// 0x0B       Targeted Number of Steps Changed                        New Targeted Number of Steps Value (UINT16, in Steps with a resolution of 1)
// 0x0C       Targeted Number of Strides Changed                      New Targeted Number of Strides (UINT16, in Stride with a resolution of 1)
// 0x0D       Targeted Distance Changed                               New Targeted Distance (UINT24, in Meters with a resolution of 1)
// 0x0E       Targeted Training Time Changed                          New Targeted Training Time (UINT16, in Seconds with a resolution of 1)

  gatt.setChar(fitnessMachineStatusId,data,len);

}

void getCxFitnessControlPoint(){
  
  unsigned char rData[32]; // Read Buffer Reading the specification MAX_SIZE is 1 Opcode & 18 Octets parameter (without potential header)
  unsigned char wData[32]; // Write Buffer Reading the specification MAX_SIZE is 1 opcode & 17 Octets for parameter
  int len=0;
  
  unsigned char statusData[32];
  char s4buffer[32];
  char tmp[32];

  len=gatt.getChar(fitnessMachineControlPointId, rData, 32);
  if (len>0){
    // A Message is received from the BLE Client
    // 1 start getting the opcode
    
    
    if (rData[0]!=0x80){
      SerialDebug.print("getCxFitnessControlPoint() Data:");
      for (int i=0;i<len;i++){
        sprintf(tmp,"0x%02X;",rData[i]);
        SerialDebug.print(tmp);
      }
      SerialDebug.print("len:");
      SerialDebug.println(len);
    }

    switch(rData[0]){
      
      case 0x00:        // Take Control Request
        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);
        break;

      case 0x01:        // RESET Command Request
        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);
        
        // Send reset command to the WR S4
        //writeCdcAcm((char*)"RESET");
        setReset();

        statusData[0]=0x01;
        setCxFitnessStatus(statusData,1);

        break;

      case 0x07:        // Start / Resume Command Request
        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);

        //Send start/resume command to S4
        break;

      case 0x08:        // Stop / Pause Command Request
        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);

        //Send start/resume command to S4
        break;

      case 0x0C:{        
        
        // set Target Distance follow by a UINT 24 in meter with a resolution of 1 m
        // It is also recommended that the rowing computer is RESET prior to downloading any workout, a PING after a reset will indicate the rowing computer is ready again for data.
        // S4 Command W SI+X+YYYY+0x0D0A
        
        long distance= (rData[1] &  0x000000FF) + ((rData[2] << 8 ) & 0x0000FF00)  +  ((rData[3] << 16) & 0x00FF0000); // LSO..MO (Inversed)
        SerialDebug.print("distance:");
        SerialDebug.println(distance);

        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);
        
        //writeCdcAcm((char*)"RESET");
        // TODO Check the need of Reset at this stage
        setReset();

        // Assuming the coding is MSO..LSO (Normal)
        sprintf(s4buffer,"WSI1%04X",distance);
        SerialDebug.println(s4buffer);
        writeCdcAcm((char*)s4buffer);

        statusData[0]=0x0D;
        statusData[1]=rData[1];
        statusData[2]=rData[2];
        statusData[3]=rData[3];

        setCxFitnessStatus(statusData,4);
        // Send new workout distance to the S4 
        // todo add reset
        break;
      }

      case 0x0D:{    
        // Set Target time follow by a UINT16 in second with a resolution of 1 sec
        //It is also recommended that the rowing computer is RESET prior to downloading any workout, a PING after a reset will indicate the rowing computer is ready again for data.
        // S4 Command W SU + YYYY + 0x0D0A
        
        long duration= (rData[1] &  0x000000FF) + ((rData[2] << 8 ) & 0x0000FF00);  // LSO..MO (Inversed)
        
        wData[0]=0x80;  // Response opcode 
        wData[1]=0x01;  // for success
        gatt.setChar(fitnessMachineControlPointId, wData, 2);

         //writeCdcAcm((char*)"RESET");
         // TODO Check the need of Reset at this stage
         setReset();
        
        // Assuming the coding is MSO..LSO (Normal)
        sprintf(s4buffer,"WSU%04X",duration);
        SerialDebug.println(s4buffer);
        writeCdcAcm((char*)s4buffer);

        break;
      }
      default:
        break;

    }

  }

}

 /*
  * Selected BLE Fields to be sent in one message (look at the +/_)
  * This may help to better understand how it is built
  */

void setCxLightRowerData(){

#ifdef DEEPTRACE
  SerialDebug.printf("sendBleLightData() start");
#endif
  // This function is a subset of field to be sent in one piece
  // An alternative to the sendBleData()
  uint16_t  rowerDataFlags=0b0000001111110;

  // 0000000000001 - 1   - 0x001 + More Data 0 <!> WARNINNG <!> This Bit is working the opposite way, 0 means field is present, 1 means not present
  // 0000000000010 - 2   - 0x002 + Average Stroke present
  // 0000000000100 - 4   - 0x004 + Total Distance Present
  // 0000000001000 - 8   - 0x008 + Instantaneous Pace present
  // 0000000010000 - 16  - 0x010 + Average Pace Present
  // 0000000100000 - 32  - 0x020 + Instantaneous Power present
  // 0000001000000 - 64  - 0x040 + Average Power present
  // 0000010000000 - 128 - 0x080 - Resistance Level present
  // 0000100000000 - 256 - 0x080 + Expended Energy present
  // 0001000000000 - 512 - 0x080 - Heart Rate present
  // 0010000000000 - 1024- 0x080 - Metabolic Equivalent present
  // 0100000000000 - 2048- 0x080 - Elapsed Time present
  // 1000000000000 - 4096- 0x080 - Remaining Time present

  //  C1  Stroke Rate             uint8     Position    2  + (After the Flag 2bytes)
  //  C1  Stroke Count            uint16    Position    3  +
  //  C2  Average Stroke Rate     uint8     Position    5  +
  //  C3  Total Distance          uint24    Position    6  +
  //  C4  Instantaneous Pace      uint16    Position    9  +
  //  C5  Average Pace            uint16    Position    11 +
  //  C6  Instantaneous Power     sint16    Position    13 +
  //  C7  Average Power           sint16    Position    15 +
  //  C8  Resistance Level        sint16    Position    17 -
  //  C9  Total Energy            uint16    Position    19 +
  //  C9  Energy Per Hour         uint16    Position    21 +
  //  C9  Energy Per Minute       uint8     Position    23 +
  //  C10 Heart Rate              uint8     Position    24 -
  //  C11 Metabolic Equivalent    uint8     Position    25 -
  //  C12 Elapsed Time            uint16    Position    26 -
  //  C13 Remaining Time          uint16    Position    28 -

  unsigned char cRower[17];

  cRower[0]=rowerDataFlags & 0x000000FF;
  cRower[1]=(rowerDataFlags & 0x0000FF00) >> 8;
  
  rdKpi.strokeRate=rdKpi.tmpstrokeRate*2;
  cRower[2] = rdKpi.strokeRate & 0x000000FF;
  
  cRower[3] = rdKpi.strokeCount & 0x000000FF;
  cRower[4] = (rdKpi.strokeCount & 0x0000FF00) >> 8;
  
  cRower[5] = rdKpi.averageStokeRate & 0x000000FF;
  
  cRower[6] = rdKpi.totalDistance &  0x000000FF;
  cRower[7] = (rdKpi.totalDistance & 0x0000FF00) >> 8;
  cRower[8] = (rdKpi.totalDistance & 0x00FF0000) >> 16;
  
  if (rdKpi.tmpinstantaneousPace>0) // Avoid Divide by Zero
    rdKpi.instantaneousPace=(100000/rdKpi.tmpinstantaneousPace)/2;
  cRower[9] = rdKpi.instantaneousPace & 0x000000FF;
  cRower[10] = (rdKpi.instantaneousPace & 0x0000FF00) >> 8;
  
  cRower[11] = rdKpi.averagePace & 0x000000FF;
  cRower[12] = (rdKpi.averagePace & 0x0000FF00) >> 8;

  cRower[13] = rdKpi.instantaneousPower & 0x000000FF;
  cRower[14] = (rdKpi.instantaneousPower & 0x0000FF00) >> 8;

  cRower[15] = rdKpi.averagePower & 0x000000FF;
  cRower[16] = (rdKpi.averagePower & 0x0000FF00) >> 8;

  gatt.setChar(fitnessMachineRowerDataId, cRower, 17);

#ifdef DEEPTRACE
  SerialDebug.printf("sendBleLightData() end");
#endif
}

/*
 * todo This has still to be tested
 */

void setCxBattery(){

#ifdef DEEPTRACE
  SerialDebug.printf("sendBleBattery() start");
#endif
unsigned char hexBat[2];
float measuredVbat = analogRead(VBATPIN);
measuredVbat *= 2;    // we divided by 2, so multiply back
measuredVbat *= 3.3;  // Multiply by 3.3V, our reference voltage
measuredVbat /= 1024; // convert to voltage

int batteryLevelPercent=((measuredVbat-2.9)/(5.15-2.90))*100;

SerialDebug.print("Mesured Bat:");
SerialDebug.println(batteryLevelPercent);

hexBat[0]=batteryLevelPercent & 0x000000FF;
hexBat[1]='\0'; // just in case 

gatt.setChar(batteryLevelId, hexBat, 1);

#ifdef DEEPTRACE
  SerialDebug.printf("sendBleBattery() end");
#endif

}

/*
 *  Full Message sentd in 2 part
 */

void setCxRowerData(){
  // Due the size limitation of the message in the BLE Stack of the NRF
  // the message will be split in 2 parts with the according Bitfield (read the spec :) )
  
  unsigned char cRower[19]; // P1 is the biggest part whereas P2 is 13
  
  // Send the P1 part of the Message
  cRower[0]=rowerDataFlagsP1 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP1 & 0x0000FF00) >> 8;
  
  rdKpi.strokeRate=rdKpi.tmpstrokeRate*2;
  cRower[2] = rdKpi.strokeRate & 0x000000FF;
  
  cRower[3] = rdKpi.strokeCount & 0x000000FF;
  cRower[4] = (rdKpi.strokeCount & 0x0000FF00) >> 8;
  
  cRower[5] = rdKpi.averageStokeRate & 0x000000FF;
  
  cRower[6] = rdKpi.totalDistance &  0x000000FF;
  cRower[7] = (rdKpi.totalDistance & 0x0000FF00) >> 8;
  cRower[8] = (rdKpi.totalDistance & 0x00FF0000) >> 16;
  
  cRower[9] = rdKpi.instantaneousPace & 0x000000FF;
  cRower[10] = (rdKpi.instantaneousPace & 0x0000FF00) >> 8;
  
  cRower[11] = rdKpi.averagePace & 0x000000FF;
  cRower[12] = (rdKpi.averagePace & 0x0000FF00) >> 8;

  cRower[13] = rdKpi.instantaneousPower & 0x000000FF;
  cRower[14] = (rdKpi.instantaneousPower & 0x0000FF00) >> 8;

  cRower[15] = rdKpi.averagePower & 0x000000FF;
  cRower[16] = (rdKpi.averagePower & 0x0000FF00) >> 8;

  cRower[17] = rdKpi.resistanceLevel & 0x000000FF;
  cRower[18] = (rdKpi.resistanceLevel & 0x0000FF00) >> 8;

  gatt.setChar(fitnessMachineRowerDataId, cRower, 19);

  // Send the P2 part of the Message
  cRower[0]=rowerDataFlagsP2 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP2 & 0x0000FF00) >> 8;
         
  cRower[2] = rdKpi.totalEnergy & 0x000000FF;
  cRower[3] = (rdKpi.totalEnergy & 0x0000FF00) >> 8;

  cRower[4] = rdKpi.energyPerHour & 0x000000FF;
  cRower[5] = (rdKpi.energyPerHour & 0x0000FF00) >> 8;

  cRower[6] = rdKpi.energyPerMinute & 0x000000FF;

  cRower[7] = rdKpi.bpm & 0x000000FF;

  cRower[8] = rdKpi.metabolicEquivalent& 0x000000FF;

  rdKpi.elapsedTime=rdKpi.elapsedTimeSec+rdKpi.elapsedTimeMin*60+rdKpi.elapsedTimeHour*3600;
  cRower[9] =   rdKpi.elapsedTime & 0x000000FF;
  cRower[10] = (rdKpi.elapsedTime & 0x0000FF00) >> 8;

  cRower[11] = rdKpi.remainingTime & 0x000000FF;
  cRower[12] = (rdKpi.remainingTime & 0x0000FF00) >> 8;
  
  gatt.setChar(fitnessMachineRowerDataId, cRower, 13);

}

void setReset(){
  // This function will send a Reset Command to S4
  // and also initBLEData

  // TODO : To be tested

  writeCdcAcm((char*)"RESET"); 
  initBleData();
}

void writeCdcAcm(char str[]){
  UsbH.Task();
  // Important S4 can not handle more than 25 msec

#ifdef DEEPTRACE
  SerialDebug.printf("writeCdcAcm() start");
#endif

  uint8_t rcode;  //return code of the USB port
  uint8_t c;
  int ll=strlen(str)+2;  
  char buf[ll+1]; // Buffer of Bytes to be sent
  sprintf(buf,"%s\r\n",str);
  
  if( AcmSerial.isReady()) {
    readCdcAcm();
    if (ll > 0) {
      /* sending to USB CDC ACM */
#ifdef DEBUG
      SerialDebug.print("<");
      SerialDebug.print(buf);
#endif

      for (int i=0;i<ll;i++){
        c=buf[i];
#ifdef DEBUG
        SerialDebug.write(c);
#endif
        delay(10);          // By the S4 Spec Wait 10/25msec between each char sent
        rcode = AcmSerial.SndData(1, &c);
        if (rcode)
          ErrorMessage<uint8_t>(PSTR("SndData"), rcode);
      } 
    }
  }else{
    SerialDebug.print("USB CDC ACM not ready for write\n");
  }

#ifdef DEEPTRACE
  SerialDebug.printf("writeCdcAcm() end");
#endif

}

void readCdcAcm(){
//UsbH.Task();
#ifdef DEEPTRACE
  SerialDebug.printf("readCdcAcm() start");
#endif

  /* reading USB CDC ACM */
  /* buffer size must be greater or equal to max.packet size */
  /* it it set to 64 (largest possible max.packet size) here, can be tuned down for particular endpoint */
  //ASCSI TABLE https://fr.wikibooks.org/wiki/Les_ASCII_de_0_%C3%A0_127/La_table_ASCII
 
  if( AcmSerial.isReady()) {

    uint8_t rcode;
    char buf[_BUFFSIZE];    
    uint16_t rcvd = sizeof(buf);
    
    rcode = AcmSerial.RcvData(&rcvd, (uint8_t *)buf); 
    if(rcvd){ //more than zero bytes received
      buf[rcvd]='\0';

#ifdef DEBUG
      SerialDebug.print(">");
      SerialDebug.print(rcvd);
      SerialDebug.print(",");
      SerialDebug.print(buf);
#endif
      parseS4ReceivedData(buf,rcvd);
    }

    if (rcode && rcode != USB_ERRORFLOW)
      ErrorMessage<uint8_t>(PSTR("Ret"), rcode);

  }else{
    SerialDebug.print("USB CDC ACM not ready for read\n");
  }

#ifdef DEEPTRACE
  SerialDebug.printf("readCdcAcm() end");
#endif
}

void parseS4ReceivedData(char data[],int len){

#ifdef DEEPTRACE
  SerialDebug.printf("parseS4ReceivedData() start");
#endif

  SerialDebug.println(data);
  if (len>2){

    if (data[len-1]=='\n' && data[len-2]=='\r'){
      data[len-2]='\0';
    }
    else{
#ifdef DEBUG
      SerialDebug.println("");
#endif
    }
    decodeS4Message(data);
  }else{
      SerialDebug.println("Inv msg");
   }

#ifdef DEEPTRACE
  SerialDebug.printf("parseS4ReceivedData() end");
#endif

}

int asciiHexToInt(char hex[],int base){

#ifdef DEEPTRACE
  SerialDebug.printf("asciiHexToInt() start");
#endif

  //https://stackoverflow.com/questions/23576827/arduino-convert-a-string-hex-ffffff-into-3-int
  int number = (int) strtol( &hex[0], NULL, base);

#ifdef DEEPTRACE
  SerialDebug.printf("asciiHexToInt() end");
#endif

  return number;
}

void decodeS4Message(char cmd[]){

#ifdef DEEPTRACE
  SerialDebug.printf("decodeS4Message() start");
#endif

#ifdef DEBUG
  SerialDebug.print("=");
  SerialDebug.print(cmd);
  SerialDebug.print("\n");
#endif  
  switch (cmd[0]){
    case '_':
    
      if (!strcmp(cmd,"_WR_")){
        writeCdcAcm((char*)"IV?");
        //readCdcAcm();
      }
      break;
    case 'E':
    
      break;
    case 'I': // Information Handler

      if (cmd[1]=='V'){
        if (!strcmp(cmd,"IV40210")){ // 
          SerialDebug.println("S4 Good Firmware Version");
        }
        //writeCdcAcm((char*)"RESET");         // You should here a Bip on the WaterRower
        setReset();
        
        readCdcAcm();
        s4InitFlag=true;
        // Init the BLE; 
        initBLE();
      }
      else if (cmd[1]=='D') { // Incomming data from S4
        if (strlen(cmd)>6){
          for (int i=0;i<S4SIZE;i++){
          
            if (!strncmp(cmd+3,s4mmap[i].addr,3)){
              *s4mmap[i].kpi=asciiHexToInt(cmd+6,s4mmap[i].base);
//#ifdef DEBUG
              SerialDebug.print(s4mmap[i].desc);
              SerialDebug.print(",");
              SerialDebug.print(s4mmap[i].addr);
              SerialDebug.print("=");
              SerialDebug.println(*s4mmap[i].kpi);
//#endif            
              break;  
            }
          }
        }
      }
      break;
     
    case 'O':
      break;
     
    case 'P':
      break;
     
    case 'S':
      break;
    
    default:
      break;
  }

#ifdef DEEPTRACE
  SerialDebug.printf("decodeS4Message() end");
#endif

}



void loop(){
  // Start with Usb Host Task
  // No Delay expect the one at the end to avoid coredump due to USB R/W Collision
  // 
  UsbH.Task(); 

#ifdef DEEPTRACE
  SerialDebug.print("loop() start");
#endif
  
  currentTime=millis();
  // TODO FOR TEST ONLY to be removed
 /*
  if ((currentTime-battPreviousTime)>100){ // Every 60 sec send Battery percent to GATT Battery Level Service
    battPreviousTime=currentTime;
    setCxBattery();
    getCxFitnessControlPoint();
    #ifdef USE_FAKE_DATA
        setFakeCxRowerDataP1();
        setFakeCxRowerDataP2();
    #else
        
        setCxLightRowerData();
    
    #endif
  }
*/


  if (s4InitFlag==false && AcmSerial.isReady() ){
    SerialDebug.println("Going in");
    if (s4SendUsb==false){
      SerialDebug.print("USB read");
      writeCdcAcm((char*)"USB");
      readCdcAcm(); 
      s4SendUsb=true;
    }

    if ((currentTime-previousTime)>10000 && s4SendUsb==true){ // If After 10 sec no Reset then retry
      previousTime=currentTime;
      s4SendUsb=false;
    }

  }else if(s4InitFlag==true && AcmSerial.isReady()){
    
    if ((currentTime-previousTime)>REFRESH_DATA_TIME){
      previousTime=currentTime;
      if ( s4InitFlag==true && bleInitFlag==true && bleConnectionStatus==true ){ // Get S4 Data Only if BLE is Connected

        char cmd[7];
        sprintf(cmd,"IR%s%s",s4mmap[s4KpiTurn].msize,s4mmap[s4KpiTurn].addr); // One KPI per cycle or SAMD21 will lost message
        writeCdcAcm(cmd);

        s4KpiTurn++;
        if (s4KpiTurn==5)
        s4KpiTurn=0; 
      }
      
      // Send BLE Data 

      if (ble.isConnected() && s4InitFlag==true ){ // Start Sending BLE Data only when BLE is connected and when S4 is fully initialized
        
        if ((currentTime-battPreviousTime)>60000){ // Every 60 sec send Battery percent to GATT Battery Level Service
          battPreviousTime=currentTime;
          setCxBattery();
        }

        if (bleConnectionStatus==false)
          SerialDebug.println("BLE:Connected");   
        bleConnectionStatus=true;
        
#ifdef USE_FAKE_DATA
        setFakeCxRowerDataP1();
        setFakeCxRowerDataP2();
#else
        //setCxRowerData();
        setCxLightRowerData();
#endif

      }else{
        if (bleConnectionStatus==true)
          SerialDebug.println("BLE:Disconnected");
        bleConnectionStatus=false;
      }
    }
    readCdcAcm();
  }else if (!AcmSerial.isReady()){
    usbCounterCycle++;
    //previousTime=currentTime;
    if (usbCounterCycle>10){  // Need 32 Cycle of USB.task to init the USB
      //SerialDebug.println("USB Serial is not ready sleep for 1 sec");
      delay(100);
      usbCounterCycle=0;
    }
  }
#ifdef DEEPTRACE
  SerialDebug.println("loop() end");
#endif
delay(5); // This avoid coredump mesured by JTAG, Period of S4 is 5 ms ;)
}
