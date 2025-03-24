/********************************************************************************************************************
*****************************    Author  : Ehab Magdy Abdullah                      *********************************
*****************************    Linkedin: https://www.linkedin.com/in/ehabmagdyy/  *********************************
*****************************    Youtube : https://www.youtube.com/@EhabMagdyy      *********************************
*********************************************************************************************************************/

#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#elif ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#include <LittleFS.h>
// File path to save the downloaded file
#define SPIFFS_FilePath       "/update.bin"
#endif
#include <ArduinoJson.h>

// Wi-Fi credentials
#define SSID                  "SSID"
#define PASSWORD              "PASSWORD"

// Supabase credentials
#define supabaseUrl           "supabaseUrl"
#define supabaseAPIKey        "supabaseAPIKey"
#define tableName             "tableName"

// Current firmware version
const String CURRENT_FIRMWARE_VERSION = "1.0";

// Variables to store JSON data
String firmwareVersion;
String updateFileUrl;

void setup()
{
  Serial.begin(115200);

#ifdef ESP8266
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed. Attempting to format...");
    if (LittleFS.format()) {
      Serial.println("LittleFS formatted successfully.");
      if (!LittleFS.begin()) {
        Serial.println("Failed to mount after formatting. Halt.");
        while (1); // Stop here if formatting fails
      }
    } else {
      Serial.println("Formatting failed. Halt.");
      while (1);
    }
  } else {
    Serial.println("LittleFS mounted successfully");
  }
#endif

  ConnectToWiFi();
}

void loop()
{
  Serial.println("\nCurrent Version: " + CURRENT_FIRMWARE_VERSION);
  CheckForNewUpdate();
  delay(5000);
}

void ConnectToWiFi()
{
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
}

// Check for new updates 
void CheckForNewUpdate()
{

#ifdef ESP32
  HTTPClient http;
  String endpoint = String(supabaseUrl) + "/rest/v1/" + tableName + "?id=eq.1&select=version,update_file_url";
  http.begin(endpoint);
#elif ESP8266
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String endpoint = String(supabaseUrl) + "/rest/v1/" + tableName + "?id=eq.1&select=version,update_file_url";
  http.begin(client, endpoint);
#endif
  http.addHeader("apikey", supabaseAPIKey);

  int httpResponseCode = http.GET();
  if (httpResponseCode == HTTP_CODE_OK)
  {
    String response = http.getString();
    Serial.println("Response: " + response);
    http.end();

    // Parsing Response & Extracts Version and Update File URL
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.f_str());
      return;
    }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) {
      Serial.println("Empty array or invalid JSON structure");
      return;
    }
    JsonObject obj = arr[0];
    firmwareVersion = obj["version"].as<String>();
    updateFileUrl   = obj["update_file_url"].as<String>();

    Serial.print("Update Version: "); Serial.println(firmwareVersion);
    Serial.print("Update File URL: "); Serial.println(updateFileUrl);

    double currentVersionNum = CURRENT_FIRMWARE_VERSION.toDouble();
    double newVersionNum = firmwareVersion.toDouble();

    if(newVersionNum > currentVersionNum)
    {
      #ifdef ESP32
      ESP32UpdateFirwmare(updateFileUrl);
      #elif ESP8266
      ESP8266UpdateFirwmare(updateFileUrl);
      #endif
    }
    else{ Serial.println("Application is up to data!"); }
  }
  else
  {
    Serial.println("Error in HTTP request");
    Serial.println("HTTP Response code: " + String(httpResponseCode));
    String response = http.getString();
    Serial.println("Response: " + response);
    http.end();
  }

}

#ifdef ESP32
void ESP32UpdateFirwmare(String updateFileUrl)
{
  HTTPClient http;
  http.begin(updateFileUrl);

  Serial.println("\nDownloading update file...");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    // reference to the data stream of the http response
    WiFiClient& client = http.getStream();
    uint32_t firmwareSize = http.getSize();
    Serial.print("Firmware Size: ");
    Serial.println(firmwareSize);

    if (Update.begin(firmwareSize))
    {
      Serial.println("Performing OTA Update...");
      size_t written = Update.writeStream(client);

      if (Update.size() == written)
      {
        Serial.println("Update successfully completed. Rebooting...");

        if (Update.end())
        {
          Serial.println("Rebooting...");
          ESP.restart();
        } 
        else 
        {
          Serial.print("Update failed: ");
          Serial.println(Update.errorString());
        }
      }
      else{ Serial.println("Not enough space for OTA."); }
    } 
    else{ Serial.println("Failed to begin OTA update."); }
  }

  http.end();
}
#endif

#ifdef ESP8266
void ESP8266UpdateFirwmare(String updateFileUrl)
{
    const size_t chunkSize = 16384; // 16KB chunks
    const int maxRetries = 3;       // Max retries per chunk
    HTTPClient http;
    WiFiClientSecure client;

    // Configure client
    client.setInsecure();
    client.setTimeout(15000);
    client.setBufferSizes(4096, 1024);

    // Get total file size using HEAD request
    Serial.println("Getting file size...");
    http.begin(client, updateFileUrl);
    int httpCode = http.sendRequest("HEAD");
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Failed to get file size, HTTP code: %d\n", httpCode);
        http.end();
        return;
    }
    int totalSize = http.getSize();
    if (totalSize <= 0) {
        Serial.println("Invalid Content-Length");
        http.end();
        return;
    }
    http.end();
    Serial.printf("Total file size: %d bytes\n", totalSize);

    // Check available space in LittleFS
    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t freeSpace = fs_info.totalBytes - fs_info.usedBytes;
    if (freeSpace < totalSize) {
        Serial.printf("Not enough space in file system: needed %d, available %d\n", totalSize, freeSpace);
        return;
    }

    // Open file for writing
    File file = LittleFS.open(SPIFFS_FilePath, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    // Download file in chunks
    Serial.printf("Free heap before download: %d bytes\n", ESP.getFreeHeap());
    Serial.println("Downloading update file...");
    size_t position = 0;
    while (position < totalSize) {
        size_t end = position + chunkSize - 1;
        if (end >= totalSize) end = totalSize - 1;
        String range = "bytes=" + String(position) + "-" + String(end);
        size_t expected = end - position + 1;

        bool success = false;
        for (int retry = 0; retry < maxRetries; retry++) {
            http.begin(client, updateFileUrl);
            http.addHeader("Range", range);
            http.setTimeout(15000);
            httpCode = http.GET();

            if (httpCode == HTTP_CODE_PARTIAL_CONTENT) {
                WiFiClient& stream = http.getStream();
                size_t bytesRead = 0;
                uint8_t buffer[256]; // Small buffer to manage memory
                while (bytesRead < expected && stream.connected()) {
                    size_t available = stream.available();
                    if (available) {
                        size_t toRead = min(sizeof(buffer), available);
                        size_t readNow = stream.readBytes(buffer, toRead);
                        if (readNow > 0) {
                            file.seek(position + bytesRead);
                            size_t written = file.write(buffer, readNow);
                            if (written != readNow) {
                                Serial.println("Write error: failed to write to file");
                                break;
                            }
                            bytesRead += written;
                        }
                    }
                    delay(1); // Yield to avoid watchdog timeout
                }
                if (bytesRead == expected) {
                    position += bytesRead;
                    Serial.printf("Downloaded chunk %d-%d, written %d bytes (%.1f%%)\n",
                                  position - bytesRead, end, bytesRead, (position * 100.0) / totalSize);
                    Serial.printf("Free heap after chunk: %d bytes\n", ESP.getFreeHeap());
                    success = true;
                    break;
                } else {
                    Serial.printf("Incomplete read: expected %d, read %d\n", expected, bytesRead);
                }
            } else {
                Serial.printf("HTTP error: %d for range %s\n", httpCode, range.c_str());
            }
            http.end();
            if (retry < maxRetries - 1) {
                Serial.printf("Retry %d of %d...\n", retry + 1, maxRetries);
                delay(1000); // Wait before retry
            }
        }
        if (!success) {
            Serial.println("Failed to download chunk after retries");
            file.close();
            return;
        }
    }
    file.close();

    // Verify downloaded file size
    File checkFile = LittleFS.open(SPIFFS_FilePath, "r");
    if (!checkFile) {
        Serial.println("Failed to verify downloaded file");
        return;
    }
    size_t downloadedSize = checkFile.size();
    checkFile.close();
    if (downloadedSize != totalSize) {
        Serial.printf("Download size mismatch: expected %d, got %d\n", totalSize, downloadedSize);
        return;
    }
    Serial.println("File downloaded successfully");

    // Perform OTA update
    File updateFile = LittleFS.open(SPIFFS_FilePath, "r");
    if (!updateFile) {
        Serial.println("Failed to open file for update");
        return;
    }

    size_t fileSize = updateFile.size();
    Serial.printf("Starting OTA update from file: %s, size: %d bytes\n", SPIFFS_FilePath, fileSize);

    if (Update.begin(fileSize)) {
        size_t written = Update.writeStream(updateFile);
        if (written == fileSize) {
            Serial.println("Update successfully written.");
            if (Update.end()) {
                if (Update.isFinished()) {
                    Serial.println("Update successfully completed. Rebooting...");
                    ESP.restart();
                } else {
                    Serial.println("Update not finished. Something went wrong.");
                }
            } else {
                Serial.println("Update failed");
            }
        } else {
            Serial.printf("Update failed. Written only %d/%d bytes\n", written, fileSize);
        }
    } else {
        Serial.println("Not enough space to begin OTA");
    }
    updateFile.close();
}
#endif