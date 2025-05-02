#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NewPing.h> // Include the NewPing library

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID "ABC"
#define WIFI_PASSWORD "87654321"

// Insert Firebase project API Key
#define API_KEY "AIzaSyDIXYc05Lkl8ZITW76Y907FolQYMUbw8tc"

// Insert RTDB URL
#define DATABASE_URL "https://gate-system-620fc-default-rtdb.asia-southeast1.firebasedatabase.app/" 

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Servo pin
#define SERVO_PIN 14

// Servo object
Servo gateServo;

// LCD I2C address (adjust as needed, commonly 0x27 or 0x3F)
#define LCD_ADDR 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// LCD object
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLUMNS, LCD_ROWS);

// Define the pins for the ultrasonic sensor
#define TRIGGER_PIN  12
#define ECHO_PIN     13
#define MAX_DISTANCE 200 // Maximum distance to measure (in centimeters)

// Create a NewPing object
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

bool signupOK = false;
bool gateOpening = false;
bool gateClosing = false;
String lastApprovalStatus = "No";
String lastRequestStatus = "No";

// Define pins for Serial2
#define RXp2 16
#define TXp2 17

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXp2, TXp2);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing....");
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  
  // Initialize Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Sign up
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup ok");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
    return;
  }
  delay(1000);

  // Attach servo to pin
  gateServo.attach(SERVO_PIN);
  
  // Initialize gate position (closed)
  gateServo.write(0);
  Serial.println("Gate initialized to closed position");
}

void loop() {
  // Check distance from the ultrasonic sensor
  unsigned int uS = sonar.ping(); // Measure the distance
  float distance = uS / US_ROUNDTRIP_CM; // Convert the distance to centimeters

  // Display "Scan NFC or QR" based on the distance
  displayInitialMessage(distance);
  
  if (Firebase.ready() && signupOK) {
    // Read the approval and request statuses from Firebase
    if (Firebase.RTDB.getString(&fbdo, "approval")) {
      String approvalStatus = fbdo.stringData();
      if (approvalStatus != lastApprovalStatus) {
        lastApprovalStatus = approvalStatus;
        if (approvalStatus == "Yes") {
          openGate();
        }
      }
    } else {
      Serial.println("Failed to read approval status");
      Serial.println("REASON: " + fbdo.errorReason());
    }
    
    if (Firebase.RTDB.getString(&fbdo, "Request")) {
      String requestStatus = fbdo.stringData();
      if (requestStatus != lastRequestStatus) {
        lastRequestStatus = requestStatus;
        if (requestStatus == "Yes") {
          handleRequest();
          if (Firebase.RTDB.setString(&fbdo, "Request", "No")) {
            Serial.println("Firebase updated to No");
          } else {
            Serial.println("Failed to update Firebase to No");
            Serial.println("REASON: " + fbdo.errorReason());
          }
        } else if (requestStatus == "No") {
          displayInitialMessage(distance); // Go back to the initial message
        }
      }
    } else {
      Serial.println("Failed to read request status");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }

  // Read serial input from Serial2
  if (Serial2.available()) {
    String serialInput = Serial2.readStringUntil('\n');
    serialInput.trim(); // Remove any whitespace or newline characters

    if (serialInput == "Yes") {
      Serial.println("Opening gate");
      lcd.clear();
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print("Gate Open");
      lcd.setCursor(0, 1);
      lcd.print("Close in: 10");
      gateServo.write(90); // Adjust angle as needed
      gateOpening = true;
      gateClosing = false;
      
      // Countdown before closing the gate
      for (int i = 10; i >= 0; i--) {
        lcd.setCursor(0, 1);
        lcd.print("Close in: ");
        lcd.print(i);
        lcd.print(" sec");
        delay(1000); // Wait for one second
      }
      
      closeGate();
    }
  }

  delay(1000); // Check every second
}

void displayInitialMessage(float distance) {
  if (distance > 2 && distance < 5) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scan NFC or");
    lcd.setCursor(0, 1);
    lcd.print("Scan QR");
    lcd.backlight(); // Ensure the backlight is on
  } else {
    lcd.clear();
    lcd.noBacklight(); // Turn off the backlight
  }
}

void openGate() {
  Serial.println("Opening gate");
  lcd.clear();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Gate Open");
  lcd.setCursor(0, 1);
  lcd.print("Close in: 10");
  gateServo.write(90); // Adjust angle as needed
  gateOpening = true;
  gateClosing = false;
  
  // Countdown before closing the gate
  for (int i = 10; i >= 0; i--) {
    lcd.setCursor(0, 1);
    lcd.print("Close in: ");
    lcd.print(i);
    lcd.print(" sec");
    delay(1000); // Wait for one second
    if (Firebase.RTDB.getString(&fbdo, "approval") && fbdo.stringData() == "No") {
      Serial.println("Approval status changed to No during countdown");
      break;
    }
  }
  
  closeGate();
}

void closeGate() {
  Serial.println("Closing gate");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gate Closing");
  gateServo.write(0); // Adjust angle as needed
  gateClosing = true;
  gateOpening = false;
  delay(1000); // Wait for the servo to move
  
  // Update Firebase
  if (Firebase.RTDB.setString(&fbdo, "approval", "No")) {
    Serial.println("Firebase updated to No");
  } else {
    Serial.println("Failed to update Firebase to No");
    Serial.println("REASON: " + fbdo.errorReason());
  }

  // Turn off the LCD
  lcd.clear();
  lcd.noBacklight(); // Turn off the LCD backlight
  delay(1000); // Wait a moment before checking again
  
}

void handleRequest() {
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Request send");
  lcd.setCursor(0, 1);
  lcd.print("Waiting: 10");
  
  // Countdown before checking approval status
  for (int i = 10; i >= 0; i--) {
    lcd.setCursor(0, 1);
    lcd.print("Waiting: ");
    lcd.print(i);
    lcd.print(" sec");
    delay(1000); // Wait for one second
    if (Firebase.RTDB.getString(&fbdo, "approval") && fbdo.stringData()=="Yes") {
      openGate();
      return;
    }
  }
  
  displayInitialMessage(sonar.ping() / US_ROUNDTRIP_CM); // Go back to the initial message
}
