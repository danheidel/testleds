#include <FastSPI_LED.h>

#define NUM_LEDS 64

// Sometimes chipsets wire in a backwards sort of way
//struct CRGB { unsigned char b; unsigned char r; unsigned char g; };
struct CRGB { uint8_t r; uint8_t g; uint8_t b; };
struct CRGB *leds;
uint8_t *buffer; //64 byte serial buffer

enum controlSet{
  CMDSTART = 255,    //commands start with this byte
  //CMDEND = 128,      //commands end with this byte
  LOADFRAME = 1,     //next 2 bytes are uint16_t for starting pixel followed by 
                     //48 bytes of pixel RGB data (16 pixels)
  LOADLED = 2,        //next 2 bytes are uint16_t for starting pixel followed by
                      //3 bytes of pixel RGB data (1 pixel)
  CLEARFRAME = 10,    //set all pixel data to black
  TESTSEQ = 20        //fire the startup test sequence, all incoming serial data during 
                      //sequence will be discarded
};

uint8_t commandMode = 0;
//if 0, no command
//otherwise value equals controlSet value

uint32_t timeStamp = 0,   //used to space out matrix refreshes
        cmdFrameStamp = 0;  //watchdog to kill serial buffer fills if they take too long
uint16_t LEDStartPointer = 0, LEDCurrentPointer = 0, LEDEndPointer = 0;
boolean sizeRead = false;
boolean statLED = false;
uint8_t statLEDPin = 12;

#define PIN 2

void setup()
{
  Serial.begin(115200);
  
  buffer = (uint8_t*)malloc(64);
  
  FastSPI_LED.setLeds(NUM_LEDS);
  //FastSPI_LED.setChipset(CFastSPI_LED::SPI_SM16716);
  //FastSPI_LED.setChipset(CFastSPI_LED::SPI_TM1809);
  FastSPI_LED.setChipset(CFastSPI_LED::SPI_LPD6803);
  //FastSPI_LED.setChipset(CFastSPI_LED::SPI_HL1606);
  //FastSPI_LED.setChipset(CFastSPI_LED::SPI_595);
  //FastSPI_LED.setChipset(CFastSPI_LED::SPI_WS2801);
  Serial.println("setChipset");
  FastSPI_LED.setPin(PIN);
  Serial.println("setPin");
  
  FastSPI_LED.init();
  Serial.println("init");
  FastSPI_LED.start();
  Serial.println("start");
  
  pinMode(statLEDPin, OUTPUT);
  digitalWrite(statLEDPin, HIGH);

  leds = (struct CRGB*)FastSPI_LED.getRGBData(); 
  
  checkLEDs();
  
  timeStamp = millis();
}

void checkLEDs()
{  // one at a time
  for(int j = 0; j < 3; j++) { 
    for(int i = 0 ; i < NUM_LEDS; i++ ) {
      memset(leds, 0, NUM_LEDS * 3);
      switch(j) { 
        case 0: leds[i].r = 255; break;
        case 1: leds[i].g = 255; break;
        case 2: leds[i].b = 255; break;
      }
      FastSPI_LED.show();
      delay(10);
    }
  }

  // growing/receeding bars
  for(int j = 0; j < 3; j++) { 
    memset(leds, 0, NUM_LEDS * 3);
    for(int i = 0 ; i < NUM_LEDS; i++ ) {
      switch(j) { 
        case 0: leds[i].r = 255; break;
        case 1: leds[i].g = 255; break;
        case 2: leds[i].b = 255; break;
      }
      FastSPI_LED.show();
      delay(10);
    }
    for(int i = NUM_LEDS-1 ; i >= 0; i-- ) {
      switch(j) { 
        case 0: leds[i].r = 0; break;
        case 1: leds[i].g = 0; break;
        case 2: leds[i].b = 0; break;
      }
      FastSPI_LED.show();
      delay(1);
    }
  }
  
  // Fade in/fade out
  for(int j = 0; j < 3; j++ ) { 
    memset(leds, 0, NUM_LEDS * 3);
    for(int k = 0; k < 256; k++) { 
      for(int i = 0; i < NUM_LEDS; i++ ) {
        switch(j) { 
          case 0: leds[i].r = k; break;
          case 1: leds[i].g = k; break;
          case 2: leds[i].b = k; break;
        }
      }
      FastSPI_LED.show();
      delay(3);
    }
    for(int k = 255; k >= 0; k--) { 
      for(int i = 0; i < NUM_LEDS; i++ ) {
        switch(j) { 
          case 0: leds[i].r = k; break;
          case 1: leds[i].g = k; break;
          case 2: leds[i].b = k; break;
        }
      }
      FastSPI_LED.show();
      delay(3);
    }
  }  
}

void loop() 
{ 
  uint8_t tempByte = 0;
  CRGB tempCRBG;
  //Serial.println("loop");
  statLED = !statLED;
  digitalWrite(statLEDPin, statLED);  
  
  //if buffer data does not start with a command byte and 
  //no pending command, flush the byte
  if(commandMode == 0 && Serial.available() && Serial.peek() != CMDSTART) 
  {  Serial.read(); }
  
  //start of a new command frame if there is command byte 
  //and at least one more byte in buffer
  if(Serial.available() > 1 && Serial.peek() == CMDSTART)
  { 
    Serial.read(); //flush the command byte
    commandMode = Serial.read();
    switch(commandMode)
    {
      case 1:
        cmdFrameStamp = millis();
        commandMode = 1;
        sizeRead = false;  //LED start pointer not yet set
      break;
      case 2:
        cmdFrameStamp = millis();
        commandMode = 2;
        sizeRead = false;  //LED start pointer not yet set
      break;
      case 10:
        memset(leds, 0, NUM_LEDS * 3); //set all LEDs to 0 intensity
        commandMode = 0;
      break;
      case 20:
        checkLEDs();
        commandMode = 0;
      break;
      default:  //unrecognized command type
        commandMode = 0;
      break;
    }
  } 
    
  //LED update is started, LED start pointer not set but info received
  if(sizeRead == false && (commandMode == 1 || commandMode == 2) && Serial.available() > 1)
  {
    tempByte = Serial.read(); //unless panel is > 256 pixels, this should be 0
    LEDCurrentPointer = /*(tempByte << 8) + */Serial.read();
    if(commandMode == 2)  //expect 3 bytes of color data or one CRBG struct
      { LEDEndPointer = LEDCurrentPointer; }
    if(commandMode == 1)  //expect 48 bytes of color data or 16 CRBG structs
      { LEDEndPointer = LEDCurrentPointer + 15; }
    sizeRead = true;
    //Serial.write(byte(LEDCurrentPointer));
    //Serial.write(byte(LEDEndPointer));
    //Serial.write(255);
  }
  
  //if write pointer goes beyond the end of the LED array or has reached the expected
  //stop point or the entire update operation has exceeded 100ms, stop the write
  if(LEDCurrentPointer > NUM_LEDS ||
    LEDCurrentPointer > LEDEndPointer ||
    millis() - cmdFrameStamp > 100)
  { 
    sizeRead = false;
    commandMode = 0;
  }
  
  //consume incoming bytes for LED updates 3 bytes at a time
  if(sizeRead == true && Serial.available())
  {
    tempCRBG.r = Serial.read(); //get R
    tempCRBG.g = Serial.read(); //get G
    tempCRBG.b = Serial.read();
    *(leds + LEDCurrentPointer) = tempCRBG;
    LEDCurrentPointer++;
    //Serial.write(byte(LEDCurrentPointer));
    //Serial.write(byte(LEDEndPointer));
  }
  
  if((millis() - timeStamp) > 5) //wait at least 5 ms between matrix refreshes
  {
    FastSPI_LED.show();    //this is redundant(?) with lpd6803 chips
    timeStamp = millis();
  }
}

