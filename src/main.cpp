#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <GEM_u8g2.h>
#include "X9C10X_H595.h"
// #include <ESP32Encoder.h>
// #include <ezButton.h>  // the library to use for SW pin
//#include "max7219.h"
#include <KeyDetector.h>

#define CS_HC595    5
// #define CS_MAX7219  15
#define KEY_PIN     0
#define LED_BLUE    2

#define CS_HC595_SET()  digitalWrite(CS_HC595, LOW)
#define CS_HC595_RESET()  digitalWrite(CS_HC595, HIGH)


// #define CLK_PIN 27  // ESP32 pin GPIO25 connected to the rotary encoder's CLK pin
// #define DT_PIN 26   // ESP32 pin GPIO26 connected to the rotary encoder's DT pin
// #define SW_PIN 25   // ESP32 pin GPIO27 connected to the rotary encoder's SW pin

#define DIRECTION_CW 0   // clockwise direction
#define DIRECTION_CCW 1  // counter-clockwise direction

// Define signal identifiers for three outputs of encoder (channel A, channel B and a push-button)
#define KEY_A 1
#define KEY_B 2
#define KEY_C 3

void setupMenu();
void Task0(void* parameters);
void Task1(void* parameters);
void Task2(void* parameters);
void Task3(void* parameters);
void Task4(void* parameters);
void Task5(void* parameters);

// Pins encoder is connected to
const byte channelA = 27;
const byte channelB = 26;
const byte buttonPin = 25;

// Array of Key objects that will link GEM key identifiers with dedicated pins
// (it is only necessary to detect signal change on a single channel of the encoder, either A or B;
// order of the channel and push-button Key objects in an array is not important)
Key keys[] = {{KEY_A, channelA}, {KEY_C, buttonPin}};
//Key keys[] = {{KEY_C, buttonPin}, {KEY_A, channelA}};

// Create KeyDetector object
// KeyDetector myKeyDetector(keys, sizeof(keys)/sizeof(Key));
// To account for switch bounce effect of the buttons (if occur) you may want to specify debounceDelay
// as the third argument to KeyDetector constructor.
// Make sure to adjust debounce delay to better fit your rotary encoder.
// Also it is possible to enable pull-up mode when buttons wired with pull-up resistors (as in this case).
// Analog threshold is not necessary for this example and is set to default value 16.
KeyDetector myKeyDetector(keys, sizeof(keys)/sizeof(Key), /* debounceDelay= */ 5, /* analogThreshold= */ 16, /* pullup= */ true);

bool secondaryPressed = false;  // If encoder rotated while key was being pressed; used to prevent unwanted triggers
bool cancelPressed = false;  // Flag indicating that Cancel action was triggered, used to prevent it from triggering multiple times
const int keyPressDelay = 1000; // How long to hold key in pressed state to trigger Cancel action, ms
long keyPressTime = 0; // Variable to hold time of the key press event
long now; // Variable to hold current time taken with millis() function at the beginning of loop()


U8G2_SSD1306_128X64_NONAME_F_SW_I2C
u8g2(U8G2_R0,/*clock=*/22,21,U8X8_PIN_NONE);

// Create variables that will be editable through the menu and assign them initial values
int number = -512;
bool enablePrint = false;

// Create variable that will be editable through option select and create associated option select.
// This variable will be passed to menu.invertKeysDuringEdit(), and naturally can be presented as a bool,
// but is declared as a byte type to be used in an option select rather than checkbox (for demonstration purposes)
byte invert = 1;
SelectOptionByte selectInvertOptions[] = {{"Invert", 1}, {"Normal", 0}};
GEMSelect selectInvert(sizeof(selectInvertOptions)/sizeof(SelectOptionByte), selectInvertOptions);

// Create menu item for option select with applyInvert() callback function
void applyInvert(); // Forward declaration
GEMItem menuItemInvert("Chars order:", invert, selectInvert, applyInvert);

// Create two menu item objects of class GEMItem, linked to number and enablePrint variables 
GEMItem menuItemInt("Number:", number);
GEMItem menuItemBool("Enable print:", enablePrint);

// Create menu button that will trigger printData() function. It will print value of our number variable
// to Serial monitor if enablePrint is true. We will write (define) this function later. However, we should
// forward-declare it in order to pass to GEMItem constructor
void printData(); // Forward declaration
GEMItem menuItemButton("Print", printData);

// Create menu page object of class GEMPage. Menu page holds menu items (GEMItem) and represents menu level.
// Menu can have multiple menu pages (linked to each other) with multiple menu items each
GEMPage menuPageMain("Main Menu"); // Main page
GEMPage menuPageSettings("Settings"); // Settings submenu

// Create menu item linked to Settings menu page
GEMItem menuItemMainSettings("Settings", menuPageSettings);

// Create menu object of class GEM_u8g2. Supply its constructor with reference to u8g2 object we created earlier
GEM_u8g2 menu(u8g2, GEM_POINTER_ROW, GEM_ITEMS_COUNT_AUTO);
// Which is equivalent to the following call (you can adjust parameters to better fit your screen if necessary):
// GEM_u8g2 menu(u8g2, /* menuPointerType= */ GEM_POINTER_ROW, /* menuItemsPerScreen= */ GEM_ITEMS_COUNT_AUTO, /* menuItemHeight= */ 10, /* menuPageScreenTopOffset= */ 10, /* menuValuesLeftOffset= */ 86);


X9C10X pot(10000);
bool keyFlag = false;
bool task4Active = true;

TaskHandle_t Task2Handle = NULL;

SemaphoreHandle_t mutexSPI;
SemaphoreHandle_t mutexSerial;

// volatile int counter = 0;
// volatile int direction = DIRECTION_CW;
// volatile unsigned long last_time;  // for debouncing
// int prev_counter;

// ezButton button(SW_PIN);  // create ezButton object that attach to pin 7;


// void IRAM_ATTR ISR_encoder() {
//   if ((millis() - last_time) < 50)  // debounce time is 50ms
//     return;

//   if (digitalRead(DT_PIN) == HIGH) {
//     // the encoder is rotating in counter-clockwise direction => decrease the counter
//     counter--;
//     direction = DIRECTION_CCW;
//   } else {
//     // the encoder is rotating in clockwise direction => increase the counter
//     counter++;
//     direction = DIRECTION_CW;
//   }

//   last_time = millis();
// }

void setup() 
{
  Serial.begin(115200);
  Serial.println("Serial is OK!");
  u8g2.begin();
  Serial.println();
  Serial.print("X9C10X_LIB_VERSION: ");
  pot.begin(33, 32, 35);  // pulse, direction, select
  pot.setPosition(0);

  pinMode(channelA, INPUT_PULLUP);
  pinMode(channelB, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);


  SPI.begin(18, 21, 23);
  //max7219.Begin();

  mutexSPI = xSemaphoreCreateMutex();
  mutexSerial = xSemaphoreCreateMutex();

  // configure encoder pins as inputs
  // pinMode(CLK_PIN, INPUT);
  // pinMode(DT_PIN, INPUT);
  // button.setDebounceTime(50);  // set debounce time to 50 milliseconds

  // use interrupt for CLK pin is enough
  // call ISR_encoder() when CLK pin changes from LOW to HIGH
  // attachInterrupt(digitalPinToInterrupt(CLK_PIN), ISR_encoder, RISING);

    // U8g2 library init.
  u8g2.begin();
  
  // Turn inverted order of characters during edit mode on (feels more natural when using encoder)
  menu.invertKeysDuringEdit(invert);
  
  // Menu init, setup and draw
  menu.init();
  setupMenu();
  menu.drawMenu();

  xTaskCreate(Task0, "Task0", 2048, NULL, 1, NULL);
  xTaskCreatePinnedToCore(Task1, "Task1", 2048, NULL, 1, NULL, 0);
  // xTaskCreatePinnedToCore(Task2, "Task2", 2048, NULL, 1, &Task2Handle, 1);
  // xTaskCreate(Task3, "Task3", 2048, NULL, 2, NULL);
  // xTaskCreate(Task4, "Task4", 2048, NULL, 1, NULL);
  // xTaskCreate(Task5, "Task5", 2048, NULL, 1, NULL);
}

void setupMenu() {
  // Add menu items to Settings menu page
  menuPageSettings.addMenuItem(menuItemInvert);

  // Add menu items to menu page
  menuPageMain.addMenuItem(menuItemMainSettings);
  menuPageMain.addMenuItem(menuItemInt);
  menuPageMain.addMenuItem(menuItemBool);
  menuPageMain.addMenuItem(menuItemButton);

  // Specify parent menu page for the Settings menu page
  menuPageSettings.setParentMenuPage(menuPageMain);

  // Add menu page to menu and set it as current
  menu.setMenuPageCurrent(menuPageMain);
}

void loop() 
{
  
}

void Task0(void* parameters)
{
  xSemaphoreTake(mutexSerial, portMAX_DELAY);
  Serial.printf("[%8lu] Run Task0 only once: %d\r\n", millis(), xPortGetCoreID());
  xSemaphoreGive(mutexSerial);

  vTaskDelete(NULL);
}

void Task1(void* parameters)
{
  //TODO: код що виконується раз при запускі задачі
  pinMode(CS_HC595, OUTPUT);
  CS_HC595_RESET();
  // u8g2.clearBuffer();
  // u8g2.setFont(u8g2_font_7x14B_tr);
  // u8g2.drawStr(0,10,"Hi,vit");
  // u8g2.sendBuffer();
  
  while (1)
  {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  // Get current time to use later on
  now = millis();

  // If menu is ready to accept button press...
  if (menu.readyForKey()) {
    // ...detect key press using KeyDetector library
    // and pass pressed button to menu
    myKeyDetector.detect();
  
    switch (myKeyDetector.trigger) {
      case KEY_C:
        // Button was pressed
        Serial.println("Button pressed");
        // Save current time as a time of the key press event
        keyPressTime = now;
        break;
    }
    /* Detecting rotation of the encoder on release rather than push
    (i.e. myKeyDetector.triggerRelease rather myKeyDetector.trigger)
    may lead to more stable readings (without excessive signal ripple) */
    switch (myKeyDetector.triggerRelease) {
      case KEY_A:
        // Signal from Channel A of encoder was detected
        if (digitalRead(channelB) == LOW) {
          // If channel B is low then the knob was rotated CCW
          if (myKeyDetector.current == KEY_C) {
            // If push-button was pressed at that time, then treat this action as GEM_KEY_LEFT,...
            Serial.println("Rotation CCW with button pressed (release)");
            menu.registerKeyPress(GEM_KEY_LEFT);
            // Button was in a pressed state during rotation of the knob, acting as a modifier to rotation action
            secondaryPressed = true;
          } else {
            // ...or GEM_KEY_UP otherwise
            Serial.println("Rotation CCW (release)");
            menu.registerKeyPress(GEM_KEY_UP);
          }
        } else {
          // If channel B is high then the knob was rotated CW
          if (myKeyDetector.current == KEY_C) {
            // If push-button was pressed at that time, then treat this action as GEM_KEY_RIGHT,...
            Serial.println("Rotation CW with button pressed (release)");
            menu.registerKeyPress(GEM_KEY_RIGHT);
            // Button was in a pressed state during rotation of the knob, acting as a modifier to rotation action
            secondaryPressed = true;
          } else {
            // ...or GEM_KEY_DOWN otherwise
            Serial.println("Rotation CW (release)");
            menu.registerKeyPress(GEM_KEY_DOWN);
          }
        }
        break;
      case KEY_C:
        // Button was released
        Serial.println("Button released");
        if (!secondaryPressed) {
          // If button was not used as a modifier to rotation action...
          if (now <= keyPressTime + keyPressDelay) {
            // ...and if not enough time passed since keyPressTime,
            // treat key that was pressed as Ok button
            menu.registerKeyPress(GEM_KEY_OK);
          }
        }
        secondaryPressed = false;
        cancelPressed = false;
        break;
    }
    // After keyPressDelay passed since keyPressTime
    if (now > keyPressTime + keyPressDelay) {
      switch (myKeyDetector.current) {
        case KEY_C:
          if (!secondaryPressed && !cancelPressed) {
            // If button was not used as a modifier to rotation action, and Cancel action was not triggered yet
            Serial.println("Button remained pressed");
            // Treat key that was pressed as Cancel button
            menu.registerKeyPress(GEM_KEY_CANCEL);
            cancelPressed = true;
          }
          break;
      }
    }
  }




    // button.loop();  // MUST call the loop() function first

    // if (prev_counter != counter) {
    //   Serial.print("Rotary Encoder:: direction: ");
    //   if (direction == DIRECTION_CW)
    //     Serial.print("CLOCKWISE");
    //   else
    //     Serial.print("ANTICLOCKWISE");
    
    //   Serial.print(" - count: ");
    //   Serial.println(counter);
    
    //   prev_counter = counter;
    // }

    // if (button.isPressed()) {
    //   Serial.println("The button is pressed");
    // }


    //TODO: код що виконується безкінечно
  //   for (size_t i = 0; i < 4; i++)
  //   {
  //     xSemaphoreTake(mutexSerial, portMAX_DELAY);
  //     Serial.printf("[%8lu] Run Task1 on Core: %d\r\n", millis(), xPortGetCoreID());
  //     xSemaphoreGive(mutexSerial);
      
  //     xSemaphoreTake(mutexSPI, portMAX_DELAY);
  //     CS_HC595_SET();
  //     SPI.transfer(1 << i);
  //     CS_HC595_RESET();
  //     xSemaphoreGive(mutexSPI);

  //     vTaskDelay(1000 / portTICK_PERIOD_MS);
  //   }
  }
}

// void Task2(void* parameters)
// {
//   while (1)
//   {
//     max7219.Clean();
//     max7219.DecodeOn();

//     xSemaphoreTake(mutexSerial, portMAX_DELAY);
//     Serial.printf("[%8lu] Run Task2 on Core: %d\r\n", millis(), xPortGetCoreID());
//     xSemaphoreGive(mutexSerial);
    
//     xSemaphoreTake(mutexSPI, portMAX_DELAY);
//     max7219.PrintNtos(8, millis(), 8);
//     xSemaphoreGive(mutexSPI);

//     vTaskDelay(973 / portTICK_PERIOD_MS);
//   }  
// }

void Task3(void* parameters)
{
  pinMode(KEY_PIN, INPUT);

  while (1)
  {
    if(!digitalRead(KEY_PIN) && !keyFlag)
    {
      keyFlag = true;

      xSemaphoreTake(mutexSerial, portMAX_DELAY);
      Serial.printf("[%8lu] KEY is pressed!\r\n", millis());
      xSemaphoreGive(mutexSerial);      
    }

    if (digitalRead(KEY_PIN) && keyFlag)
    {
      keyFlag = false;

      xSemaphoreTake(mutexSerial, portMAX_DELAY);
      Serial.printf("[%8lu] KEY is unpressed!\r\n", millis());
      xSemaphoreGive(mutexSerial);

      if (task4Active)
      {
        task4Active = false;
        vTaskSuspend(Task2Handle);
      }
      else
      {
        task4Active = true;        
        vTaskResume(Task2Handle);
      }           
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  
}

// void Task4(void* parameters)
// { 
//   uint8_t brightness[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 
//                            0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
  
//   while (1)
//   {
//       for (size_t i = 0; i < sizeof(brightness) / sizeof(brightness[0]); i++)
//       {
//         xSemaphoreTake(mutexSPI, portMAX_DELAY);
//         max7219.SetIntensivity(brightness[i]);
//         xSemaphoreGive(mutexSPI);

//         vTaskDelay(150 / portTICK_PERIOD_MS); 
//       }            
//   }  
// }

void Task5(void* parameters)
{
  ledcSetup(0, 5000, 8);
  ledcAttachPin(LED_BLUE, 0);

  uint8_t n = 0;
  bool direction = false;

  while (1)
  {  
    if (!direction && n == 255)    
    {
      direction = true;
    }
    
    if (direction && n == 0)
    {
      direction = false;
    }
    
    ledcWrite(0, n);

    if (!direction)
    {
      n++;
    }
    else
    {
      n--;
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

void printData() {
  // If enablePrint flag is set to true (checkbox on screen is checked)...
  if (enablePrint) {
    // ...print the number to Serial
    Serial.print("Number is: ");
    Serial.println(number);
  } else {
    Serial.println("Printing is disabled, sorry:(");
  }
}

void applyInvert() {
  menu.invertKeysDuringEdit(invert);
  // Print invert variable to Serial
  Serial.print("Invert: ");
  Serial.println(invert);
}
