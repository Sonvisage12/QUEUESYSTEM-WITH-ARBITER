#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <map>
#include <queue>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>

// ==== 🔥 Define QueueItem ====
struct QueueItem {
  char uid[20];
  char timestamp[25];
  int number;
  bool removeFromQueue;
};

#define GREEN_LED_PIN 15
#define RED_LED_PIN   2
#define RST_PIN       5
#define SS_PIN        4

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences preferences;

uint8_t myMAC[6];
int patientNum = 0;
QueueItem currentPatient;

uint8_t displayMAC[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
uint8_t peer3[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t peer2[] = {0x30, 0xC6, 0xF7, 0x44, 0x1D, 0x24};
uint8_t peer1[] = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};
std::vector<uint8_t*> arrivalNodes = { peer1, peer2, peer3 };

int currentArrivalIndex = 0;
bool waitingForPatient = false;

void sendRequestToArrival() {
  // Construct a QueueItem as a "request marker" with removeFromQueue = false
  QueueItem requestItem;
  strncpy(requestItem.uid, "REQ_NEXT", sizeof(requestItem.uid));
  strncpy(requestItem.timestamp, "", sizeof(requestItem.timestamp));  // Empty timestamp
  requestItem.number = 0;  // Can be set to 0 or any dummy number
  requestItem.removeFromQueue = false;  // Ensure the Arrival Node won't delete

  esp_now_send(arrivalNodes[currentArrivalIndex], (uint8_t*)&requestItem, sizeof(requestItem));
  Serial.printf("📤 Sent REQ_NEXT (removeFromQueue=false) to Arrival Node %d\n", currentArrivalIndex + 1);
  waitingForPatient = true;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.printf("📥 Data received from ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", info->src_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.printf(" | Length: %d\n", len);

  if (len == sizeof(QueueItem)) {
    memcpy(&currentPatient, data, sizeof(QueueItem));
    patientNum = currentPatient.number;

    Serial.println("🔎 Extracted Patient Data:");
    Serial.printf("  UID: %s\n", currentPatient.uid);
    Serial.printf("  Number: %d\n", currentPatient.number);
    Serial.printf("  Timestamp: %s\n", currentPatient.timestamp);
    Serial.printf("  RemoveFromQueue: %s\n", currentPatient.removeFromQueue ? "true" : "false");

    esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
    Serial.printf("📤 Forwarded Patient No: %d to Display Node\n", patientNum);
    waitingForPatient = false;
  } else {
    Serial.println("⚠️ Received unknown data or format");

    if (waitingForPatient) {
      currentArrivalIndex++;
      if (currentArrivalIndex < arrivalNodes.size()) {
        sendRequestToArrival();
      } else {
        Serial.println("📭 All Arrival Nodes tried, queue might be empty.");
        patientNum = 0;
        esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
        waitingForPatient = false;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.macAddress(myMAC);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  for (auto mac : arrivalNodes) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&peerInfo);
  }

  esp_now_peer_info_t displayPeer = {};
  memcpy(displayPeer.peer_addr, displayMAC, 6);
  displayPeer.channel = 1;
  displayPeer.encrypt = false;
  if (!esp_now_is_peer_exist(displayMAC)) esp_now_add_peer(&displayPeer);

  Serial.println("👨‍⚕️ Doctor Node Ready");
  sendRequestToArrival();  // Send initial "REQ_NEXT" as a QueueItem with removeFromQueue = false
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);

  uid.toUpperCase();

  if (patientNum != 0 && uid == String(currentPatient.uid)) {
    Serial.printf("✅ Patient No %d attended\n", patientNum);
    currentPatient.removeFromQueue = true;
    esp_now_send(arrivalNodes[currentArrivalIndex], (uint8_t*)&currentPatient, sizeof(currentPatient));
    delay(100);
    currentArrivalIndex = 0;
    sendRequestToArrival();
    blinkLED(GREEN_LED_PIN);
  } else {
    Serial.println("❌ Access Denied");
    blinkLED(RED_LED_PIN);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1000);
}

String getUIDString(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uid += "0";
    uid += String(buffer[i], HEX);
  }
  return uid;
}

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(500);
  digitalWrite(pin, HIGH);
}
