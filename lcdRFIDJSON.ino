#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>  // Ensure compatibility; use LiquidCrystal_PCF8574 if needed

const char* ssid = "TP-Link_IoT_2DEE";  // Replace with your Wi-Fi SSID
const char* password = "63158977";      // Replace with your Wi-Fi password

// API endpoints
const char* morningApi = "https://medimate-backend-production.up.railway.app/api/prescriptions/morning";
const char* dayApi = "https://medimate-backend-production.up.railway.app/api/prescriptions/day";
const char* nightApi = "https://medimate-backend-production.up.railway.app/api/prescriptions/night";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 6 * 3600, 60000);  // UTC+6 for Dhaka

// LCD and RFID Setup
#define RST_PIN 4                    // RST pin for RFID
#define SS_PIN 5                     // SDA (SS) pin for RFID
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Adjust LCD address if necessary
MFRC522 rfid(SS_PIN, RST_PIN);       // Create MFRC522 instance

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

  // Initialize time client, LCD, and RFID
  timeClient.begin();
  lcd.init();
  lcd.backlight();
  SPI.begin();
  rfid.PCD_Init();

  lcd.setCursor(0, 0);
  lcd.print("ESP32 RFID System");
}

void loop() {
  timeClient.update();
  int currentHour = timeClient.getHours();

  // Determine API endpoint based on time
  const char* apiEndpoint;
  if (currentHour >= 4 && currentHour < 6) {
    apiEndpoint = morningApi;
  } else if (currentHour >= 6 && currentHour < 16) {
    apiEndpoint = dayApi;
  } else if (currentHour >= 18 || currentHour == 0) {
    apiEndpoint = nightApi;
  } else {
    Serial.print(currentHour);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Outside working hour");
    delay(60000);
    return;
  }

  // Fetch data from the selected API
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(apiEndpoint);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Received JSON payload:");
      Serial.println(payload);

      DynamicJsonDocument doc(4096);  // Adjust size if necessary
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      for (JsonObject item : doc.as<JsonArray>()) {
        const char* id = item["id"];
        const char* name = item["name"];
        const char* wardNumber = item["wardNumber"];
        const char* bedNumber = item["bedNumber"];
        const char* rfidCardNumber = item["rfidCardNumber"];
        const char* medicineDistributionCompleted = item["medicineDistributionCompleted"];

        // Check if medicine distribution is not completed
        if (strcmp(medicineDistributionCompleted, "No") == 0) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Patient:");
          lcd.setCursor(0, 1);
          lcd.print(name);
          lcd.setCursor(0, 2);
          lcd.print("Ward: ");
          lcd.print(wardNumber);
          lcd.print(" Bed: ");
          lcd.print(bedNumber);

          // Wait for RFID scan
          Serial.println("Waiting for RFID scan...");
          while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
            delay(100);
          }

          // Format the scanned UID to match API UID format "A7 DF 25 03"
          String scannedUID = "";
          for (byte i = 0; i < rfid.uid.size; i++) {
            if (rfid.uid.uidByte[i] < 0x10) {
              scannedUID += "0";  // Add leading zero for single hex digits
            }
            scannedUID += String(rfid.uid.uidByte[i], HEX);
            if (i < rfid.uid.size - 1) scannedUID += " ";
          }
          scannedUID.toUpperCase();  // Convert to uppercase to match API format

          // Print the scanned UID and expected UID to Serial Monitor for debugging
          Serial.print("Scanned UID: ");
          Serial.println(scannedUID);
          Serial.print("Expected UID: ");
          Serial.println(rfidCardNumber);

          // Compare scanned UID with patient RFID from the API
          if (scannedUID.equalsIgnoreCase(rfidCardNumber)) {
            Serial.println("RFID matched! Sending confirmation...");

            // Send POST request to mark as completed
            String postApi = String(apiEndpoint) + "/" + id;
            http.begin(postApi);
            http.addHeader("Content-Type", "application/json");
            int postResponseCode = http.POST("{}");

            if (postResponseCode == 200) {
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("Medicine Distribution");
              lcd.setCursor(0, 1);
              lcd.print("Completed Successfully");
              Serial.println("Medicine Distribution Completed");
              delay(2000);
              lcd.clear();

            } else {
              Serial.print("Error on POST request: ");
              Serial.println(postResponseCode);
            }

            http.end();
          } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("RFID Mismatch!");
            Serial.println("RFID Mismatch!");
          }

          delay(2000);        // Wait before continuing to avoid multiple scans
          rfid.PICC_HaltA();  // Halt current card
          return;
        }
      }
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("Wi-Fi not connected. Reconnecting...");
    WiFi.reconnect();
  }

  delay(50000);  // Wait before the next request
}
