#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include "SharedQueue.h"

#define RST_PIN  5
#define SS_PIN   4
#define GREEN_LED_PIN 15
#define RED_LED_PIN   2

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences prefs;

SharedQueue sharedQueue("rfid-patients");

// MAC addresses for Arrival Nodes (7) and Doctor Nodes (4)
const uint8_t arrivalMAC1[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
const uint8_t arrivalMAC2[] = {0x30, 0xC6, 0xF7, 0x44, 0x1D, 0x24};
const uint8_t arrivalMAC3[] = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};
const uint8_t arrivalMAC4[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t arrivalMAC5[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
const uint8_t arrivalMAC6[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
const uint8_t arrivalMAC7[] = {0x9F, 0x8E, 0x7D, 0x6C, 0x5B, 0x4A};

const uint8_t doctorMAC1[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
const uint8_t doctorMAC2[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};
const uint8_t doctorMAC3[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x03};
const uint8_t doctorMAC4[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x04};

void onDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));

    const uint8_t* mac = recvInfo->src_addr;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("📩 Received from: ");
    Serial.println(macStr);

    std::vector<const uint8_t*> arrivalNodes = {arrivalMAC1, arrivalMAC2, arrivalMAC3, arrivalMAC4, arrivalMAC5, arrivalMAC6, arrivalMAC7};
    std::vector<const uint8_t*> doctorNodes = {doctorMAC1, doctorMAC2, doctorMAC3, doctorMAC4};

    bool isArrivalNode = std::any_of(arrivalNodes.begin(), arrivalNodes.end(),
                                     [mac](const uint8_t* node) { return memcmp(mac, node, 6) == 0; });
    bool isDoctorNode = std::any_of(doctorNodes.begin(), doctorNodes.end(),
                                     [mac](const uint8_t* node) { return memcmp(mac, node, 6) == 0; });

    if (isArrivalNode) {
        Serial.println("🔄 Handling Arrival Node message...");
        if (item.addToQueue && !item.removeFromQueue) {
            sharedQueue.addIfNew(String(item.uid), String(item.timestamp), item.number);
        } else if (!item.addToQueue && !item.removeFromQueue) {
            Serial.println("🔄 Sync message (optional)");
        }
    } else if (isDoctorNode) {
        Serial.println("👨‍⚕️ Handling Doctor Node message...");
        if (item.removeFromQueue) {
            // Doctor sends a remove request
            sharedQueue.removeByUID(String(item.uid));
        } else  {
            // Doctor sends a request for the next patient
            if (!sharedQueue.empty()) {
                QueueEntry entry = sharedQueue.front();

                QueueItem sendItem;
                strncpy(sendItem.uid, entry.uid.c_str(), sizeof(sendItem.uid));
                strncpy(sendItem.timestamp, entry.timestamp.c_str(), sizeof(sendItem.timestamp));
                sendItem.number = entry.number;
                sendItem.removeFromQueue = false;
                sendItem.addToQueue = false;

                esp_now_send(mac, (uint8_t*)&sendItem, sizeof(sendItem));

                sharedQueue.pop();
                sharedQueue.push(entry);
                Serial.println("✅ Sent next patient to doctor.");
            } else {
                Serial.println("⚠️ Queue is empty! No patient to send.");
            }
        }
    } else {
        Serial.println("❌ Unknown sender.");
    }

    sharedQueue.print();
}



void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("📤 Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered 🟢" : "Failed 🔴");
}

void setup() {
    Serial.begin(115200);
    prefs.begin("rfidMap", false);
    // Example UID mappings
    prefs.putUInt("13B6B1E3", 1); prefs.putUInt("13D7ADE3", 2); prefs.putUInt("A339D9E3", 3);
    prefs.putUInt("220C1805", 4); prefs.putUInt("638348E3", 5); prefs.putUInt("A3E9C7E3", 6);
    prefs.putUInt("5373BEE3", 7); prefs.putUInt("62EDFF51", 8); prefs.putUInt("131DABE3", 9);
    prefs.putUInt("B3D4B0E3", 10);
    prefs.end();

    SPI.begin();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.print("WiFi MAC Address: ");
    Serial.println(WiFi.macAddress());

    mfrc522.PCD_Init();
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, HIGH);

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed");
        return;
    }

    std::vector<const uint8_t*> peers = {arrivalMAC1, arrivalMAC2, arrivalMAC3, arrivalMAC4, arrivalMAC5, arrivalMAC6, arrivalMAC7,
                                         doctorMAC1, doctorMAC2, doctorMAC3, doctorMAC4};
    for (auto peer : peers) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, peer, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        if (!esp_now_is_peer_exist(peer)) {
            esp_now_add_peer(&peerInfo);
        }
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    sharedQueue.load();
    Serial.println("📋 RFID Arrival Node Ready. Waiting for card...");
    sharedQueue.print();
}

void loop() {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        return;
    }

    String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("🆔 Card UID detected: ");
    Serial.println(uid);

    if (sharedQueue.exists(uid)) {
        Serial.println("⏳ Already in queue. Wait for your turn.");
        blinkLED(RED_LED_PIN);
    } else {
        int pid = getPermanentNumber(uid);
        if (pid == -1) {
            blinkLED(RED_LED_PIN);
            return;
        }

        String timeStr = String(millis());
        sharedQueue.add(uid, timeStr, pid);

        QueueItem sendItem;
        strncpy(sendItem.uid, uid.c_str(), sizeof(sendItem.uid));
        strncpy(sendItem.timestamp, timeStr.c_str(), sizeof(sendItem.timestamp));
        sendItem.number = pid;
        sendItem.removeFromQueue = false;
        sendItem.addToQueue = true;

        std::vector<const uint8_t*> arrivals = {arrivalMAC1, arrivalMAC2, arrivalMAC3, arrivalMAC4, arrivalMAC5, arrivalMAC6, arrivalMAC7};
        for (auto peer : arrivals) {
            esp_now_send(peer, (uint8_t*)&sendItem, sizeof(sendItem));
        }

        Serial.printf("✅ Patient Registered: %d | Time: %s\n", pid, timeStr.c_str());
        blinkLED(GREEN_LED_PIN);
        sharedQueue.print();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(1500);
}


String getUIDString(byte *buffer, byte bufferSize) {
    String uidString = "";
    for (byte i = 0; i < bufferSize; i++) {
        if (buffer[i] < 0x10) uidString += "0";
        uidString += String(buffer[i], HEX);
    }
    uidString.toUpperCase();
    return uidString;
}

int getPermanentNumber(String uid) {
    prefs.begin("rfidMap", true);
    int pid = -1;
    if (prefs.isKey(uid.c_str())) {
        pid = prefs.getUInt(uid.c_str(), -1);
        Serial.printf("✅ Known UID: %s -> Assigned: %d\n", uid.c_str(), pid);
    } else {
        Serial.printf("❌ Unknown UID: %s → Access denied.\n", uid.c_str());
    }
    prefs.end();
    return pid;
}

void blinkLED(int pin) {
    digitalWrite(pin, LOW);
    delay(1000);
    digitalWrite(pin, HIGH);
}
