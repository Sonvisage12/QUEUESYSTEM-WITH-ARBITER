#include <Ticker.h>
#include <PxMatrix.h> //https://github.com/2dom/PxMatrix

Ticker display_ticker;


// Pin Definition for Nodemcu to HUB75 LED MODULE
#define P_LAT 16 //nodemcu pin D0
#define P_A 5    //nodemcu pin D1
#define P_B 4    //nodemcu pin D2
#define P_C 15   //nodemcu pin D8
#define P_OE 2   //nodemcu pin D4
#define P_D 12   //nodemcu pin D6
#define P_E 0    //nodemcu pin GND // no connection

PxMATRIX display(128, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D);

// Single color (Red)
uint16_t myRED = display.color565(255, 0, 0);

// ISR for display refresh
void display_updater() {
  display.display(100);
}



void setup() {
  // Initialize display
  display.begin(16);
  display.clearDisplay();
  display.setTextColor(myRED); // Set default text color to Red
  Serial.begin(115200);

  // Initialize WiFiManager
  //WiFiManager wifiManager;

  // Uncomment the following line to reset saved settings (for testing)
  // wifiManager.resetSettings();

  // Set a timeout for the configuration portal
  //wifiManager.setTimeout(180); // 3 minutes

  // Attempt to connect to Wi-Fi or launch the configuration portal
 // if (!wifiManager.autoConnect("MediboardsAP")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.reset(); // Reset and try again
    delay(5000);
  

  Serial.println("Connected to WiFi");

  // Start display ticker
  display_ticker.attach(0.002, display_updater);

  // Display initial message
  display.clearDisplay();
  display.setCursor(5, 1);
  display.setTextSize(2);
  display.setTextColor(myRED); // Use Red color
  display.print("MEDIBOARDS ");
  display.setCursor(5, 17);
  display.setTextSize(2);
  display.setTextColor(myRED); // Use Red color
  display.print("BY SONVISAGE ");
  delay(6000);

  // Check API
   
}


void loop() { 



    //display.clearDisplay();
    display.setCursor(4, 1);
    display.setTextSize(2);
    display.setTextColor(myRED);
    display.print("NEXT: ");
 
    
     delay(15000);
  
    //display.clearDisplay();
    }
    
