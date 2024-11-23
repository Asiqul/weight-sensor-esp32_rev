#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Wire.h>
#include <HX711.h>
#include <Keypad.h>
#include <splash.h>

/* TASKS HANDLER DEFINITION */
TaskHandle_t MachineController;
/* TASKS HANDLER DEFINITION END */

/* DEFINING HX711 PINS */
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 4
HX711 scale;
/* DEFINING HX711 PINS END */

/* RELAY and LIMIT SWITCH PINS */
#define RELAY_PIN 23
#define LIMIT_SWITCH_PIN 5
/* RELAY and LIMIT SWITCH PINS END */

/* OLED DISPLAY PARAMETERS */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
/* OLED DISPLAY PARAMETERS END */

/* KEYPAD SETUP */
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
    {'1', '4', '7', '*'},
    {'2', '5', '8', '0'},
    {'3', '6', '9', '#'},
    {'A', 'B', 'C', 'D'},
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
/* KEYPAD SETUP END */

/* GLOBAL PARAMETERS */
float calibration_factor;
long zero_offset = 0;
float weight;
bool machine_state = false;
bool isTare = false;
bool isCalibrate = false;
bool calMode = false;
bool isReady = false;
String calStatus = "Not OK";
String tareStatus = "Not OK";
/* GLOBAL PARAMETERS END */

/* FUNCTION DEFINITION */
void keypadEvent(KeypadEvent key);
void MachineControllerTask(void *pvParameters);
void DisplayText(int x, int y, int size, String text);
/* FUNCTION DEFINITION END*/

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(2);

  calibration_factor = EEPROM.readFloat(1);
  if (calibration_factor == 0)
  {
    calibration_factor = -500;
    EEPROM.put(1, calibration_factor);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  // Splash Screen Display
  display.clearDisplay();
  display.drawBitmap(0, 0, splash, 128, 64, WHITE);
  display.display();

  // HX711 Setup
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  // Relay and Limit Switch Setup
  pinMode(RELAY_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  delay(2000);

  display.clearDisplay();

  customKeypad.addEventListener(keypadEvent);
}

void loop()
{
  if (!machine_state)
  {
    display.clearDisplay();
    while (!isReady)
    {
      char input = customKeypad.getKey();

      DisplayText(16, 0, 1, String("CALIBRATE FIRST!"));
      DisplayText(10, 19, 1, String("Tare : " + tareStatus));
      DisplayText(10, 29, 1, String("Cal : " + calStatus));
      DisplayText(0, 46, 1, String("Hold * to Tare!"));
      DisplayText(0, 56, 1, String("Hold # to Calibrate!"));

      if (isTare && isCalibrate && !calMode)
      {
        delay(1500);
        isReady = true;
        display.clearDisplay();

        // Task Creation
        xTaskCreatePinnedToCore(
            MachineControllerTask,
            "MachineController",
            10000,
            NULL,
            1,
            &MachineController,
            0);
        // Task Creation End
        break;
      };
    }
  }
  else
  {
    display.clearDisplay();
    DisplayText(17, 0, 1, String("Measured Weight"));
    DisplayText(50, 50, 1, String("grams"));

    display.clearDisplay();
    display.setCursor(20, 25);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.println(String(weight));
    display.display();
  }
}

void keypadEvent(KeypadEvent key)
{
  switch (customKeypad.getState())
  {
  case HOLD:
    if (key == '*')
    {
      display.clearDisplay();
      isTare = true;
      tareStatus = "OK";
      zero_offset = scale.read_average(15);
      scale.set_offset(zero_offset);
      DisplayText(5, 25, 2, String("Tare Done!"));
      delay(2000);
      display.clearDisplay();
    }
    else if (key == '#')
    {
      String newFactor = "";
      calMode = true;
      display.clearDisplay();
      while (calMode)
      {
        char input = customKeypad.getKey();
        DisplayText(10, 0, 1, String("Calibration Factor:"));
        DisplayText(0, 55, 1, String("A.save, D.del, C.cancel"));
        if (input == '0' || input == '1' || input == '2' || input == '3' || input == '4' || input == '5' || input == '6' || input == '7' || input == '8' || input == '9' && newFactor.length() <= 5)
        {
          newFactor += input;
          DisplayText(29, 25, 2, String(newFactor));
        }
        else if (input == 'A')
        {
          calibration_factor = newFactor.toFloat();
          EEPROM.writeFloat(1, calibration_factor);
          calMode = false;
          isCalibrate = true;
          calStatus = "OK";
          display.clearDisplay();
          DisplayText(30, 25, 2, String("SAVED!"));
          delay(2000);
          Serial.println(EEPROM.readFloat(1));
          display.clearDisplay();
        }
        else if (input == 'D')
        {
          newFactor = "";
          display.clearDisplay();
          DisplayText(0, 10, 2, String(newFactor));
        }
        else if (input == 'C')
        {
          calMode = false;
          display.clearDisplay();
          DisplayText(20, 25, 2, String("CANCELED!"));
          delay(2000);
          display.clearDisplay();
        }
      }
    }
    delay(2000);
    break;
  }
}

void DisplayText(int x, int y, int size, String text)
{
  display.setCursor(x, y);
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.println((text));
  display.display();
}

void MachineControllerTask(void *pvParameters)
{
  while (true)
  {
    if (!machine_state)
    {
      if (digitalRead(LIMIT_SWITCH_PIN) == LOW)
      {
        digitalWrite(RELAY_PIN, LOW);
        machine_state = true;
        Serial.println("Machine Started!");
      }
      Serial.println("Limit Switch: " + String(digitalRead(LIMIT_SWITCH_PIN)));
      Serial.println("Machine State: " + String(machine_state));
    }
    else if (machine_state)
    {
      // code to measure weight
      weight = scale.get_units(15);
      Serial.println("Weight: " + String(weight));
      if (weight >= 500)
      {
        digitalWrite(RELAY_PIN, HIGH);
        machine_state = false;
        Serial.println("Machine Stopped!");
      }
      Serial.println("Limit Switch: " + String(digitalRead(LIMIT_SWITCH_PIN)));
    }
    delay(100);
  }
}