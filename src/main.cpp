#include <Arduino.h>
#include <Wire.h>
#include <GEM_u8g2.h>
#include "X9C10X_H595.h"
#include <KeyDetector.h>

#define KEY_PIN     0
#define LED_BLUE    2

// Define signal identifiers for three outputs of encoder (channel A, channel B and a push-button)
#define KEY_A 1
#define KEY_B 2
#define KEY_C 3

void setupMenu();
void Task0(void* parameters);
void Task1(void* parameters);

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


// X9C10X pot(10000);
HC595 hc595;

bool keyFlag = false;
bool task4Active = true;

TaskHandle_t Task2Handle = NULL;

SemaphoreHandle_t mutexSPI;
SemaphoreHandle_t mutexSerial;

void setup() 
{
  Serial.begin(115200);
  Serial.println("Serial is OK!");
  u8g2.begin();
  Serial.println();
  Serial.print("X9C10X_LIB_VERSION: ");

  pinMode(channelA, INPUT_PULLUP);
  pinMode(channelB, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);

  hc595.begin();
  mutexSPI = xSemaphoreCreateMutex();
  mutexSerial = xSemaphoreCreateMutex();

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
  int direction = 1;
  for (uint8_t i = 0; i < 12; i++)
  {
    if(direction)
    {
      hc595.pot[0].incr();
      hc595.pot[1].incr();
      hc595.pot[2].incr();
      hc595.pot[3].incr();
      // Serial.print('+');
    } 
    else
    {
      hc595.pot[0].decr();
      hc595.pot[1].decr();
      hc595.pot[2].decr();
      hc595.pot[3].decr();
      // Serial.print('-'); 
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if(i == 11)
    {
      i = 0;
      if(direction)
        direction = 0;
      else
        direction = 1;

      Serial.println();
    }  
  }
  vTaskDelete(NULL);
}

void Task1(void* parameters)
{
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
