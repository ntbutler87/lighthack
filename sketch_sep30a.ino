/*******************************************************************************
Includes
******************************************************************************/
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif
#include <string.h>
#include <SPI.h>
#include <TFT.h>

/*******************************************************************************
Macros and Constants
******************************************************************************/
//LCD TFT screen
#define cs 10
#define dc 9
#define rst 8
TFT TFTscreen = TFT(cs, dc, rst);

#define NEXT_BTN 6
#define LAST_BTN 7
#define TOGGLE_BTN 5
#define SHIFT_BTN 4

#define SUBSCRIBE ((int32_t)1)
#define UNSUBSCRIBE ((int32_t)0)

#define EDGE_DOWN ((int32_t)1)
#define EDGE_UP ((int32_t)0)

#define FORWARD 0
#define REVERSE 1

#define NUM_ENCODERS 3

#define PAN_DIR FORWARD
#define TILT_DIR FORWARD

// Use these values to make the encoder more coarse or fine.
// This controls the number of wheel "ticks" the device sends to the console
// for each tick of the encoder. 1 is the default and the most fine setting.
// Must be an integer.
#define ENC1_SCALE 4
#define ENC2_SCALE 4
#define ENC3_SCALE 4

#define SIG_DIGITS 1 // Number of significant digits displayed

#define OSC_BUF_MAX_SIZE 512

const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY = "OK";
const String PING_QUERY = "box1_hello";
const String SUBSCRIBE_QUERY = "/eos/subscribe/param/";
const String PARAMETER_QUERY = "eos/out/param/";

int connectionStatus;
int changePageDisplay;

//See displayScreen() below - limited to 10 chars (after 6 prefix chars)
#define VERSION_STRING "2.0.0.1"

#define BOX_NAME_STRING "box1"

// Change these values to alter how long we wait before sending an OSC ping
// to see if Eos is still there, and then finally how long before we
// disconnect and show the splash screen
// Values are in milliseconds
#define PING_AFTER_IDLE_INTERVAL 2500
#define TIMEOUT_AFTER_IDLE_INTERVAL 5000

/*******************************************************************************
Local Types
******************************************************************************/

struct Parameter {
String name;
String display;
float value;
float lastDisplayed;
};

const uint8_t PARAMETER_MAX = 6;
struct Parameter parameter[PARAMETER_MAX] =
{
// {"empty", "--"},
// {"Intens"},
{"Pan"},
{"Tilt"},
// {"Zoom"},
{"Edge"},
// {"Iris"},
// {"Diffusn"},
// {"Hue"},
// {"Saturatn"},
{"Red"},
{"Green"},
{"Blue"}
// {"Cyan"},
// {"Magenta"},
// {"Yellow"}
};

struct Encoder
{
uint8_t parameterIdx;
uint8_t pinA;
uint8_t pinB;
int pinAPrevious;
int pinBPrevious;
uint8_t direction;
};
struct Encoder encoder1;
struct Encoder encoder2;
struct Encoder encoder3;

//Buttons
struct Button
{
uint8_t pin;
uint8_t last;
String function;
};

struct Button encoder1Button; //7
struct Button encoder2Button; //6
struct Button encoder3Button; //5
struct Button staticButton; //4

enum ConsoleType
{
ConsoleNone,
ConsoleEos,
ConsoleCobalt,
ConsoleColorSource
};

struct displayItem
{
uint8_t xpos;
uint8_t ypos;
uint8_t textSize;
String value;

};
struct displayItem screenLayout[6] =
{
{5, 15, 2},
{70, 15, 2},
{5, 55, 2},
{70, 55, 2},
{5, 95, 2},
{70, 95, 2}
};

/*******************************************************************************
Global Variables
******************************************************************************/

int8_t index = 0; // start with parameter index 2 must even
bool updateDisplay = false;
bool connectedToEos = false;
ConsoleType connectedToConsole = ConsoleNone;
unsigned long lastMessageRxTime = 0;
bool timeoutPingSent = false;

/*******************************************************************************
Local Functions
******************************************************************************/

void sendPing()
{
OSCMessage ping("/eos/ping");
ping.add(PING_QUERY.c_str());
SLIPSerial.beginPacket();
ping.send(SLIPSerial);
SLIPSerial.endPacket();
timeoutPingSent = true;
}

void issueSubscribes()
{
// Add a filter so we don't get spammed with unwanted OSC messages from Eos
OSCMessage filter("/eos/filter/add");
filter.add("/eos/out/param/*");
filter.add("/eos/out/ping");
SLIPSerial.beginPacket();
filter.send(SLIPSerial);
SLIPSerial.endPacket();

// subscribes all parameters of your list, exept item 0
for (int i = 0; i < PARAMETER_MAX; i++)
{
String subMsg = SUBSCRIBE_QUERY + parameter[i].name;
OSCMessage subscribe(subMsg.c_str());
subscribe.add(SUBSCRIBE);
SLIPSerial.beginPacket();
subscribe.send(SLIPSerial);
SLIPSerial.endPacket();
}
}

void initEOS()
{
SLIPSerial.beginPacket();
SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
SLIPSerial.endPacket();
issueSubscribes();
}

void parseOSCMessage(String& msg)
{
// check to see if this is the handshake string
if (msg.indexOf(HANDSHAKE_QUERY) != -1)
{
initEOS();
// Make our splash screen go away
connectedToEos = true;
issueSubscribes();
connectedToConsole = ConsoleEos;
updateDisplay = true;
}
else
{
// checks if there is a message with data of your parameter list
OSCMessage oscmsg;
oscmsg.fill((uint8_t*)msg.c_str(), (int)msg.length());
for (int i = 0; i < PARAMETER_MAX; i++)
{
String parseMsg = PARAMETER_QUERY + parameter[i].name;
if(msg.indexOf(parseMsg) != -1)
{
parameter[i].value = oscmsg.getOSCData(0)->getFloat(); // get the value
connectedToEos = true; // Update this here just in case we missed the handshake
connectedToConsole = ConsoleEos;
updateDisplay = true;
}
}
}
}

void displayStatus()
{
if (connectedToConsole == ConsoleNone)
{
TFTscreen.background(0, 0, 0);
TFTscreen.stroke(0,0,0);
TFTscreen.fill(20,0,0);
TFTscreen.rect(-1,-1,TFTscreen.width()+3,TFTscreen.height()+3);

TFTscreen.stroke(255,200,255);
TFTscreen.setTextSize(2);
TFTscreen.text("Waiting_", 10, 60);
}
else
{
int j = 0;
// if changePage!!!!
if (changePageDisplay)
{
changePageDisplay = 0;
TFTscreen.background(0, 0, 0);
TFTscreen.stroke(0,0,0);
TFTscreen.fill(0,0,0);
TFTscreen.rect(-1,-1,TFTscreen.width()+3,TFTscreen.height()+3);

TFTscreen.stroke(255, 255, 255);
// set the font size
TFTscreen.setTextSize(2);
// write the text to the top left corner of the screen

for (int i=index; i < index + NUM_ENCODERS; i++)
{
TFTscreen.text( parameter[i].name.c_str(), screenLayout[j].xpos, screenLayout[j].ypos);
j += 2;
}
}
//endIF

j = 1;
for (int i=index; i < index + NUM_ENCODERS; i++)
{
if ( parameter[i].lastDisplayed != parameter[i].value )
{
char valBuf[8];
TFTscreen.stroke(0, 0, 0);
TFTscreen.setTextSize(2);
//wipe out the last value
dtostrf(parameter[i].lastDisplayed, 3, 1, valBuf);
TFTscreen.text(valBuf, screenLayout[j].xpos, screenLayout[j].ypos);
//write the new value
TFTscreen.stroke(255, 255, 255);
dtostrf(parameter[i].value, 3, 1, valBuf);
TFTscreen.text(valBuf, screenLayout[j].xpos, screenLayout[j].ypos);
//update the value
parameter[i].lastDisplayed = parameter[i].value;
}
j += 2;
}
}

updateDisplay = false;
}

void initEncoder(struct Encoder* encoder, uint8_t pinA, uint8_t pinB, uint8_t direction)
{
encoder->pinA = pinA;
encoder->pinB = pinB;
encoder->direction = direction;

pinMode(pinA, INPUT_PULLUP);
pinMode(pinB, INPUT_PULLUP);

encoder->pinAPrevious = digitalRead(pinA);
encoder->pinBPrevious = digitalRead(pinB);
}

int8_t updateEncoder(struct Encoder* encoder)
{
int8_t encoderMotion = 0;
int pinACurrent = digitalRead(encoder->pinA);
int pinBCurrent = digitalRead(encoder->pinB);

// has the encoder moved at all?
if (encoder->pinAPrevious != pinACurrent)
{
// Since it has moved, we must determine if the encoder has moved forwards or backwards
encoderMotion = (encoder->pinAPrevious == encoder->pinBPrevious) ? -1 : 1;

// If we are in reverse mode, flip the direction of the encoder motion
if (encoder->direction == REVERSE)
encoderMotion = -encoderMotion;
}
encoder->pinAPrevious = pinACurrent;
encoder->pinBPrevious = pinBCurrent;

return encoderMotion;
}

/*******************************************************************************
Sends a message to Eos informing them of a wheel movement.

Parameters:
type - the type of wheel that's moving (i.e. pan or tilt)
ticks - the direction and intensity of the movement

Return Value: void

******************************************************************************/

void sendOscMessage(const String &address, float value)
{
OSCMessage msg(address.c_str());
msg.add(value);
SLIPSerial.beginPacket();
msg.send(SLIPSerial);
SLIPSerial.endPacket();
}
void sendKeyPress(bool down, const String &key)
{
String keyAddress;
switch (connectedToConsole)
{
default:
case ConsoleEos:
keyAddress = "/eos/key/" + key;
break;
case ConsoleCobalt:
keyAddress = "/cobalt/key/" + key;
break;
case ConsoleColorSource:
keyAddress = "/cs/key/" + key;
break;
}
OSCMessage keyMsg(keyAddress.c_str());

if (down)
keyMsg.add(EDGE_DOWN);
else
keyMsg.add(EDGE_UP);

SLIPSerial.beginPacket();
keyMsg.send(SLIPSerial);
SLIPSerial.endPacket();
}

/*******************************************************************************
Checks the status of all the relevant buttons (i.e. Next & Last)

NOTE: This does not check the shift key. The shift key is used in tandem with
the encoder to determine coarse/fine mode and thus does not report directly.

Parameters: none

Return Value: void

******************************************************************************/

void checkButtons()
{
// OSC configuration
const int keyCount = 3;
const int keyPins[3] = {NEXT_BTN, LAST_BTN, TOGGLE_BTN};
const String keyNames[4] = {
"NEXT", "LAST",
"soft6", "soft4"
};

static int keyStates[3] = {HIGH, HIGH, HIGH};

// Eos and Cobalt buttons are the same
// ColorSource is different
int firstKey = (connectedToConsole == ConsoleColorSource) ? 2 : 0;

// Loop over the buttons
for (int keyNum = 0; keyNum < keyCount; ++keyNum)
{
// Has the button state changed
if (digitalRead(keyPins[keyNum]) != keyStates[keyNum])
{
if (keyPins[keyNum] == TOGGLE_BTN) {
updateDisplay = true;
index += NUM_ENCODERS;
if (index > PARAMETER_MAX - NUM_ENCODERS)
{
index = 0;
}
encoder1.parameterIdx = index;
encoder2.parameterIdx = index + 1;
encoder3.parameterIdx = index + 2;
displayMessage(3);
connectionStatus = -1; //to re-draw the screen
changePageDisplay = 1;
displayStatus();
}
else
{
// Notify console of this key press
if (keyStates[keyNum] == LOW)
{
sendKeyPress(false, keyNames[firstKey + keyNum]);
keyStates[keyNum] = HIGH;
}
else
{
displayMessage(keyNum);
connectionStatus = -1; //to re-draw the screen
changePageDisplay = 1;
displayStatus();
sendKeyPress(true, keyNames[firstKey + keyNum]);
keyStates[keyNum] = LOW;
}
}
}
}
}
void sendWheelMove(const String &param, float ticks)
{
String wheelMsg("/eos/wheel");

if (digitalRead(SHIFT_BTN) == LOW)
//wheelMsg.concat("/fine");
ticks = ticks / abs(ticks);
else
wheelMsg.concat("/coarse");

wheelMsg.concat("/" + param);

sendOscMessage(wheelMsg, ticks);
}

void displayMessage(int message)
{
TFTscreen.background(0, 0, 0);
TFTscreen.stroke(0,0,0);
TFTscreen.fill(0,0,0);
TFTscreen.rect(0,0,TFTscreen.width(),TFTscreen.height());

TFTscreen.stroke(0,0,255);
TFTscreen.fill(0,0,0);
TFTscreen.rect(2,2,TFTscreen.width()-4,TFTscreen.height()-4);
TFTscreen.stroke(255, 0, 255);
TFTscreen.setTextSize(2);
switch (message) {
case 0:
TFTscreen.text("NEXT", 40, 60);
break;
case 1:
TFTscreen.text("LAST", 40, 60);
break;
case 3:
TFTscreen.text("PAGE UP", 40, 60);
break;
}

delay(400);
updateDisplay = true;
}

void initButton(struct Button* button, uint8_t pin)
{
button->pin = pin;
button->last = HIGH;
pinMode(pin, INPUT_PULLUP);
}

void updateButton(struct Button* button)
{
if ((digitalRead(button->pin)) != button->last)
{
if (button->last == LOW)
{
button->last = HIGH;
//calc index
if (button->pin == TOGGLE_BTN)
{
index += NUM_ENCODERS;
if (index > PARAMETER_MAX - NUM_ENCODERS)
{
index = 0;
}
encoder1.parameterIdx = index;
encoder2.parameterIdx = index + 1;
encoder3.parameterIdx = index + 2;
displayStatus();
}
else if (button->pin == NEXT_BTN)
{
sendKeyPress(true, "NEXT");
displayMessage(button->pin);
displayStatus();
}
else if (button->pin == LAST_BTN)
{
sendKeyPress(true, "LAST");
displayMessage(button->pin);
displayStatus();
}
}
else
{
button->last = LOW;
if (button->pin == NEXT_BTN)
{
sendKeyPress(false, "NEXT");
}
else if (button->pin == LAST_BTN)
{
sendKeyPress(false, "LAST");
}
}
}
}

void setup()
{
//***************** Initiate Serial ********************
SLIPSerial.begin(115200);
// This is a hack around an Arduino bug. It was taken from the OSC library
//examples
#ifdef BOARD_HAS_USB_SERIAL
while (!SerialUSB);
#else
while (!Serial);
#endif

// This is necessary for reconnecting a device because it needs some time
// for the serial port to open. The handshake message may have been sent
// from the console before #lighthack was ready

SLIPSerial.beginPacket();
SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
SLIPSerial.endPacket();

// If it's an Eos, request updates on some things
issueSubscribes();

//***************** Initiate Encoder Wheels ********************
initEncoder(&encoder1, A0, A1, PAN_DIR);
initEncoder(&encoder2, A2, A3, TILT_DIR);
initEncoder(&encoder3, A4, A5, TILT_DIR);

encoder1.parameterIdx = index;
encoder2.parameterIdx = index + 1;
encoder3.parameterIdx = index + 2;

//***************** Initiate Buttons ********************
initButton(&encoder1Button, LAST_BTN);
initButton(&encoder2Button, NEXT_BTN);
initButton(&encoder3Button, TOGGLE_BTN);
initButton(&staticButton, SHIFT_BTN);

//***************** Initiate Display ********************
TFTscreen.begin();
TFTscreen.setRotation(3);
screenLayout[0].value = parameter[encoder1.parameterIdx].name;
screenLayout[2].value = parameter[encoder2.parameterIdx].name;
screenLayout[4].value = parameter[encoder3.parameterIdx].name;
connectionStatus = -1;
changePageDisplay = 1;
displayStatus();
}
void loop()
{

//***************** Check for new messages ********************
static String curMsg;
int size;
// Check to see if any OSC commands have come from Eos
// and update the display accordingly.
size = SLIPSerial.available();
if (size > 0)
{
// Fill the msg with all of the available bytes
while (size--)
curMsg += (char)(SLIPSerial.read());
}
if (SLIPSerial.endofPacket())
{
parseOSCMessage(curMsg);
lastMessageRxTime = millis();
// We only care about the ping if we haven't heard recently
// Clear flag when we get any traffic
timeoutPingSent = false;
curMsg = String();
}

//***************** Check for wheel movements ********************

// get the updated state of each encoder
int32_t encoder1Motion = updateEncoder(&encoder1);
int32_t encoder2Motion = updateEncoder(&encoder2);
int32_t encoder3Motion = updateEncoder(&encoder3);

// Scale the result by a scaling factor
encoder1Motion *= ENC1_SCALE;
encoder2Motion *= ENC2_SCALE;
encoder3Motion *= ENC3_SCALE;

//***************** If wheel moved, update EOS ********************

// now update our wheels
if (encoder1Motion != 0)
sendWheelMove(parameter[encoder1.parameterIdx].name, encoder1Motion);

if (encoder2Motion != 0)
sendWheelMove(parameter[encoder2.parameterIdx].name, encoder2Motion);

if (encoder3Motion != 0)
sendWheelMove(parameter[encoder3.parameterIdx].name, encoder3Motion);
//***************** Check for button changes ********************

// check for next/last updates
checkButtons();
//***************** Make sure we are still connected ********************

if (lastMessageRxTime > 0)
{
unsigned long diff = millis() - lastMessageRxTime;
//We first check if it's been too long and we need to time out
if (diff > TIMEOUT_AFTER_IDLE_INTERVAL)
{
connectedToConsole = ConsoleNone;
lastMessageRxTime = 0;
updateDisplay = true;
timeoutPingSent = false;
}

//It could be the console is sitting idle. Send a ping once to
// double check that it's still there, but only once after 2.5s have passed
if (!timeoutPingSent && diff > PING_AFTER_IDLE_INTERVAL)
{
OSCMessage ping("/eos/ping");
ping.add(BOX_NAME_STRING "_hello"); // This way we know who is sending the ping
SLIPSerial.beginPacket();
ping.send(SLIPSerial);
SLIPSerial.endPacket();
timeoutPingSent = true;
}
}

if (updateDisplay)
displayStatus();
}
