////////////////////////////////////////////////////////////////////////////////
//
//                                  //
//
//      //////   //  //// //////    //    ////
//            // ////     //    //  //  //    //
//      //////// //       //    //  //  ////////
//    //      // //       //    //  //  //
//      //////   //       //////    //    ////
//      MIDI ARPEGGIATOR  //
//      hotchk155/2013    //
//
//    Revision History   
//    1.00  16Apr13  Baseline 
//    1.01  21Apr13  Improvements for MIDI thru control
//    1.02  26Apr13  Synch source/input lockout options
//    1.03  12May13  Fix issue with synch thru/change lockout blink rate
//    1.04  09Nov13  Support MIDI stop/continue on external synch
//
#define VERSION_HI  1
#define VERSION_LO  4

//
// INCLUDE FILES
//
#include <avr/interrupt.h>  
#include <avr/io.h>
#include <EEPROM.h>

////////////////////////////////////////////////////////////////////////////////
//
//
//
// LOW LEVEL USER INTERFACE FUNCTIONS
// - Drive the LEDs and read the switches
//
//
//
////////////////////////////////////////////////////////////////////////////////

// pin definitions
#define P_UI_HOLDSW      12

#define P_UI_IN_LED      8
#define P_UI_SYNCH_LED   16
#define P_UI_OUT_LED     17
#define P_UI_HOLD_LED     9

#define P_UI_CLK     5   //PD5
#define P_UI_DATA    7   //PD7
#define P_UI_STROBE  15  //PC1
#define P_UI_READ1   10  //PB2
#define P_UI_READ0   6   //PD6

#define DBIT_UI_CLK     0b00100000  // D5
#define DBIT_UI_DATA    0b10000000  // D7
#define CBIT_UI_STROBE  0b00000010  // C1

#define DBIT_UI_READ0   0b01000000  // D6
#define BBIT_UI_READ1   0b00000100  // B2

#define NO_VALUE (-1)
#define DEBOUNCE_COUNT 50

// Time in ms that counts as a long button press
#define UI_LONG_HOLD_TIME 1500

#define LED_BRIGHT 255
#define LED_MEDIUM 25
#define LED_DIM 1
#define LED_OFF 0

#define UI_IN_LED_TIME     20
#define UI_OUT_LED_TIME    20
#define UI_SYNCH_LED_TIME  5

#define UI_HOLD_PRESSED  0x01
#define UI_HOLD_HELD     0x02
#define UI_HOLD_CHORD    0x04
#define UI_HOLD_LOCKED   0x08

#define SYNCH_SOURCE_INTERNAL  1
#define SYNCH_SOURCE_INPUT     2
#define SYNCH_SOURCE_AUX       3

volatile byte uiLeds[16];
volatile char uiDataKey;
volatile char uiLastDataKey;
volatile char uiMenuKey;
volatile char uiLastMenuKey;
volatile byte uiDebounce;
volatile byte uiScanPosition;
volatile byte uiLedOffPeriod;
volatile byte uiFlashHold;

unsigned long uiUnflashInLED;
unsigned long uiUnflashOutLED;
unsigned long uiUnflashSynchLED;
byte uiHoldType;
unsigned long uiHoldPressedTime;
#define UI_DEBOUNCE_MS 100

// Enumerate the menu keys
// Columns A, B, C 
// Rows 1,2,3,4
enum {
  UI_KEY_A1  = 0,
  UI_KEY_A2  = 1,
  UI_KEY_A3  = 2,
  UI_KEY_A4  = 3,

  UI_KEY_B1  = 4,
  UI_KEY_B2  = 5,
  UI_KEY_B3  = 6,
  UI_KEY_B4  = 7,

  UI_KEY_C1  = 8,
  UI_KEY_C2  = 9,
  UI_KEY_C3  = 10,
  UI_KEY_C4  = 11  
};

////////////////////////////////////////////////////////////////////////////////
//
// INTERRUPT SERVICE ROUTINE FOR UPDATING THE DISPLAY AND READING KEYBOARD
//
////////////////////////////////////////////////////////////////////////////////
ISR(TIMER2_OVF_vect) 
{  
  // turn off LED strobe
  PORTC &= ~CBIT_UI_STROBE;

  // Do we need to wait until turning the next LED on? (this is used implement
  // crude PWM brightness control)
  if(uiLedOffPeriod)
  {
    // ok, we need another interrupt before we illuminate the next LED
    TCNT2 = 255 - uiLedOffPeriod;
    uiLedOffPeriod = 0;
  }
  else
  {         
    // used to flash hold light
    ++uiFlashHold;  

    // need to start scanning from the start of the led row again?
    if(uiScanPosition >= 15)
    {
      // Clock a single bit into the shift registers
      PORTD &= ~DBIT_UI_CLK;
      PORTD |= DBIT_UI_DATA;
      PORTD |= DBIT_UI_CLK;
    }

    // Shift the bit along (NB we need to shift it once 
    // before it will appear at shift reg output 0)
    PORTD &= ~DBIT_UI_DATA;
    PORTD &= ~DBIT_UI_CLK;
    PORTD |= DBIT_UI_CLK;

    // look up the button index
    byte buttonIndex = 15 - uiScanPosition;

    // does this LED need to be lit?    
    byte ledOnPeriod = uiLeds[buttonIndex];
    if(ledOnPeriod)
    {
      // ok enable led strobe line
      PORTC |= CBIT_UI_STROBE;      
    }     
    // set up the off period to make up a 255 clock tick cycle
    uiLedOffPeriod = 255 - ledOnPeriod;

    // check we're not debouncing the data entry keys
    if(!uiDebounce)
    {
      // is a data entry key pressed?
      if(!!(PINB & BBIT_UI_READ1))
      {
        // is it a new keypress?
        if(uiLastDataKey != buttonIndex)
        {
          // store it            
          uiDataKey = buttonIndex;
          uiLastDataKey = uiDataKey;
          uiDebounce = DEBOUNCE_COUNT;
        }
      }
      // has the previously pressed key been released?
      else if(buttonIndex == uiLastDataKey)
      {
        // no key is pressed
        uiLastDataKey = NO_VALUE;
      }
    }    
    else
    {
      // debouncing
      --uiDebounce;
    }

    // check for input at the menu keys
    // here we don't bother with a debounce
    if(!!(PIND & DBIT_UI_READ0))
    {
      // is a new key pressed
      if(buttonIndex != uiLastMenuKey)
      {
        // report it
        uiMenuKey = buttonIndex;
        uiLastMenuKey = uiMenuKey;
      }
    }
    else if(buttonIndex == uiLastMenuKey)
    {
      // previous key no longer pressed
      uiLastMenuKey = NO_VALUE;
    }

    if(!uiScanPosition)
      uiScanPosition = 15;
    else
      --uiScanPosition;

    TCNT2 = 255 - ledOnPeriod;
  }  
}

////////////////////////////////////////////////////////////////////////////////
// UI INIT
void uiInit()
{
  pinMode(P_UI_CLK, OUTPUT);     
  pinMode(P_UI_DATA, OUTPUT);     
  pinMode(P_UI_READ1, INPUT);     
  pinMode(P_UI_READ0, INPUT);     
  pinMode(P_UI_STROBE, OUTPUT);     

  pinMode(P_UI_HOLDSW, INPUT);
  pinMode(P_UI_IN_LED, OUTPUT);
  pinMode(P_UI_OUT_LED, OUTPUT);
  pinMode(P_UI_SYNCH_LED, OUTPUT);
  pinMode(P_UI_HOLD_LED, OUTPUT);

  // enable pullups
  digitalWrite(P_UI_HOLDSW, HIGH);

  for(int i=0;i<16;++i)
    uiLeds[i] = LED_OFF;  

  uiDataKey = NO_VALUE;
  uiLastDataKey = NO_VALUE;
  uiMenuKey = NO_VALUE;
  uiDebounce = 0;
  uiScanPosition = 15;
  uiLedOffPeriod = 0;
  uiUnflashInLED = 0;
  uiUnflashOutLED = 0;
  uiUnflashSynchLED = 0;
  uiHoldPressedTime = 0;
  uiHoldType = 0;
  uiFlashHold = 0;

  // start the interrupt to service the UI   
  TCCR2A = 0;
  TCCR2B = 1<<CS21 | 1<<CS20;
  TIMSK2 = 1<<TOIE2;
  TCNT2 = 0; 
}

////////////////////////////////////////////////////////////////////////////////
// SHOW FIRMWARE VERSION (BCD)
void uiShowVersion()
{
  if(digitalRead(P_UI_HOLDSW) == LOW) {
    uiLeds[0] =  !!((VERSION_HI/10)&0x8) ? LED_BRIGHT:LED_DIM;
    uiLeds[1] =  !!((VERSION_HI/10)&0x4) ? LED_BRIGHT:LED_DIM;
    uiLeds[2] =  !!((VERSION_HI/10)&0x2) ? LED_BRIGHT:LED_DIM;
    uiLeds[3] =  !!((VERSION_HI/10)&0x1) ? LED_BRIGHT:LED_DIM;

    uiLeds[4] =  !!((VERSION_HI%10)&0x8) ? LED_BRIGHT:LED_DIM;
    uiLeds[5] =  !!((VERSION_HI%10)&0x4) ? LED_BRIGHT:LED_DIM;
    uiLeds[6] =  !!((VERSION_HI%10)&0x2) ? LED_BRIGHT:LED_DIM;
    uiLeds[7] =  !!((VERSION_HI%10)&0x1) ? LED_BRIGHT:LED_DIM;

    uiLeds[8] =  !!((VERSION_LO/10)&0x8) ? LED_BRIGHT:LED_DIM;
    uiLeds[9] =  !!((VERSION_LO/10)&0x4) ? LED_BRIGHT:LED_DIM;
    uiLeds[10] = !!((VERSION_LO/10)&0x2) ? LED_BRIGHT:LED_DIM;
    uiLeds[11] = !!((VERSION_LO/10)&0x1) ? LED_BRIGHT:LED_DIM;

    uiLeds[12] = !!((VERSION_LO%10)&0x8) ? LED_BRIGHT:LED_DIM;
    uiLeds[13] = !!((VERSION_LO%10)&0x4) ? LED_BRIGHT:LED_DIM;
    uiLeds[14] = !!((VERSION_LO%10)&0x2) ? LED_BRIGHT:LED_DIM;
    uiLeds[15] = !!((VERSION_LO%10)&0x1) ? LED_BRIGHT:LED_DIM;

    while(digitalRead(P_UI_HOLDSW) == LOW);
  }
}

////////////////////////////////////////////////////////////////////////////////
// FLASH IN LED
void uiFlashInLED(unsigned long milliseconds)
{
  digitalWrite(P_UI_IN_LED, HIGH);
  uiUnflashInLED = milliseconds + UI_IN_LED_TIME;
}

////////////////////////////////////////////////////////////////////////////////
// FLASH OUT LED
void uiFlashOutLED(unsigned long milliseconds)
{
  digitalWrite(P_UI_OUT_LED, HIGH);
  uiUnflashOutLED = milliseconds + UI_OUT_LED_TIME;
}

////////////////////////////////////////////////////////////////////////////////
// FLASH SYNCH LED
void uiFlashSynchLED(unsigned long milliseconds)
{
  digitalWrite(P_UI_SYNCH_LED, HIGH);
  uiUnflashSynchLED = milliseconds + UI_SYNCH_LED_TIME;
}

////////////////////////////////////////////////////////////////////////////////
//  CLEAR ALL LEDS
void uiClearLeds()
{
  for(int i=0;i<16;++i)
    uiLeds[i] = 0;
}

////////////////////////////////////////////////////////////////////////////////
// SET A RANGE OF LEDS TO SAME STATUS
void uiSetLeds(int start, int len, byte newStatus)
{
  while(start < 16 && len-- > 0)
    uiLeds[start++] = newStatus;
}

////////////////////////////////////////////////////////////////////////////////
// RUN THE UI STATE MACHINE
void uiRun(unsigned long milliseconds)
{
  if(uiUnflashInLED && uiUnflashInLED < milliseconds)
  {
    digitalWrite(P_UI_IN_LED, LOW);
    uiUnflashInLED = 0;
  }
  if(uiUnflashOutLED && uiUnflashOutLED < milliseconds)
  {
    digitalWrite(P_UI_OUT_LED, LOW);
    uiUnflashOutLED = 0;
  }
  if(uiUnflashSynchLED && uiUnflashSynchLED < milliseconds)
  {
    digitalWrite(P_UI_SYNCH_LED, LOW);
    uiUnflashSynchLED = 0;
  }

  // Hold button logic  
  if(milliseconds < uiHoldPressedTime + UI_DEBOUNCE_MS)
  {
    // debouncing... ignore everything
  }
  else
    if(!digitalRead(P_UI_HOLDSW))
    {
      // button pressed... new press?
      if(!(uiHoldType & UI_HOLD_PRESSED))
      {
        // record and debounce it
        uiHoldType |= UI_HOLD_PRESSED;
        uiHoldPressedTime = milliseconds;
      }
      else if(!(uiHoldType & UI_HOLD_HELD) && (milliseconds > uiHoldPressedTime + UI_LONG_HOLD_TIME))
      {
        // record a long hold and set LOCK flag
        uiHoldType |= UI_HOLD_HELD;          
        uiHoldType |= UI_HOLD_LOCKED;          
        uiFlashHold = 0;
      }
    }  
    else
      if(!!(uiHoldType & UI_HOLD_PRESSED))
      {
        if(!(uiHoldType & UI_HOLD_HELD))
        {
          // release after short hold clears lock flag
          // and toggles hold
          if(!!(uiHoldType & UI_HOLD_LOCKED))
            uiHoldType &= ~UI_HOLD_LOCKED;
          else
            uiHoldType ^= UI_HOLD_CHORD;
        }
        uiHoldType &= ~UI_HOLD_PRESSED;
        uiHoldType &= ~UI_HOLD_HELD;
        uiHoldPressedTime = milliseconds;
      }    

  if(uiHoldType & UI_HOLD_LOCKED)
    digitalWrite(P_UI_HOLD_LED, !!(uiFlashHold & 0x80));
  else
    digitalWrite(P_UI_HOLD_LED, !!(uiHoldType & UI_HOLD_CHORD));   
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
//
//
// EEPROM PROXY FUNCTIONS
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

enum {
  EEPROM_INPUT_CHAN = 100,
  EEPROM_OUTPUT_CHAN,
  EEPROM_SYNCH_SOURCE,
  EEPROM_SYNCH_SEND,
  EEPROM_MIDI_OPTS
};

////////////////////////////////////////////////////////////////////////////////
// SET A VALUE IN EEPROM
void eepromSet(byte which, byte value)
{
  EEPROM.write(which, value);
}

////////////////////////////////////////////////////////////////////////////////
// GET A VALUE FROM EEPROM
byte eepromGet(byte which, byte minValue, byte maxValue, byte defaultValue)
{
  byte value = EEPROM.read(which);
  if(value == defaultValue)
    return value;
  if(value < minValue || value > maxValue)
  {
    value = defaultValue;
    eepromSet(which, value);
  }
  return value;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
//
//
// LOW LEVEL MIDI HANDLING
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// max ms we will wait for a mandatory midi parameter to arrive
#define MIDI_PARAM_TIMEOUT  50

// state variables
byte midiInRunningStatus;
byte midiOutRunningStatus;
byte midiNumParams;
byte midiParams[2];
char midiParamIndex;
byte midiSendChannel;
byte midiReceiveChannel;
byte midiOptions;

// midi options
#define MIDI_OPTS_SEND_CHMSG       0x01
#define MIDI_OPTS_PASS_INPUT_NOTES 0x02
#define MIDI_OPTS_PASS_INPUT_CHMSG 0x04
#define MIDI_OPTS_SYNCH_INPUT      0x08
#define MIDI_OPTS_SYNCH_AUX        0x10

#define MIDI_OPTS_MAX_VALUE        0x1F
#define MIDI_OPTS_DEFAULT_VALUE    (MIDI_OPTS_SEND_CHMSG|MIDI_OPTS_SYNCH_INPUT|MIDI_OPTS_SYNCH_AUX)

// macros
#define MIDI_IS_NOTE_ON(msg) ((msg & 0xf0) == 0x90)
#define MIDI_IS_NOTE_OFF(msg) ((msg & 0xf0) == 0x80)
#define MIDI_MK_NOTE (0x90 | midiSendChannel)

// realtime synch messages
#define MIDI_SYNCH_TICK     0xf8
#define MIDI_SYNCH_START    0xfa
#define MIDI_SYNCH_CONTINUE 0xfb
#define MIDI_SYNCH_STOP     0xfc

#define MIDI_OMNI           0xff

////////////////////////////////////////////////////////////////////////////////
// MIDI INIT
void midiInit()
{
  // init the serial port
  Serial.begin(31250);
  Serial.flush();

  midiInRunningStatus = 0;
  midiOutRunningStatus = 0;
  midiNumParams = 0;
  midiParamIndex = 0;
  midiSendChannel = eepromGet(EEPROM_OUTPUT_CHAN, 0, 15, 0);
  midiReceiveChannel = eepromGet(EEPROM_INPUT_CHAN, 0, 15, MIDI_OMNI);
  midiOptions = eepromGet(EEPROM_MIDI_OPTS, 0, MIDI_OPTS_MAX_VALUE, MIDI_OPTS_DEFAULT_VALUE);

}

////////////////////////////////////////////////////////////////////////////////
// MIDI WRITE
void midiWrite(byte statusByte, byte param1, byte param2, byte numParams, unsigned long milliseconds)
{
  if((statusByte & 0xf0) == 0xf0)
  {
    // realtime byte pass straight through
    Serial.write(statusByte);
  }
  else
  {
    // send channel message
    if(midiOutRunningStatus != statusByte)
    {
      Serial.write(statusByte);
      midiOutRunningStatus = statusByte;
    }
    if(numParams > 0)
      Serial.write(param1);
    if(numParams > 1)
      Serial.write(param2);    
  }

  // indicate activity
  uiFlashOutLED(milliseconds);
}

////////////////////////////////////////////////////////////////////////////////
// MIDI READ
byte midiRead(unsigned long milliseconds, byte passThru)
{
  extern byte synchToMIDI;

  // loop while we have incoming MIDI serial data
  while(Serial.available())
  {    
    // fetch the next byte
    byte ch = Serial.read();

    // REALTIME MESSAGE
    if((ch & 0xf0) == 0xf0)
    {
      switch(ch)
      {
      case MIDI_SYNCH_TICK:
        if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_INPUT))
          synchTick(SYNCH_SOURCE_INPUT);
        break;            
      case MIDI_SYNCH_START:
        if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_INPUT))
          synchStart(SYNCH_SOURCE_INPUT);
        break;
      case MIDI_SYNCH_CONTINUE:
        if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_INPUT))
          synchContinue(SYNCH_SOURCE_INPUT);
        break;
      case MIDI_SYNCH_STOP:
        if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_INPUT))
          synchStop(SYNCH_SOURCE_INPUT);
        break;
      }
    }      
    // CHANNEL STATUS MESSAGE
    else if(!!(ch & 0x80))
    {
      midiParamIndex = 0;
      midiInRunningStatus = ch; 
      switch(ch & 0xF0)
      {
      case 0xD0: //  Channel Pressure  1  pressure  
        midiNumParams = 1;
        break;    
      case 0x80: //  Note-off  2  key  velocity  
      case 0x90: //  Note-on  2  key  veolcity  
      case 0xA0: //  Aftertouch  2  key  touch  
      case 0xB0: //  Continuous controller  2  controller #  controller value  
      case 0xC0: //  Patch change  2  instrument #   
      case 0xE0: //  Pitch bend  2  lsb (7 bits)  msb (7 bits)  
      default:
        midiNumParams = 2;
        break;        
      }
    }    
    else if(midiInRunningStatus)
    {
      // gathering parameters
      midiParams[midiParamIndex++] = ch;
      if(midiParamIndex >= midiNumParams)
      {
        midiParamIndex = 0;

        // flash the LED
        uiFlashInLED(milliseconds);

        // is it a channel message for our channel?
        if(MIDI_OMNI == midiReceiveChannel ||
          (midiInRunningStatus & 0x0F) == midiReceiveChannel)
        {
          switch(midiInRunningStatus & 0xF0)
          {
          case 0x80: // note off
          case 0x90: // note on
            if(!!(midiOptions & MIDI_OPTS_PASS_INPUT_NOTES))
              midiWrite(midiInRunningStatus, midiParams[0], midiParams[1], midiNumParams, milliseconds);                
            return midiInRunningStatus; // return to the arp engine
          default:
            if(!!(midiOptions & MIDI_OPTS_PASS_INPUT_CHMSG))
              midiWrite(midiInRunningStatus, midiParams[0], midiParams[1], midiNumParams, milliseconds);                  
            // send to the new channel
            if(!!(midiOptions & MIDI_OPTS_SEND_CHMSG))
              midiWrite((midiInRunningStatus & 0xF0)|midiSendChannel, midiParams[0], midiParams[1], midiNumParams, milliseconds);
          }
        }
        else
        {                
          // send thru to output
          midiWrite(midiInRunningStatus, midiParams[0], midiParams[1], midiNumParams, milliseconds);
        }
      }
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
// MIDI SEND REALTIME
void midiSendRealTime(byte msg)
{
  Serial.write(msg);
}

////////////////////////////////////////////////////////////////////////////////
// MIDI PANIC
void midiPanic()
{  
  midiOutRunningStatus = 0;
  for(int i=0;i<128;++i)
    midiWrite(MIDI_MK_NOTE, i, 0, 2, millis());    
}

////////////////////////////////////////////////////////////////////////////////
// CLEAR RUUNNING STATUS
void midiClearRunningStatus()
{
  midiOutRunningStatus = 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
//
//
// SYNCHRONISATION
// - Functions for managing the beat clock
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// variables
volatile byte synchToMIDI;                     // whether we're synching to MIDI
volatile byte synchSendMIDI;                   // whether the MIDI clock is to be sent
volatile unsigned long synchTickCount;         // tick count
volatile int synchPlayRate;                    // ratio of ticks per arp note
volatile unsigned long synchPlayIndex;         // arp note count
volatile byte synchPlayAdvance;                // flag to indicate that next arp note can be played
volatile int synchBPM;                         // internal synch bpm
volatile float synchInternalTickPeriod;        // internal synch millseconds per tick
volatile float synchNextInternalTick;          // internal synch next tick time

volatile unsigned long synchLastStepTime;      // the last step time
volatile unsigned long synchStepPeriod;          // period between steps


volatile byte synchBeat;                       // flag for flashing the SYNCH lamp
volatile byte synchFlags;                  // synch events to send
volatile char synchTicksToSend;                // ticks to send

// PIN DEFS (From PIC MCU servicing MIDI SYNCH input)
#define P_SYNCH_TICK     2
#define P_SYNCH_RESTART  3
#define P_SYNCH_RUN      4

#define DBIT_SYNCH_RUN   (1<<4) // port D bit that indicates a run status on AUX SYNCH port

#define MILLISECONDS_PER_MINUTE 60000
#define TICKS_PER_QUARTER_NOTE 24
#define SYNCH_DEFAULT_BPM 120

#define SYNCH_SEND_START               0x01    // This flag indicates that a START message needs to be sent to slaves
#define SYNCH_SEND_STOP                0x02    // This flag indicates that a STOP message needs to be sent to slaves
#define SYNCH_SEND_CONTINUE            0x04    // This flag indicates that a CONTINUE message needs to be sent to slaves
#define SYNCH_RESET_NEXT_STEP_TIME     0x08    // This flag indicates that the next step will be timed from now
#define SYNCH_INPUT_RUNNING            0x10    // This flag indicates that the MIDI INPUT synch is in a running state
#define SYNCH_RESTART_ON_BEAT          0x20    // This flag indicates that the sequence will restart at the next beat
#define SYNCH_AUX_RUNNING              0x40    // This flag indicates that the AUX SYNCH input in a running state 


// Values for synchRate
enum
{
  SYNCH_RATE_1    = 96,
  SYNCH_RATE_2D   = 72,
  SYNCH_RATE_2    = 48,
  SYNCH_RATE_4D   = 36,
  SYNCH_RATE_2T   = 32,  
  SYNCH_RATE_4    = 24,
  SYNCH_RATE_8D   = 18,
  SYNCH_RATE_4T   = 16,
  SYNCH_RATE_8    = 12,
  SYNCH_RATE_16D  = 9,
  SYNCH_RATE_8T   = 8,
  SYNCH_RATE_16   = 6,
  SYNCH_RATE_16T  = 4,
  SYNCH_RATE_32   = 3
};

//////////////////////////////////////////////////////////////////////////
// HANDLER FOR A MIDI CLOCK TICK
// This could be internally generated or come from an external source
void synchTick(byte source)
{
  ++synchTickCount;
  if(!(synchTickCount % synchPlayRate))//ready for next step?
  {
    // store step length in ms.. this will be used
    // when calculating step length
    unsigned long ms = millis();
    if(synchLastStepTime > 0)
      synchStepPeriod = ms - synchLastStepTime;
    synchLastStepTime = ms;

    if((SYNCH_SOURCE_INPUT == source) && !(synchFlags&SYNCH_INPUT_RUNNING))
    {
      // do not advance playback - midi in synch is stopped
    }
    else if((SYNCH_SOURCE_AUX == source) && !(synchFlags&SYNCH_AUX_RUNNING))
    {
      // do not advance playback - aux synch is stopped
    }
    else 
    {
      if(synchFlags & SYNCH_RESTART_ON_BEAT)
      {
        synchPlayIndex = 0;
      }
      else 
      {
        synchPlayIndex++;
      }
      synchPlayAdvance = 1;
    }
  }
  synchFlags &= ~SYNCH_RESTART_ON_BEAT;
  
  if(!(synchTickCount % TICKS_PER_QUARTER_NOTE))
    synchBeat = 1;  

  if(synchSendMIDI)
    synchTicksToSend++;
}

//////////////////////////////////////////////////////////////////////////
// RESTART PLAY FROM START OF SEQUENCE IMMEDIATELY
void synchRestartSequence()
{
  synchLastStepTime = millis();
  synchTickCount = 0;
  synchPlayIndex = 0;
  synchPlayAdvance = 1;
  synchTicksToSend = 0;
  synchBeat = 1;  
}

//////////////////////////////////////////////////////////////////////////
// START PLAYING
void synchStart(byte source)
{
  if(SYNCH_SOURCE_INPUT == source) synchFlags|=SYNCH_INPUT_RUNNING;
  if(SYNCH_SOURCE_AUX == source) synchFlags|=SYNCH_AUX_RUNNING;
  synchFlags |= SYNCH_SEND_START;  
  synchRestartSequence();
}

//////////////////////////////////////////////////////////////////////////
// STOP PLAYING
void synchStop(byte source)
{
  if(SYNCH_SOURCE_INPUT == source) synchFlags&=~SYNCH_INPUT_RUNNING;
  if(SYNCH_SOURCE_AUX == source) synchFlags&=~SYNCH_AUX_RUNNING;
  synchFlags |= SYNCH_SEND_STOP;
}
//////////////////////////////////////////////////////////////////////////
// CONTINUE PLAYING FROM CURRENT POSITION
void synchContinue(byte source)
{
  if(SYNCH_SOURCE_INPUT == source) synchFlags|=SYNCH_INPUT_RUNNING;
  if(SYNCH_SOURCE_AUX == source) synchFlags|=SYNCH_AUX_RUNNING;
  synchFlags |= SYNCH_SEND_CONTINUE;
}

//////////////////////////////////////////////////////////////////////////
//
// synchReset_ISR
// Called at start of bar
// 
//////////////////////////////////////////////////////////////////////////
ISR(synchReset_ISR)
{
  if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_AUX))     
    synchStart(SYNCH_SOURCE_AUX);
}

//////////////////////////////////////////////////////////////////////////
//
// synchTick_ISR
// Called on midi synch
// 
//////////////////////////////////////////////////////////////////////////
ISR(synchTick_ISR)
{
  // For AUX midi tick to be actioned we need to synch to AUX MIDI and we need a RUNNING status
  if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_AUX))
    synchTick(SYNCH_SOURCE_AUX);
}

//////////////////////////////////////////////////////////////////////////
// SET TEMPO
void synchSetTempo(int bpm)
{
  synchBPM = bpm;
  synchInternalTickPeriod = (float)MILLISECONDS_PER_MINUTE/(bpm * TICKS_PER_QUARTER_NOTE);
}

////////////////////////////////////////////////////////////////////////////////
// SYNCH INIT
void synchInit()
{
  // by default don't synch
  synchToMIDI = eepromGet(EEPROM_SYNCH_SOURCE,0,1,0);

  // by default do not send synch
  synchSendMIDI = eepromGet(EEPROM_SYNCH_SEND,0,1,0);
  synchTicksToSend = 0;
  synchFlags = 0;

  // set default play rate
  synchPlayRate = SYNCH_RATE_16;

  synchLastStepTime = 0;
  synchStepPeriod = 0;

  // reset the counters
  synchRestartSequence();

  // initialise internal synch generator
  synchSetTempo(SYNCH_DEFAULT_BPM);
  synchNextInternalTick = 0;

  pinMode(P_SYNCH_TICK, INPUT);
  pinMode(P_SYNCH_RESTART, INPUT);
  pinMode(P_SYNCH_RUN, INPUT);

  // weak pull-ups
  digitalWrite(P_SYNCH_TICK, HIGH);
  digitalWrite(P_SYNCH_RESTART, HIGH);
  digitalWrite(P_SYNCH_RUN, HIGH);

  attachInterrupt(0, synchReset_ISR, RISING);
  attachInterrupt(1, synchTick_ISR, RISING);
  interrupts();
}

////////////////////////////////////////////////////////////////////////////////
// SYNCH RUN
void synchRun(unsigned long milliseconds)
{
  byte auxRunning = !!(PIND & DBIT_SYNCH_RUN); // see if aux port is running
  if(synchToMIDI && !!(midiOptions & MIDI_OPTS_SYNCH_AUX)) // Check if we are interested
  {
    if(auxRunning && !(synchFlags & SYNCH_AUX_RUNNING)) // transition STOP->RUN
      synchContinue(SYNCH_SOURCE_AUX);
    else if(!auxRunning && !!(synchFlags & SYNCH_AUX_RUNNING)) // transition RUN->STOP
      synchStop(SYNCH_SOURCE_AUX);
  }  
  synchFlags = auxRunning? (synchFlags|SYNCH_AUX_RUNNING) : (synchFlags&~SYNCH_AUX_RUNNING);

  // are we synching to MIDI?
  if(!synchToMIDI)
  {      
    if(synchFlags & SYNCH_RESET_NEXT_STEP_TIME)
    {
      synchNextInternalTick = milliseconds + synchInternalTickPeriod;
    }
    // need to generate our own ticks
    else if(synchNextInternalTick < milliseconds)
    {
      synchTick(SYNCH_SOURCE_INTERNAL);
      synchNextInternalTick += synchInternalTickPeriod;
      if(synchNextInternalTick < milliseconds)
        synchNextInternalTick = milliseconds + synchInternalTickPeriod;
    }
  }
  synchFlags &= ~SYNCH_RESET_NEXT_STEP_TIME;//clear flag

  if(synchSendMIDI)//Are we sending MIDI synch to slaves?
  {
    if(synchFlags & SYNCH_SEND_STOP)//Stop
    {
      midiSendRealTime(MIDI_SYNCH_STOP);
      synchTicksToSend = 0;
    }
    else if(synchFlags & SYNCH_SEND_START)//Start
    {
      midiSendRealTime(MIDI_SYNCH_START);
    }  
    else if(synchFlags & SYNCH_SEND_CONTINUE)//Continue?
    {
      midiSendRealTime(MIDI_SYNCH_CONTINUE);
    }  

    if(synchTicksToSend>0) // need to send a tick?
    {
      synchTicksToSend--;
      midiSendRealTime(MIDI_SYNCH_TICK);
    }
  }
  else
  {
    synchTicksToSend = 0; // ensure no ticks will be sent
  }
  synchFlags &= ~(SYNCH_SEND_STOP|SYNCH_SEND_START|SYNCH_SEND_CONTINUE); // clear all realtime msg flags


  // check if we need to report a beat
  if(synchBeat)
  {
    uiFlashSynchLED(milliseconds);
    synchBeat = 0;
  } 
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
//
//
//
// ARPEGGIATOR FUNCTIONS
//
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Macro defs
#define ARP_MAX_CHORD 12
#define ARP_MAKE_NOTE(note, vel) ((((unsigned int)vel)<<8)|(note))
#define ARP_GET_NOTE(x) ((x)&0x7f)
#define ARP_GET_VELOCITY(x) (((x)>>8)&0x7f)
#define ARP_MAX_SEQUENCE 60
#define ARP_NOTE_HELD 0x8000

// Values for arpType
enum 
{
  ARP_TYPE_UP = 0,
  ARP_TYPE_DOWN,
  ARP_TYPE_UP_DOWN,
  ARP_TYPE_RANDOM,
  ARP_TYPE_MANUAL
};

// Values for arpInsertMode
enum
{
  ARP_INSERT_OFF = 0,
  ARP_INSERT_HI,
  ARP_INSERT_LOW,
  ARP_INSERT_3_1,
  ARP_INSERT_4_2
};

// ARP PARAMETERS
byte arpType;              // arpeggio type
char arpOctaveShift;       // octave transpose
byte arpOctaveSpan;        // number of octaves to span with the arpeggio
byte arpInsertMode;        // additional note insertion mode
byte arpVelocityMode;      // (0 = original, 1 = override)
byte arpVelocity;          // velocity 
byte arpGateLength;        // gate length (0 = tie notes)
char arpTranspose;         // up/down transpose

// CHORD INFO - notes held by user
unsigned int arpChord[ARP_MAX_CHORD];
int arpChordLength;        // number of notes in the chord
int arpNotesHeld;          // number of notes physically held

// ARPEGGIO SEQUENCE - the arpeggio build from chord/inserts etc
unsigned int arpSequence[ARP_MAX_SEQUENCE];
int arpSequenceLength;     // number of notes in the sequence

// NOTE PATTERN - the rythmic pattern of played/muted notes
byte arpPattern[ARP_MAX_SEQUENCE];
byte arpPatternLength;   // user-defined pattern length (1-16)
byte arpPatternIndex;    // position in the pattern (for display)

byte arpRefresh;  // whether the pattern index is changed

// STOP NOTE - remembers which (single) note is playing
// and when it should be stopped
byte arpStopNote;
unsigned long arpStopNoteTime;

// used to time the length of a step
unsigned long arpLastPlayAdvance;

// ARP STATUS FLAGS
byte arpRebuild;          // whether the sequence needs to be rebuilt

////////////////////////////////////////////////////////////////////////////////
// ARP INIT
void arpInit()
{
  //  arpHold = 0;
  arpType = ARP_TYPE_UP;
  arpOctaveShift = 0;
  arpOctaveSpan = 1;
  arpInsertMode = ARP_INSERT_OFF;
  arpVelocity = 127;
  arpVelocityMode = 1;
  arpChordLength = 0;
  arpNotesHeld = 0;
  arpPatternLength = 16;
  arpRefresh = 0;
  arpRebuild = 0;
  arpStopNote = 0;
  arpGateLength = 10;
  arpSequenceLength = 0;
  arpLastPlayAdvance = 0;
  arpTranspose = 0;

  // the pattern starts with all beats on
  for(int i=0;i<16;++i)
    arpPattern[i] = 1;
}

////////////////////////////////////////////////////////////////////////////////
// CLEAR CHORD
void arpClear()
{
  arpChordLength = 0;
  arpRebuild = 1;
}

////////////////////////////////////////////////////////////////////////////////
// COPY CHORD
void arpCopyChord(int *dest)
{
  int i;
  for(i=0; i<arpChordLength; ++i)
    dest[i] = arpChord[i];
}

////////////////////////////////////////////////////////////////////////////////
// SORT NOTES OF CHORD
// Crappy bubblesort.. but there are not too many notes
void arpSortChord(int *dest)
{
  arpCopyChord(dest);
  byte sorted = 0;
  while(!sorted)
  {
    sorted = 1;
    for(int i=0; i<arpChordLength-1; ++i)
    {
      if(ARP_GET_NOTE(dest[i]) > ARP_GET_NOTE(dest[i+1]))
      {
        int t = dest[i];
        dest[i] = dest[i+1];
        dest[i+1] = t;
        sorted = 0;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// RANDOMIZE CHORD
void arpRandomizeChord(int *dest)
{
  int i,j;

  // clear destination buffer
  for(i=0; i<arpChordLength; ++i)
    dest[i] = 0;

  // loop through the source chord
  for(i=0; i<arpChordLength; ++i)
  {
    // loop until we find a place to 
    // put this note in dest buffer
    for(;;)
    {
      // look for a place
      j = random(arpChordLength);
      if(!dest[j])
      {
        // its empty, so we can use it
        dest[j] = arpChord[i];
        break;
      }
    }        
  }
}

////////////////////////////////////////////////////////////////////////////////
// BUILD A NEW SEQUENCE
void arpBuildSequence()
{
  // sequence is empty if no chord notes present
  arpSequenceLength=0;
  if(!arpChordLength)
    return;

  // sort the chord info if needed
  int chord[ARP_MAX_CHORD];
  if(arpType == ARP_TYPE_UP || arpType == ARP_TYPE_DOWN || arpType == ARP_TYPE_UP_DOWN)
    arpSortChord(chord);
  else
    arpCopyChord(chord);

  int tempSequence[ARP_MAX_SEQUENCE];
  int tempSequenceLength = 0;        
  int highestNote = ARP_MAKE_NOTE(0,0);
  int lowestNote = ARP_MAKE_NOTE(127,0);    

  // This outer loop allows us two passes for UP-DOWN mode
  int nextPass = 1;    
  while(nextPass && 
    tempSequenceLength < ARP_MAX_SEQUENCE)
  {
    // this loop is for the octave span
    int octaveCount;
    for(octaveCount = 0; 
      octaveCount < arpOctaveSpan && tempSequenceLength < ARP_MAX_SEQUENCE; 
      ++octaveCount)
    {
      // Set up depending on arp type
      int transpose;
      int chordIndex;
      int lastChordIndex;
      int chordIndexDelta;      
      switch(arpType)
      {
      case ARP_TYPE_RANDOM:
        arpRandomizeChord(chord);
        // fall thru
      case ARP_TYPE_UP:
      case ARP_TYPE_MANUAL:
        chordIndex = 0;
        lastChordIndex = arpChordLength - 1;
        transpose = arpTranspose + 12 * (arpOctaveShift + octaveCount);    
        chordIndexDelta = 1;
        nextPass = 0;
        break;          

      case ARP_TYPE_DOWN:
        chordIndex = arpChordLength - 1;
        ;
        lastChordIndex = 0;
        transpose = arpTranspose + 12 * (arpOctaveShift + arpOctaveSpan - octaveCount - 1);    
        chordIndexDelta = -1;
        nextPass = 0;
        break;          

      case ARP_TYPE_UP_DOWN:        
        if(nextPass == 1)
        {
          // going up we can play all the notes
          chordIndex = 0;
          lastChordIndex = arpChordLength - 1;
          chordIndexDelta = 1;
          transpose = arpTranspose + 12 * (arpOctaveShift + octaveCount);    
          if(octaveCount == arpOctaveSpan - 1)
            nextPass = 2;
        }
        else
        {
          // GOING DOWN!
          // Is the range just one octave?
          if(arpOctaveSpan == 1)
          {
            // On the way down we don't play top or bottom notes of the chord
            chordIndex = arpChordLength - 2;
            lastChordIndex = 1;
            nextPass = 0;
          }
          // are we on the top octave of the descent?
          else if(octaveCount == 0)
          {
            // the top note is skipped, the bottom note can be played
            chordIndex = arpChordLength - 2;
            lastChordIndex = 0;
          }
          // are we on the bottom octave of the descent?
          else if(octaveCount == arpOctaveSpan - 1)
          {
            // top note can be played but bottom note is not
            chordIndex = arpChordLength - 1;
            lastChordIndex = 1;

            // this the the last octave to play
            nextPass = 0;
          }
          else
          {
            // this is not first or last octave of the descent, so there
            // is no need to skip any of the notes
            chordIndex = arpChordLength - 1;
            lastChordIndex = 0;
          }
          transpose = arpTranspose + 12 * (arpOctaveShift + arpOctaveSpan - octaveCount - 1);    
          chordIndexDelta = -1;
        }
        break;
      }        

      // Write notes from the chord into the arpeggio sequence
      while(chordIndex >= 0 && 
        chordIndex < arpChordLength && 
        tempSequenceLength < ARP_MAX_SEQUENCE)
      {
        // fetch the current note
        int note = ARP_GET_NOTE(chord[chordIndex]);
        byte velocity = ARP_GET_VELOCITY(chord[chordIndex]);

        // transpose as needed
        note += transpose;
        while(note>127)
          note -= 12;
        while(note<0)
          note += 12;          
        int newNote = ARP_MAKE_NOTE(note, velocity);

        // track lowest and highest notes
        if(note > ARP_GET_NOTE(highestNote))
          highestNote = newNote;
        if(note < ARP_GET_NOTE(lowestNote))
          lowestNote = newNote;

        // insert into sequence
        tempSequence[tempSequenceLength++] = newNote;

        // have we reached the last note we want?
        if(chordIndex == lastChordIndex)
          break;

        // skip to next note in the chord
        chordIndex += chordIndexDelta;
      }      
    }           
  }  


  // we have the expanded sequence for one octave... now we need to 
  // perform any necessary note insertions
  int i, j;
  arpSequenceLength = 0;
  switch(arpInsertMode)
  {
  case ARP_INSERT_OFF:
    for(i=0; i<tempSequenceLength; ++i)
      arpSequence[arpSequenceLength++] = tempSequence[i];
    break;
  case ARP_INSERT_HI:
    for(i=0; i<tempSequenceLength && arpSequenceLength < ARP_MAX_SEQUENCE; ++i)
    {
      if(tempSequence[i] != highestNote)
      {
        arpSequence[arpSequenceLength++] = highestNote;
        arpSequence[arpSequenceLength++] = tempSequence[i];
      }
    }
    break;
  case ARP_INSERT_LOW:
    for(i=0; i<tempSequenceLength && arpSequenceLength < ARP_MAX_SEQUENCE; ++i)
    {
      if(tempSequence[i] != lowestNote)
      {
        arpSequence[arpSequenceLength++] = lowestNote;
        arpSequence[arpSequenceLength++] = tempSequence[i];
      }
    }
    break;
  case ARP_INSERT_3_1: // 3 steps forward and one back 012123234345456
    i = 0;
    j = 0;
    while(i<tempSequenceLength && arpSequenceLength < ARP_MAX_SEQUENCE)
    {
      arpSequence[arpSequenceLength++] = tempSequence[i];
      if(!(++j%3))
        i--;
      else
        i++;
    }
    break;
  case ARP_INSERT_4_2: // 4 steps forward and 2 back 0123123423453456
    i = 0;
    j = 0;
    while(i<tempSequenceLength && arpSequenceLength < ARP_MAX_SEQUENCE)
    {
      arpSequence[arpSequenceLength++] = tempSequence[i];
      if(!(++j%4))
        i-=2;
      else
        i++;
    }
    break;
  }
}  

////////////////////////////////////////////////////////////////////////////////
// READ THE MIDI INPUT AND UPDATE THE CHORD BUFFER
void arpReadInput(unsigned long milliseconds)
{
  int i;
  char noteIndexInChord; 

  // we may have multiple notes to read
  for(;;)
  {
    // read the MIDI port
    byte msg = midiRead(milliseconds, 0);      
    if(!msg || !!(uiHoldType & UI_HOLD_LOCKED))
      break;
    byte note = midiParams[0];
    byte velocity = midiParams[1];

    // NOTE ON MESSAGE
    if(MIDI_IS_NOTE_ON(msg) && velocity && note)
    {
      // scan the current chord for this note
      // to see if it is already part of the chord      
      noteIndexInChord = -1;
      arpNotesHeld = 0;
      for(i=0;i<arpChordLength;++i)
      {        
        if(ARP_GET_NOTE(arpChord[i])== note)
          noteIndexInChord = i;
        if(arpChord[i] & ARP_NOTE_HELD)
          arpNotesHeld++;
      }

      // is the note already part of the current chord?
      if(noteIndexInChord >= 0 && arpNotesHeld)
      {
        // Mark the key as held. There is no change to the arpeggio
        if(!(arpChord[noteIndexInChord] & ARP_NOTE_HELD))
        {        
          arpChord[noteIndexInChord] |= ARP_NOTE_HELD;
          arpNotesHeld++;
        }
      }
      else 
      {
        // if its the first note of a new chord then
        // we need to restart play
        if(!arpNotesHeld)
        {
          arpChordLength = 0;
          if(!!(uiHoldType & UI_HOLD_CHORD))
            synchFlags |= SYNCH_RESTART_ON_BEAT; // wait till next beat before restarting
          else
            synchRestartSequence();
        }

        // insert the new note into the chord                   
        if(arpChordLength < ARP_MAX_CHORD-1)
        {        
          arpChord[arpChordLength] = ARP_MAKE_NOTE(note,velocity);
          arpChord[arpChordLength] |= ARP_NOTE_HELD;
          arpChordLength++;
          arpNotesHeld++;

          // flag that the arp sequence needs to be rebuilt
          arpRebuild = 1;          
        }  
      }
    }
    // NOTE OFF MESSAGE
    // (NB might be note on with zero velocity)
    else if(MIDI_IS_NOTE_ON(msg) || MIDI_IS_NOTE_OFF(msg))
    {
      // unflag the note as "held" and count how many notes of
      // the chord are actually still held
      noteIndexInChord = -1;
      arpNotesHeld = 0;
      for(i=0; i<arpChordLength; ++i)
      {
        // did we find the released key in the chord?
        if(ARP_GET_NOTE(arpChord[i]) == note)
        {
          arpChord[i] &= ~ARP_NOTE_HELD;
          noteIndexInChord = i;
        }
        else if(arpChord[i] & ARP_NOTE_HELD)
        {
          arpNotesHeld++;
        }
      }

      // should the note be removed from the chord?
      if(!(uiHoldType & UI_HOLD_CHORD) && noteIndexInChord >= 0)
      {     

        // shift higher notes down one position
        // to remove the released note
        for(i = noteIndexInChord;i < arpChordLength-1; ++i)
          arpChord[i] = arpChord[i+1];

        // rebuild the sequence
        --arpChordLength;
        arpRebuild = 1;
      }
    }
  }

  // check if the hold switch is released while
  // there are notes being held
  if(!(uiHoldType & UI_HOLD_CHORD) && !arpNotesHeld && arpChordLength)
    arpClear();
}


////////////////////////////////////////////////////////////////////////////////
// RUN ARPEGGIATOR
void arpRun(unsigned long milliseconds)
{  
  // update the chord based on user input
  arpReadInput(milliseconds);

  // see if user has changed a setting that would mean the
  // sequence needs to be rebuilt
  if(arpRebuild)
  {
    // rebuild the sequence 
    arpBuildSequence();                
    arpRebuild = 0;

    // ensure that the receiving device gets the
    // initial note/channel on status for the arpeggio
    midiClearRunningStatus();
  }

  // have we updated the play position?
  if(synchPlayAdvance && arpSequenceLength && arpPatternLength)
  {                 
    // get the index into the arpeggio sequence
    int sequenceIndex = synchPlayIndex % arpSequenceLength;

    // and the index into the pattern
    arpPatternIndex = synchPlayIndex % arpPatternLength;

    // check there is a note not a rest at this 
    // point in the pattern
    if(arpPattern[arpPatternIndex])
    {
      byte note = ARP_GET_NOTE(arpSequence[sequenceIndex]);
      byte velocity = arpVelocityMode? arpVelocity : ARP_GET_VELOCITY(arpSequence[sequenceIndex]);        

      // start the note playing
      if(note > 0)
        midiWrite(MIDI_MK_NOTE, note, velocity, 2, milliseconds);

      // if the previous note is still playing then stop it
      // (should be the case only for "tie" mode)
      if(arpStopNote && arpStopNote != note)
      {
        midiWrite(MIDI_MK_NOTE, arpStopNote, 0, 2, milliseconds);
      }

      // need to work out the gate length for this note
      arpStopNote = note;
      if(arpGateLength)
      {              
        // Set the stop period to occur after a certain
        arpStopNoteTime = milliseconds + (synchStepPeriod * arpGateLength) / 15;
      }
      else
      {
        // note till play till the next one starts
        arpStopNoteTime = 0;               
      }
      arpLastPlayAdvance = milliseconds;
    }

    // need to update the arp display
    arpRefresh = 1;
    synchPlayAdvance = 0;
  }
  // check if a note needs to be stopped.. either at end of playing or if there is no sequence
  // and we're in tied note mode
  else if(arpStopNote && (
  ((arpStopNoteTime && arpStopNoteTime < milliseconds) || 
    (!arpStopNoteTime && !arpSequenceLength))))
  {
    // stop the ringing note
    midiWrite(MIDI_MK_NOTE, arpStopNote, 0, 2, milliseconds);
    arpStopNote = 0;
    arpStopNoteTime = 0;
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
//
//
//
// CONTROL SURFACE
//
//
//
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


// map the edit keys to physical buttons
//
// PATN  LEN   MODE
// SHFT  SPAN  RATE
// VELO  GATE  INS
// TMPO  CHAN  TRAN
enum {
  EDIT_MODE_PATTERN          = UI_KEY_A1,
  EDIT_MODE_PATTERN_LENGTH   = UI_KEY_B1,
  EDIT_MODE_ARP_TYPE         = UI_KEY_C1,
  EDIT_MODE_OCTAVE_SHIFT     = UI_KEY_A2,
  EDIT_MODE_OCTAVE_SPAN      = UI_KEY_B2,
  EDIT_MODE_RATE             = UI_KEY_C2,
  EDIT_MODE_VELOCITY         = UI_KEY_A3,
  EDIT_MODE_GATE_LENGTH      = UI_KEY_B3,
  EDIT_MODE_INSERT           = UI_KEY_C3,
  EDIT_MODE_TEMPO_SYNCH      = UI_KEY_A4,
  EDIT_MODE_CHANNEL          = UI_KEY_B4,
  EDIT_MODE_TRANSPOSE        = UI_KEY_C4
};

// Time in ms when we go back to pattern
// edit mode after last button press
#define EDIT_REVERT_TIME 10000

enum {
  EDIT_NO_PRESS = 0,
  EDIT_PRESS,        // button pressed
  EDIT_LONG_PRESS,   // button held for long hold threshold
  EDIT_LONG_HOLD     // button held for more than long hold threshold
};

// current editing mode
byte editMode;

// track the revert time
unsigned long editRevertTime;

// track when a menu button is held for a long period of time
unsigned long editLongHoldTime;
byte editPressType;

unsigned long editTapTempoTime;

////////////////////////////////////////////////////////////////////////////////
// INIT EDITING
void editInit()
{
  editMode = EDIT_MODE_PATTERN;
  editPressType = EDIT_NO_PRESS;
  editRevertTime = 1;  // force a display refresh on startup
  editLongHoldTime = 0;
  editTapTempoTime = 0;
}

////////////////////////////////////////////////////////////////////////////////
// EDIT PATTERN
void editPattern(char keyPress, byte forceRefresh)
{
  if(keyPress != NO_VALUE)
  {
    arpPattern[keyPress] = !arpPattern[keyPress];
    forceRefresh = 1;
  }

  if(forceRefresh || arpRefresh)
  {    
    // copy the leds
    for(int i=0; i<16; ++i)
      uiLeds[i] = arpPattern[i] ? LED_MEDIUM : LED_OFF;

    // only display the play position if we have a sequence
    if(arpSequenceLength)    
      uiLeds[arpPatternIndex] = LED_BRIGHT;

    // reset the flag
    arpRefresh = 0;
  }
}

/////////////////////////////////////////////////////
// EDIT PATTERN LENGTH
void editPatternLength(char keyPress, byte forceRefresh)
{
  int i;
  if(keyPress >= 0 && keyPress <= 15)
  {
    arpPatternLength = keyPress + 1;
    forceRefresh = 1;
  }

  if(forceRefresh)
  {    
    uiClearLeds();
    uiSetLeds(0, arpPatternLength, LED_DIM);
    uiLeds[arpPatternLength-1] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT ARPEGGIO TYPE
void editArpType(char keyPress, byte forceRefresh)
{
  int i;
  switch(keyPress)
  {
  case 0: 
  case 1:  
  case 2:  
  case 3: 
  case 4:
    arpType = keyPress;
    arpRebuild = 1;
    forceRefresh = 1;
    break;
  case 13: 
    arpPatternLength = 8+random(8);
    for(i = 0;i<16;++i) arpPattern[i] = random(2);
    break;
  case 14:
    for(i = 0;i<16;++i) arpPattern[i] = 0;
    arpPatternLength = 16;
    break;
  case 15:
    for(i = 0;i<16;++i) arpPattern[i] = 1;
    arpPatternLength = 16;
    break;
  } 

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 5, LED_MEDIUM);
    uiLeds[arpType] = LED_BRIGHT;
    uiSetLeds(13, 3, LED_MEDIUM);
  }
}

/////////////////////////////////////////////////////
// EDIT OCTAVE SHIFT
void editOctaveShift(char keyPress, byte forceRefresh)
{
  if(keyPress >= 0 && keyPress <= 6)
  {
    arpOctaveShift = keyPress - 3;
    arpRebuild = 1;
    forceRefresh = 1;
  }

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 7, LED_DIM);
    uiLeds[3] = LED_MEDIUM;
    uiLeds[3 + arpOctaveShift] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT OCTAVE SPAN
void editOctaveSpan(char keyPress, byte forceRefresh)
{
  if(keyPress >= 0 && keyPress <= 3)
  {
    arpOctaveSpan = keyPress + 1;
    arpRebuild = 1;
    forceRefresh = 1;
  }

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 4, LED_DIM);
    uiLeds[arpOctaveSpan - 1] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT ARP RATE
void editRate(char keyPress, byte forceRefresh)
{
  byte rates[] = {
    SYNCH_RATE_1,
    SYNCH_RATE_2D,
    SYNCH_RATE_2,
    SYNCH_RATE_4D,
    SYNCH_RATE_2T,
    SYNCH_RATE_4,
    SYNCH_RATE_8D,
    SYNCH_RATE_4T,
    SYNCH_RATE_8,
    SYNCH_RATE_16D,
    SYNCH_RATE_8T,
    SYNCH_RATE_16,
    SYNCH_RATE_16T,
    SYNCH_RATE_32
  };    

  if(keyPress >= 0 && keyPress < 14)
  {
    synchPlayRate = rates[keyPress];
    forceRefresh = 1;
  }

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 13, LED_DIM);
    uiLeds[0] = LED_MEDIUM;
    uiLeds[2] = LED_MEDIUM;
    uiLeds[5] = LED_MEDIUM;
    uiLeds[8] = LED_MEDIUM;
    uiLeds[11] = LED_MEDIUM;
    uiLeds[13] = LED_MEDIUM;
    for(int i=0; i<14; ++i)
    {
      if(synchPlayRate == rates[i]) 
      {
        uiLeds[i] = LED_BRIGHT;
        break;
      }
    }
  }
}


/////////////////////////////////////////////////////
// EDIT VELOCITY
void editVelocity(char keyPress, byte forceRefresh, byte override)
{

  if(!override)
  {
    uiClearLeds();
    uiLeds[15] = LED_BRIGHT;
  }  
  else
  {
    if(keyPress > 0 && keyPress <= 15)
    {    
      arpVelocity = (keyPress+1) * 8 - 1;
      forceRefresh = 1;
    }
    else if(0 == keyPress)
    {
      arpVelocity = 0;
      forceRefresh = 1;
    }

    if(forceRefresh)
    {
      uiClearLeds();
      int z = 0;
      if(arpVelocity > 0)
        z = (arpVelocity+1)/8 - 1;
      uiSetLeds(0, z, LED_MEDIUM);
      uiLeds[z] = LED_BRIGHT;
    }  
  }
}

/////////////////////////////////////////////////////
// EDIT GATE LENGTH
void editGateLength(char keyPress, byte forceRefresh)
{
  if(keyPress >= 0 && keyPress <= 14)
  {    
    arpGateLength = keyPress + 1;
    forceRefresh = 1;
  }
  else if(keyPress == 15)
  {
    arpGateLength = 0;
    forceRefresh = 1;
  }
  if(forceRefresh)
  {
    uiClearLeds();
    if(arpGateLength > 0)
    {
      uiSetLeds(0, arpGateLength, LED_MEDIUM);
      uiLeds[arpGateLength - 1] = LED_BRIGHT;
    }
    else
    {
      uiSetLeds(0, 16, LED_MEDIUM);
      uiLeds[15] = LED_BRIGHT;
    }
  }    
}

/////////////////////////////////////////////////////
// EDIT MIDI OPTIONS
void editMidiOptions(char keyPress, byte forceRefresh)
{
  if(0 == keyPress)
  {    
    midiOptions ^= MIDI_OPTS_SEND_CHMSG;
    eepromSet(EEPROM_MIDI_OPTS, midiOptions);    
    forceRefresh = 1;
  }
  else if(1 == keyPress)
  {    
    midiOptions ^= MIDI_OPTS_PASS_INPUT_NOTES;
    eepromSet(EEPROM_MIDI_OPTS, midiOptions);    
    forceRefresh = 1;
  }
  else if(2 == keyPress)
  {    
    midiOptions ^= MIDI_OPTS_PASS_INPUT_CHMSG;
    eepromSet(EEPROM_MIDI_OPTS, midiOptions);    
    forceRefresh = 1;
  }
  else if(3 == keyPress)
  {    
    midiOptions ^= MIDI_OPTS_SYNCH_INPUT;
    eepromSet(EEPROM_MIDI_OPTS, midiOptions);    
    forceRefresh = 1;
  }
  else if(4 == keyPress)
  {    
    midiOptions ^= MIDI_OPTS_SYNCH_AUX;
    eepromSet(EEPROM_MIDI_OPTS, midiOptions);    
    forceRefresh = 1;
  }
  if(forceRefresh)
  {
    uiClearLeds();
    uiLeds[0] = !!(midiOptions&MIDI_OPTS_SEND_CHMSG)? LED_BRIGHT : LED_DIM;
    uiLeds[1] = !!(midiOptions&MIDI_OPTS_PASS_INPUT_NOTES)? LED_BRIGHT : LED_DIM;
    uiLeds[2] = !!(midiOptions&MIDI_OPTS_PASS_INPUT_CHMSG)? LED_BRIGHT : LED_DIM;
    uiLeds[3] = !!(midiOptions&MIDI_OPTS_SYNCH_INPUT)? LED_BRIGHT : LED_DIM;
    uiLeds[4] = !!(midiOptions&MIDI_OPTS_SYNCH_AUX)? LED_BRIGHT : LED_DIM;
  }    
}

/////////////////////////////////////////////////////
// EDIT NOTE INSERT MODE
void editInsertMode(char keyPress, byte forceRefresh)
{
  int i,j,note;
  switch(keyPress)
  {
  case 0: 
  case 1: 
  case 2: 
  case 3: 
  case 4:  
    arpInsertMode = keyPress;
    arpRebuild = 1;
    forceRefresh = 1;
    break;
  case 10:
    arpChordLength=2+random(3);
    for(i=0; i<arpChordLength; ++i)
    {
      for(;;)
      {
        note = 48+random(12); 
        for(j = 0; j<i; ++j)
        {
          if(ARP_GET_NOTE(arpChord[j]) == note)
            break;
        }
        if(j>=i)
          break;
      }           
      arpChord[i] = ARP_MAKE_NOTE(note,64+random(64));
    }
    arpRebuild = 1;
    break;
  case 11: // MIN7
    arpChord[0] = ARP_MAKE_NOTE(48,127);       
    arpChord[1] = ARP_MAKE_NOTE(51,127); 
    arpChord[2] = ARP_MAKE_NOTE(55,127);
    arpChord[3] = ARP_MAKE_NOTE(58,127);
    arpChordLength = 4;
    arpRebuild = 1;
    break;
  case 12: // MAJ7
    arpChord[0] = ARP_MAKE_NOTE(48,127);       
    arpChord[1] = ARP_MAKE_NOTE(52,127); 
    arpChord[2] = ARP_MAKE_NOTE(55,127);
    arpChord[3] = ARP_MAKE_NOTE(59,127);
    arpChordLength = 4;
    arpRebuild = 1;
    break;
  case 13: // DOM7
    arpChord[0] = ARP_MAKE_NOTE(48,127);       
    arpChord[1] = ARP_MAKE_NOTE(52,127); 
    arpChord[2] = ARP_MAKE_NOTE(55,127);
    arpChord[3] = ARP_MAKE_NOTE(58,127);
    arpChordLength = 4;
    arpRebuild = 1;
    break;
  case 14: // MIN
    arpChord[0] = ARP_MAKE_NOTE(48,127);       
    arpChord[1] = ARP_MAKE_NOTE(51,127);       
    ;       
    arpChord[2] = ARP_MAKE_NOTE(55,127);       
    ;       
    arpChordLength = 3;
    arpRebuild = 1;
    break;
  case 15: // MAJ
    arpChord[0] = ARP_MAKE_NOTE(48,127);       
    arpChord[1] = ARP_MAKE_NOTE(52,127);       
    ;       
    arpChord[2] = ARP_MAKE_NOTE(55,127);       
    ;       
    arpChordLength = 3;
    arpRebuild = 1;
    break;
  }

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 5, LED_DIM);
    uiLeds[arpInsertMode] = LED_BRIGHT;
    uiSetLeds(10, 6, LED_MEDIUM);
  }
}

/////////////////////////////////////////////////////
// EDIT SYNCH MODE AND TEMPO
void editTempoSynch(char keyPress, byte forceRefresh)
{
  switch(keyPress)
  {
  case 0: // Toggle MIDI synch
    synchToMIDI = !synchToMIDI;
    eepromSet(EEPROM_SYNCH_SOURCE, synchToMIDI);    
    forceRefresh = 1;
    break;
  case 1: // Toggle MIDI clock send
    if(synchSendMIDI)
    {
      synchFlags |= SYNCH_SEND_STOP;
      synchSendMIDI = 0;
    }
    else
    {
      synchFlags |= SYNCH_SEND_START;    // a start message will be sent to slaves
      synchFlags |= SYNCH_RESET_NEXT_STEP_TIME; // need to reset internal counter to ensure synch with slave
      synchFlags |= SYNCH_SEND_START;
      synchSendMIDI = 1;
      synchRestartSequence();
    }
    eepromSet(EEPROM_SYNCH_SEND, synchSendMIDI);    
    forceRefresh = 1;
    break;
  case 3: 
  case 4: 
  case 5: 
  case 6: 
  case 7: 
  case 8: 
  case 9: 
  case 10: 
  case 11: 
    synchSetTempo(keyPress * 20);
    forceRefresh = 1;
    break;

  case 13: // Tap tempo
    {
      unsigned long ms = millis();
      if(ms > editTapTempoTime && ms - editTapTempoTime < 1000)
      {
        synchSetTempo(60000L/(ms-editTapTempoTime));
        forceRefresh = 1;            
      }
      editTapTempoTime = ms;
    }
    break;
  case 14: // Manual tempo increment
    if(!synchToMIDI && synchBPM > 20)
    {
      synchSetTempo(synchBPM-1);
      forceRefresh = 1;
    }
    break;
  case 15: // Manual tempo decrement
    if(!synchToMIDI && synchBPM < 300)
    {
      synchSetTempo(synchBPM+1);
      forceRefresh = 1;
    }
    break;
  }

  if(forceRefresh)
  {
    uiSetLeds(0,16,LED_OFF);      
    if(synchToMIDI)
    {
      uiLeds[0] = LED_BRIGHT;
      uiLeds[1] = synchSendMIDI ? LED_BRIGHT : LED_MEDIUM;                                                                                                                                                                                                                                                            
    }
    else
    {    
#define BPM_METER(x,v) \
      ((abs(v-x) <= 4)? LED_BRIGHT : \
      ((abs(v-x) <= 11)? LED_MEDIUM : \
      ((abs(v-x) <= 19)? LED_DIM : LED_OFF)))

        uiLeds[0] = LED_MEDIUM;
      uiLeds[1] = synchSendMIDI ? LED_BRIGHT : LED_MEDIUM;
      if(synchBPM <= 40) 
        uiLeds[3] = LED_DIM;
      else
        uiLeds[3] = BPM_METER(synchBPM,60);      
      uiLeds[4] = BPM_METER(synchBPM,80);
      uiLeds[5] = BPM_METER(synchBPM,100);
      uiLeds[6] = BPM_METER(synchBPM,120);
      uiLeds[7] = BPM_METER(synchBPM,140);
      uiLeds[8] = BPM_METER(synchBPM,160);
      uiLeds[9] = BPM_METER(synchBPM,180);
      uiLeds[10] = BPM_METER(synchBPM,200);
      if(synchBPM >= 240) 
        uiLeds[11] = LED_DIM;
      else
        uiLeds[11] = BPM_METER(synchBPM,220);
      uiLeds[13] = LED_BRIGHT;
      uiLeds[14] = LED_BRIGHT;
      uiLeds[15] = LED_BRIGHT;      
    }
  } 
}

/////////////////////////////////////////////////////
// EDIT MIDI OUTPUT CHANNEL
void editMidiOutputChannel(char keyPress, byte forceRefresh)
{
  if(keyPress >= 0 && keyPress <= 15)
  {
    if((midiSendChannel != keyPress) && arpStopNote)
    {
      midiWrite(MIDI_MK_NOTE, arpStopNote, 0, 2, millis());
      arpStopNote = 0;
    }
    midiSendChannel = keyPress;
    eepromSet(EEPROM_OUTPUT_CHAN, midiSendChannel);
    forceRefresh = 1;
  }
  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 16, LED_DIM);
    uiLeds[midiSendChannel] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT MIDI INPUT  CHANNEL
void editMidiInputChannel(char keyPress, byte forceRefresh)
{
  if(keyPress >= 0 && keyPress <= 15)
  {
    if(midiReceiveChannel == keyPress)
    {
      midiReceiveChannel = MIDI_OMNI;
      eepromSet(EEPROM_INPUT_CHAN, MIDI_OMNI);
    }
    else
    {
      midiReceiveChannel = keyPress;
      eepromSet(EEPROM_INPUT_CHAN, midiReceiveChannel);
    }
    forceRefresh = 1;
    arpClear();
  }
  if(forceRefresh)
  {
    uiClearLeds();
    if(MIDI_OMNI == midiReceiveChannel)
      uiSetLeds(0, 16, LED_BRIGHT);
    else
      uiLeds[midiReceiveChannel] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT NOTE TRANSPOSE
void editTranspose(char keyPress, byte forceRefresh)
{  
  // 0123456789012345
  // DDDOXXXXXXXXXXXX        
  if(keyPress >= 0 && keyPress <= 15)
  {
    arpTranspose = keyPress - 3;
    arpRebuild = 1;
    forceRefresh = 1;
  }

  if(forceRefresh)
  {
    uiClearLeds();
    uiSetLeds(0, 16, LED_DIM);
    uiLeds[3] = LED_MEDIUM;
    uiLeds[arpTranspose + 3] = LED_BRIGHT;
  }
}

/////////////////////////////////////////////////////
// EDIT RUN
void editRun(unsigned long milliseconds)
{
  byte forceRefresh = 0;

  // Capture any key pressed on the data entry keypad
  char dataKeyPress = uiDataKey;
  if(dataKeyPress != NO_VALUE)
  {
    // reset the timeout period after which the 
    // display will revert to the pattern view
    uiDataKey = NO_VALUE;
    editRevertTime = milliseconds + EDIT_REVERT_TIME;
  }

  // Capture any key pressed on the data entry keypad  
  char menuKeyPress = uiMenuKey;
  if(menuKeyPress != NO_VALUE)
  {

    uiMenuKey = NO_VALUE;
    if(menuKeyPress != editMode)
    {
      // change to a new edit mode, so 
      // screen needs to be refreshed
      editMode = menuKeyPress;
      editPressType = EDIT_PRESS;
      editLongHoldTime = 0;
      forceRefresh = 1;
    }
    editRevertTime = milliseconds + EDIT_REVERT_TIME;
  }

  // is any menu key currently held?
  if(uiLastMenuKey != NO_VALUE)
  {
    // set a time at which the "long hold" event happens
    if(!editLongHoldTime)
    {
      editLongHoldTime = milliseconds + UI_LONG_HOLD_TIME;
    }
    else if(milliseconds > editLongHoldTime)
    {
      if(editPressType < EDIT_LONG_PRESS)
      {
        editPressType = EDIT_LONG_PRESS; 
        forceRefresh = 1;
      }
      else
      {
        editPressType = EDIT_LONG_HOLD;      
        editLongHoldTime = (unsigned long)(-1);
        forceRefresh = 1;
      }
    }
  }
  else
  {
    editLongHoldTime = 0;
  }

  // check if we timed out user input and should revert
  // to pattern edit mode  
  if(editRevertTime > 0 && editRevertTime < milliseconds)
  {
    // revert back to pattern edit mode
    editMode = EDIT_MODE_PATTERN;
    editRevertTime = 0;
    forceRefresh = 1;
  }

  // run the current edit mode
  switch(editMode)
  {
  case EDIT_MODE_PATTERN_LENGTH:
    editPatternLength(dataKeyPress, forceRefresh);
    break;    
  case EDIT_MODE_ARP_TYPE:
    editArpType(dataKeyPress, forceRefresh);
    break;        
  case EDIT_MODE_OCTAVE_SHIFT:
    editOctaveShift(dataKeyPress, forceRefresh);
    break;
  case EDIT_MODE_OCTAVE_SPAN:
    editOctaveSpan(dataKeyPress, forceRefresh);
    break;
  case EDIT_MODE_RATE:
    editRate(dataKeyPress, forceRefresh);
    break;
  case EDIT_MODE_VELOCITY:
    if(EDIT_LONG_PRESS == editPressType) {
      arpVelocityMode = !arpVelocityMode;
      forceRefresh = 1;
    }
    editVelocity(dataKeyPress, forceRefresh, arpVelocityMode);
    break;    
  case EDIT_MODE_GATE_LENGTH:
    editGateLength(dataKeyPress, forceRefresh);
    break;    
  case EDIT_MODE_INSERT:
    if(EDIT_LONG_PRESS == editPressType)
    {
      arpClear();
      midiPanic();
    }
    else
      editInsertMode(dataKeyPress, forceRefresh);
    break;    
  case EDIT_MODE_TEMPO_SYNCH:
    if(EDIT_LONG_HOLD == editPressType)
      editMidiOptions(dataKeyPress, forceRefresh);
    else
      editTempoSynch(dataKeyPress, forceRefresh);
    break;    
  case EDIT_MODE_CHANNEL:
    if(EDIT_LONG_HOLD == editPressType)
      editMidiInputChannel(dataKeyPress, forceRefresh);
    else
      editMidiOutputChannel(dataKeyPress, forceRefresh);
    break;    
  case EDIT_MODE_TRANSPOSE:
    editTranspose(dataKeyPress, forceRefresh);
    break;        
  case EDIT_MODE_PATTERN:
  default:
    editPattern(dataKeyPress, forceRefresh);
    break;   
  }    
}    

////////////////////////////////////////////////////////////////////////////////
//
//
//
// HEARTBEAT
//
//
//
////////////////////////////////////////////////////////////////////////////////
#define P_HEARTBEAT        13
#define HEARTBEAT_PERIOD 500
unsigned long heartbeatNext;
byte heartbeatStatus;

////////////////////////////////////////////////////////////////////////////////
// HEARTBEAT INIT
void heartbeatInit()
{
  pinMode(P_HEARTBEAT, OUTPUT);     
  heartbeatNext = 0;
  heartbeatStatus = 0;
}

////////////////////////////////////////////////////////////////////////////////
// HEARTBEAT RUN
void heartbeatRun(unsigned long milliseconds)
{
  if(milliseconds > heartbeatNext)
  {
    digitalWrite(P_HEARTBEAT, heartbeatStatus);
    heartbeatStatus = !heartbeatStatus;
    heartbeatNext = milliseconds + HEARTBEAT_PERIOD;
  }
}

////////////////////////////////////////////////////////////////////////////////
//
//
// SETUP
//
//
////////////////////////////////////////////////////////////////////////////////
void setup() {                

  midiInit();
  arpInit();
  heartbeatInit();
  editInit(); 

  // initialise the basic UI last, since this will 
  // start up the refresh interrupt
  cli();
  synchInit();
  uiInit();     
  sei();  

  // pressing hold switch at startup shows UI version
  uiShowVersion();

  // reset default EEPROM settings
  if(uiMenuKey == UI_KEY_C1)
  {
    midiSendChannel = 0;
    midiReceiveChannel = MIDI_OMNI;
    midiOptions = MIDI_OPTS_DEFAULT_VALUE;
    synchToMIDI = 0; 
    synchSendMIDI = 0; 

    eepromSet(EEPROM_OUTPUT_CHAN, midiSendChannel);
    eepromSet(EEPROM_INPUT_CHAN, midiReceiveChannel);
    eepromSet(EEPROM_MIDI_OPTS, MIDI_OPTS_DEFAULT_VALUE);
    eepromSet(EEPROM_SYNCH_SOURCE,synchToMIDI);
    eepromSet(EEPROM_SYNCH_SEND,synchSendMIDI);

    uiSetLeds(0, 16, LED_BRIGHT);
    delay(500);

  }  
}

////////////////////////////////////////////////////////////////////////////////
//
//
// LOOP
//
//
////////////////////////////////////////////////////////////////////////////////
void loop() 
{
  unsigned long millseconds = millis();

  synchRun(millseconds);
  arpRun(millseconds);
  heartbeatRun(millseconds);
  uiRun(millseconds);
  editRun(millseconds);   
}

//EOF

