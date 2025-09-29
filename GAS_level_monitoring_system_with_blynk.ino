// Blynk credentials - MUST BE BEFORE INCLUDING LIBRARIES
#define BLYNK_TEMPLATE_ID "TMPL6hwth8ktC"
#define BLYNK_TEMPLATE_NAME "GAS level monitoring system"
#define BLYNK_AUTH_TOKEN "N_a08cz6N5Glqv_ZMdY_iIQFe6MvnHN7"

//Include the library files
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>  // DHT sensor library

// Define sensor and actuator pins
#define GAS_SENSOR 34
#define BUZZER 2
#define DHT_PIN 4      // Change to your DHT11 data pin
#define FLAME_SENSOR 5 // Change to your flame sensor pin
#define LED_PIN 15     // Define the pin for the external LED (D15) - NEW
#define DHT_TYPE DHT11 // DHT sensor type

// Notification timing variables
unsigned long lastGasNotificationTime = 0;
unsigned long lastFlameNotificationTime = 0;
unsigned long lastTempNotificationTime = 0;
const unsigned long notificationDelay = 60000; // 1 minute between notifications

// Initialize the LCD display
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Initialize DHT sensor
DHT dht(DHT_PIN, DHT_TYPE);

BlynkTimer timer;

//WiFi credentials
char ssid[] = "asparagus"; // <-- Make sure to put your actual WiFi SSID here
char pass[] = "235235235"; // <-- Make sure to put your actual WiFi Password here

// Variables to track alarm status
bool gasAlarm = false;
bool flameAlarm = false;

// Debug function that uses LCD instead of Serial
void lcdDebug(String message, int line = 0, bool clearScreen = true) {
  if (clearScreen) {
    lcd.clear();
  }
  lcd.setCursor(0, line);
  lcd.print(message);
  delay(1000); // Short delay to make messages readable
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);

  // Initialize I2C
  Wire.begin();

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcdDebug("LCD OK", 0, true);

  // Initialize sensors, buzzer, and LED
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_PIN, OUTPUT); // Initialize the LED pin as output - NEW
  pinMode(FLAME_SENSOR, INPUT);
  dht.begin();
  digitalWrite(BUZZER, LOW); // Ensure buzzer is off initially
  digitalWrite(LED_PIN, LOW);  // Ensure LED is off initially - NEW
  lcdDebug("Sensors OK", 1, false);
  delay(1000);

  // Connect to WiFi and Blynk
  lcdDebug("WiFi: Connecting", 0, true);
  lcdDebug(ssid, 1, false);

  // Try to connect to Blynk with timeout
  int timeout = 0;
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);

  while (!Blynk.connected() && timeout < 20) {
    delay(500);
    // Avoid printing dots over the SSID if it's long
    if (strlen(ssid) < 16) {
        lcd.setCursor(timeout, 1);
        lcd.print(".");
    }
    timeout++;
    Serial.print("."); // Also print dots to Serial monitor for debugging
  }
   Serial.println(); // Newline after connection attempt dots

  if (Blynk.connected()) {
    lcdDebug("Blynk connected!", 0, true);
    Serial.println("Blynk connected!");
  } else {
    lcdDebug("Blynk failed", 0, true);
    lcdDebug("Check credentials", 1, false);
    Serial.println("Blynk connection failed. Check credentials/network.");
    delay(3000);
  }

  // Set up timer for sensor readings
  timer.setInterval(2000L, sendSensorData); // Read sensors every 2 seconds

  // Show system ready
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("System Loading");
  for (int a = 0; a <= 15; a++) {
    lcd.setCursor(a, 1);
    lcd.print(".");
    delay(100);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
  delay(1000);
  lcd.clear();
}

// Function to read all sensors and send data to Blynk
void sendSensorData() {
  // Read gas sensor
  int gasValueRaw = analogRead(GAS_SENSOR);
  // Map the 12-bit ADC range (0-4095) to a percentage (0-100)
  int gasValue = map(gasValueRaw, 0, 4095, 0, 100);

  // Read flame sensor (digital signal - LOW when flame detected)
  bool flameDetected = !digitalRead(FLAME_SENSOR); // Invert logic: LOW means detected (true)

  // Read temperature and humidity from DHT11
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Check if any reading failed (often returns NaN - Not a Number)
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    // Optional: use default values or last known good values
    // For simplicity, setting to 0 here if read fails.
    humidity = 0.0;
    temperature = 0.0;
  }

  // Update alarm states
  gasAlarm = (gasValue >= 50); // Trigger gas alarm if level is 50% or higher
  flameAlarm = flameDetected;  // Flame alarm is true if flameDetected is true

  // Activate buzzer AND LED if any alarm condition is met
  if (gasAlarm || flameAlarm) {
    digitalWrite(BUZZER, HIGH);
    digitalWrite(LED_PIN, HIGH); // Turn LED ON - NEW
    Serial.println("ALARM ACTIVE!"); // Debug output
  } else {
    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_PIN, LOW);  // Turn LED OFF - NEW
  }

  // Update Blynk if connected
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, gasValue);       // Send Gas level to Blynk V0
    Blynk.virtualWrite(V1, flameDetected ? 1 : 0); // Send Flame status (1=Detected, 0=None) to Blynk V1
    Blynk.virtualWrite(V2, temperature);    // Send Temperature to Blynk V2
    Blynk.virtualWrite(V3, humidity);       // Send Humidity to Blynk V3

    // Update LED widgets on Blynk app
    Blynk.virtualWrite(V4, gasAlarm ? 255 : 0);   // Gas alarm LED on Blynk (V4)
    Blynk.virtualWrite(V5, flameAlarm ? 255 : 0); // Flame alarm LED on Blynk (V5)

    // Send LCD content to Blynk virtual pin V6
    String lcdContent = ""; // Will be filled below based on displayMode

    // --- Notification Logic ---
    unsigned long currentTime = millis(); // Get current time once for efficiency

    // Gas alert notification
    if (gasAlarm && (currentTime - lastGasNotificationTime > notificationDelay)) {
      String gasMsg = String("WARNING: Gas level at ") + gasValue + "%!";
      Blynk.logEvent("gas_alert", gasMsg);
      Serial.println(gasMsg);
      lastGasNotificationTime = currentTime;
    }

    // Flame alert notification
    if (flameAlarm && (currentTime - lastFlameNotificationTime > notificationDelay)) {
      String fireMsg = "DANGER: Fire detected!";
      Blynk.logEvent("fire_alert", fireMsg);
       Serial.println(fireMsg);
      lastFlameNotificationTime = currentTime;
    }

    // High temperature notification (optional - threshold is 40 C)
    if (temperature > 40.0 && (currentTime - lastTempNotificationTime > notificationDelay)) {
       String tempMsg = String("WARNING: High temperature detected: ") + temperature + " C";
       Blynk.logEvent("temp_alert", tempMsg);
       Serial.println(tempMsg);
       lastTempNotificationTime = currentTime;
    }
  } else {
    // Optional: Handle case where Blynk disconnects during operation
     Serial.println("Blynk disconnected, attempting to reconnect...");
     // Note: Blynk.run() in loop() will handle reconnection attempts automatically.
  }


  // Update LCD display with rotating sensor data
  static int displayMode = 0; // Static variable to remember the display mode
  lcd.clear();
  String lcdContentForBlynk = ""; // String to store LCD content for V6

  // Rotate between different sensor displays every 2 seconds (since sendSensorData runs every 2s)
  switch (displayMode) {
    case 0: // Display Gas Level and Status
      lcd.setCursor(0, 0);
      lcd.print("Gas Level: ");
      lcd.print(gasValue);
      lcd.print("%"); // Show percentage symbol
      lcd.setCursor(0, 1);
      if (gasAlarm) {
        lcd.print("GAS ALARM!");
        lcdContentForBlynk = "Gas: " + String(gasValue) + "%\nGAS ALARM!";
      } else {
        lcd.print("Gas: Normal");
        lcdContentForBlynk = "Gas: " + String(gasValue) + "%\nGas: Normal";
      }
      break;

    case 1: // Display Flame Sensor Status
      lcd.setCursor(0, 0);
      lcd.print("Flame Sensor:");
      lcd.setCursor(0, 1);
      if (flameAlarm) {
        lcd.print("FIRE DETECTED!");
        lcdContentForBlynk = "Flame Sensor\nFIRE DETECTED!";
      } else {
        lcd.print("Fire: None");
        lcdContentForBlynk = "Flame Sensor\nFire: None";
      }
      break;

    case 2: // Display Temperature and Humidity
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(temperature, 1); // Print temperature with 1 decimal place
      lcd.print((char)223); // Degree symbol
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Humidity: ");
      lcd.print(humidity, 1); // Print humidity with 1 decimal place
      lcd.print("%");
      lcdContentForBlynk = "Temp: " + String(temperature, 1) + " C\nHumidity: " + String(humidity, 1) + "%";
      break;
  }

   // Send the generated LCD content string to Blynk virtual pin V6 if connected
  if (Blynk.connected()) {
      Blynk.virtualWrite(V6, lcdContentForBlynk);
  }

  // Update display mode for the next cycle (cycles through 0, 1, 2)
  displayMode = (displayMode + 1) % 3;

  // Print sensor values to Serial monitor for debugging
  Serial.print("Gas: "); Serial.print(gasValue); Serial.print("% | ");
  Serial.print("Flame: "); Serial.print(flameDetected ? "DETECTED" : "None"); Serial.print(" | ");
  Serial.print("Temp: "); Serial.print(temperature); Serial.print(" *C | ");
  Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");

}

void loop() {
  if (Blynk.connected()) { // Only run Blynk functions if connected
    Blynk.run();
  } else {
    // Optional: Attempt to reconnect if disconnected for a while
    // Blynk.connect(); // Be careful with this, might block excessively
    Serial.println("Waiting for Blynk connection...");
    delay(1000); // Prevent tight loop spamming Serial if disconnected
     // Attempt reconnection via Blynk.begin (less aggressive than Blynk.connect())
     // This part might be redundant if Blynk.run() handles it well enough
     if (!Blynk.connected()) {
        Serial.println("Attempting manual reconnect...");
        Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);
        delay(2000); // Give it a moment to try
     }
  }
  timer.run(); // Always run the timer to keep sensor readings going
  // A small delay is often good practice but not strictly necessary if timer.run() dominates loop time.
  // delay(100); // Can be removed if timer interval is short enough
}