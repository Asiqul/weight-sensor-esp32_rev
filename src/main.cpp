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
TaskHandle_t DisplayController;
TaskHandle_t AlarmController;
/* TASKS HANDLER DEFINITION END */

/* DEFINING HX711 PINS */
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 4
HX711 scale;
/* DEFINING HX711 PINS END */

/* RELAY PINS */
#define BUZZER_PIN 5
#define RELAY_PIN 23
/* RELAY PINS END */

/* OLED DISPLAY PARAMETERS */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
/* OLED DISPLAY PARAMETERS END */

/*

 gKEYPAD SETUP */
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
float calibration_factor = 5005;
long zero_offset = 0;
float weight;
bool isTare = false;
bool setLimMode = false;
bool setCalMode = false;
bool Start = false;
bool relayRun = false;
int16_t relayDelay = 0;
float limitSet = 0.0;
String tareStatus = "Not OK";
/* GLOBAL PARAMETERS END */

/* FUNCTION DEFINITION */
void keypadEvent(KeypadEvent key);
void DisplayText(int x, int y, int size, String text);
void DisplayControllerTask(void *pvParameters);
void AlarmControllerTask(void *pvParameters);
/* FUNCTION DEFINITION END*/

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(10);

  limitSet = EEPROM.readFloat(4);
  relayDelay = EEPROM.read(3) * 100;

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  // Splash Screen Display
  display.clearDisplay();
  display.drawBitmap(0, 0, splash, 128, 64, WHITE);
  display.display();

  // HX711 Setup
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor); // Set the calibration factor
  scale.tare();
  delay(3000);
  display.clearDisplay();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, HIGH);

  // Task Creation
        xTaskCreatePinnedToCore(
            DisplayControllerTask,
            "DisplayController",
            10000,
            NULL,
            2,
            &DisplayController,
            1);

        xTaskCreatePinnedToCore(
            AlarmControllerTask,
            "AlarmController",
            5000,
            NULL,
            1,
            &AlarmController,
            0);
  // Task Creation End

  delay(1000);
  customKeypad.addEventListener(keypadEvent);
}

void loop()
{
    // Monitoring: print minimum free stack every 5s (useful for debugging)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (DisplayController) {
      Serial.print("DisplayTask min free stack: ");
      Serial.println(uxTaskGetStackHighWaterMark(DisplayController));
    }
    if (AlarmController) {
      Serial.print("AlarmTask min free stack: ");
      Serial.println(uxTaskGetStackHighWaterMark(AlarmController));
    }
  }
  // Do nothing else in main loop: FreeRTOS tasks handle behavior
  vTaskDelay(pdMS_TO_TICKS(100));
}

void keypadEvent(KeypadEvent key)
{
  switch (customKeypad.getState())
  {
  case HOLD:
    if (key == 'A')
    {
      display.clearDisplay();
      vTaskSuspend(AlarmController);
      isTare = true;
      tareStatus = "OK";
      zero_offset = scale.read_average(15);
      scale.set_offset(zero_offset);
      scale.tare();
      DisplayText(5, 25, 2, String(F("Tare Done!")));
      delay(2000);
      display.clearDisplay();
      vTaskResume(AlarmController);
    }
    else if (key == 'C')
    {
      String newLimit = "";
      setLimMode = true;
      display.clearDisplay();
      while (setLimMode)
      {
        char input = customKeypad.getKey();
        DisplayText(22, 0, 1, String(F("SET LIMIT MAX")));
        DisplayText(0, 55, 1, String(F("A.save, D.del, C.cancel")));
        if (input == '0' || input == '1' || input == '2' || input == '3' || input == '4' || input == '5' || input == '6' || input == '7' || input == '8' || input == '9' && newLimit.length() <= 5)
        {
          newLimit += input;
          DisplayText(29, 25, 2, String(newLimit));
        }
        else if (input == 'A')
        {
          limitSet = newLimit.toFloat();
          EEPROM.put(4, limitSet);
          EEPROM.commit();
          setLimMode = false;
          limitSet = EEPROM.readFloat(4);
          display.clearDisplay();
          DisplayText(30, 25, 2, String(F("SAVED!")));
          delay(2000);
          display.clearDisplay();
        }
        else if (input == 'D')
        {
          newLimit = "";
          display.clearDisplay();
          DisplayText(0, 10, 2, String(newLimit));
        }
        else if (input == 'C')
        {
          setLimMode = false;
          display.clearDisplay();
          DisplayText(20, 25, 2, String(F("CANCELED!")));
          delay(2000);
          display.clearDisplay();
        }
      }
    } else if (key == 'B')
    {
      Start = true;
      display.clearDisplay();
    } else if (key == 'D') {
      String newCalculation = "";
      setCalMode = true;
      display.clearDisplay();
      while (setCalMode)
      {
        char input2 = customKeypad.getKey();
        DisplayText(17, 0, 1, String(F("SET DELAY RELAY")));
        DisplayText(0, 55, 1, String(F("A.save, D.del, C.cancel")));
        if (input2 == '0' || input2 == '1' || input2 == '2' || input2 == '3' || input2 == '4' || input2 == '5' || input2 == '6' || input2 == '7' || input2 == '8' || input2 == '9' && newCalculation.length() <= 5)
        {
          newCalculation += input2;
          DisplayText(29, 25, 2, String(newCalculation));
        }
        else if (input2 == 'A')
        {
          Serial.print("Delay : ");
          relayDelay = newCalculation.toInt();
          EEPROM.write(3, relayDelay);
          EEPROM.commit();
          setCalMode = false;
          Serial.println(relayDelay);
          display.clearDisplay();
          DisplayText(30, 25, 2, String(F("SAVED!")));
          relayDelay = EEPROM.read(3) * 100;
          Serial.print(F("Relay Delay : "));  //relay delay test
          Serial.println(relayDelay);
          delay(2000);
          display.clearDisplay();
        }
        else if (input2 == 'D')
        {
          newCalculation = "";
          display.clearDisplay();
          DisplayText(0, 10, 2, String(newCalculation));
        }
        else if (input2 == 'C')
        {
          setCalMode = false;
          display.clearDisplay();
          DisplayText(20, 25, 2, String(F("CANCELED!")));
          delay(2000);
          display.clearDisplay();
        }
      }
    }
    delay(2000);
    display.clearDisplay();
  case PRESSED:
    if (key == '*') {
      Start = false;
      display.clearDisplay();
    }
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

void DisplayControllerTask(void *pvParameters) {
  for (;;) {
    customKeypad.getKey();

    if (!Start) {
      DisplayText(38, 0, 1, F("MAIN MENU"));
      DisplayText(30, 19, 1, "Tare : " + tareStatus);
      DisplayText(25, 29, 1, "Limit : " + String(limitSet) + "Kg");
      DisplayText(17, 46, 1, F("A.Tare, B.Start"));
      DisplayText(17, 56, 1, F("C.Set Limit Max"));
    } else {
      display.clearDisplay();
      display.setCursor(10, 0);
      display.setTextSize(1);
      display.println(F("CALCULATING WEIGHT"));

      display.setCursor(25, 20);
      display.setTextSize(3);
      display.println(weight);

      display.setCursor(55, 55);
      display.setTextSize(1);
      display.println(F("Kg"));
      display.display();
    }
    vTaskDelay(pdMS_TO_TICKS(300));  
  }
}

void AlarmControllerTask(void *pvParameters) {
  for (;;) {
    weight = scale.get_units(4);

    if (weight >= limitSet) {
      digitalWrite(BUZZER_PIN, HIGH);
      relayRun = true;

      if (relayRun) {
        vTaskDelay(pdMS_TO_TICKS(relayDelay));
        digitalWrite(RELAY_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(350));
        digitalWrite(RELAY_PIN, HIGH);
        relayRun = false;
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(70));  
  }
}