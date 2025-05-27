// Same includes as before...
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
bool isArrivalNode = false, isDoctorNode = false;
// MAC addresses
const uint8_t arrivalMACs[][6] = {
    {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98},
    {0x30, 0xC6, 0xF7, 0x44, 0x1D, 0x24},
    {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C},
    {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
    {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
    {0x9F, 0x8E, 0x7D, 0x6C, 0x5B, 0x4A}
};
const int numArrivalNodes = sizeof(arrivalMACs) / sizeof(arrivalMACs[0]);

const uint8_t doctorMACs[][6] = {
    {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C},
    {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02},
    {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x03},
    {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x04}
};



void onDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));

    const uint8_t* mac = recvInfo->src_addr;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("📩 Received from: ");
    Serial.println(macStr);

    // Check if sender is Arrival Node or Doctor Node
    isArrivalNode = isDoctorNode = false;
    for (int i = 0; i < numArrivalNodes; i++)
        if (memcmp(mac, arrivalMACs[i], 6) == 0) { isArrivalNode = true; break; }
    for (int i = 0; i < 4; i++)
        if (memcmp(mac, doctorMACs[i], 6) == 0) { isDoctorNode = true; break; }

    if (isArrivalNode) {
        Serial.println("🔄 Handling Arrival Node message...");
        if (item.addToQueue && !item.removeFromQueue) {
            sharedQueue.addIfNew(String(item.uid), String(item.timestamp), item.number);
        } else if (!item.addToQueue && item.removeFromQueue) {
            sharedQueue.removeByUID(String(item.uid));
            Serial.printf("🔁 Synchronized removal of UID: %s\n", item.uid);
        }
    } 
    else if (isDoctorNode) {
        Serial.println("👨‍⚕️ Doctor Node message received...");

        // Check if this is a "REQ_NEXT" request (Doctor requesting next patient)
        if (strcmp(item.uid, "REQ_NEXT") == 0) {
            Serial.println("📬 Handling 'REQ_NEXT' from Doctor...");

            if (!sharedQueue.empty()) {
                // Send the next patient to the doctor
                QueueEntry entry = sharedQueue.front();
                QueueItem sendItem;
                strncpy(sendItem.uid, entry.uid.c_str(), sizeof(sendItem.uid));
                strncpy(sendItem.timestamp, entry.timestamp.c_str(), sizeof(sendItem.timestamp));
                sendItem.number = entry.number;
                sendItem.addToQueue = false;
                sendItem.removeFromQueue = false;

                esp_now_send(mac, (uint8_t*)&sendItem, sizeof(sendItem));
                Serial.printf("✅ Sent Patient %d | UID: %s\n", entry.number, entry.uid.c_str());
            } else {
                // Send a zero patient if queue is empty
                QueueItem zeroItem = {};
                strncpy(zeroItem.uid, "NO_PATIENT", sizeof(zeroItem.uid));
                zeroItem.number = 0;
                zeroItem.addToQueue = false;
                zeroItem.removeFromQueue = false;

                esp_now_send(mac, (uint8_t*)&zeroItem, sizeof(zeroItem));
                Serial.println("⚠️ Queue empty. Sent 'NO_PATIENT' to Doctor.");
            }
        }
        else if (item.removeFromQueue) {
            // Doctor is requesting to remove a patient
            sharedQueue.removeByUID(String(item.uid));
            Serial.printf("🗑️ Doctor requested removal of UID: %s\n", item.uid);

            // Broadcast removal to other Arrival Nodes for synchronization
            item.addToQueue = false;
            item.removeFromQueue = true;
            for (int i = 0; i < numArrivalNodes; i++) {
                if (memcmp(arrivalMACs[i], WiFi.macAddress().c_str(), 6) != 0)
                    esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
            }
            Serial.println("📤 Broadcasted removal to Arrival Nodes.");
        }
    } 
    else {
        Serial.println("❌ Unknown or unhandled message.");
    }

    sharedQueue.print();
}





// Send status callback
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("📤 Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered 🟢" : "Failed 🔴");
}

void setup() {
    Serial.begin(115200);
    prefs.begin("rfidMap", false);
    prefs.putUInt("13B6B1E3", 1); prefs.putUInt("13D7ADE3", 2); prefs.putUInt("A339D9E3", 3);
    prefs.putUInt("220C1805", 4); prefs.putUInt("638348E3", 5); prefs.putUInt("A3E9C7E3", 6);
    prefs.putUInt("5373BEE3", 7); prefs.end();

    SPI.begin(); WiFi.mode(WIFI_STA);
    Serial.print("WiFi MAC: "); Serial.println(WiFi.macAddress());
    mfrc522.PCD_Init(); pinMode(GREEN_LED_PIN, OUTPUT); pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, HIGH); digitalWrite(RED_LED_PIN, HIGH);

    if (esp_now_init() != ESP_OK) { Serial.println("❌ ESP-NOW Init Failed"); return; }
    for (int i = 0; i < numArrivalNodes; i++) {
        esp_now_peer_info_t p={}; memcpy(p.peer_addr, arrivalMACs[i],6); p.channel=1; esp_now_add_peer(&p);
    }
    for (int i = 0; i < 4; i++) {
        esp_now_peer_info_t p={}; memcpy(p.peer_addr, doctorMACs[i],6); p.channel=1; esp_now_add_peer(&p);
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    sharedQueue.load(); Serial.println("📋 RFID Arrival Node Ready."); sharedQueue.print();
}

void loop() {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
    String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("🆔 Card UID: "); Serial.println(uid);
    if (sharedQueue.exists(uid)) { Serial.println("⏳ Already in queue."); blinkLED(RED_LED_PIN); }
    else {
        int pid = getPermanentNumber(uid);
        if (pid==-1) { blinkLED(RED_LED_PIN); return; }
        String timeStr = String(millis());
        sharedQueue.add(uid,timeStr,pid);
        QueueItem item; strncpy(item.uid, uid.c_str(), sizeof(item.uid));
        strncpy(item.timestamp, timeStr.c_str(), sizeof(item.timestamp));
        item.number=pid; item.addToQueue=true; item.removeFromQueue=false;
        for (int i=0; i<numArrivalNodes; i++) esp_now_send(arrivalMACs[i], (uint8_t*)&item, sizeof(item));
        Serial.printf("✅ Registered: %d | Time: %s\n", pid, timeStr.c_str());
        blinkLED(GREEN_LED_PIN); sharedQueue.print();
    }
    mfrc522.PICC_HaltA(); mfrc522.PCD_StopCrypto1(); delay(1500);
}

String getUIDString(byte *buf, byte size) {
    String uid=""; for (byte i=0;i<size;i++){if(buf[i]<0x10)uid+="0"; uid+=String(buf[i],HEX);} uid.toUpperCase(); return uid;
}
void blinkLED(int pin) { digitalWrite(pin, LOW); delay(1000); digitalWrite(pin, HIGH); }
int getPermanentNumber(String uid) {
    prefs.begin("rfidMap", true); int pid=-1; if(prefs.isKey(uid.c_str()))pid=prefs.getUInt(uid.c_str(),-1);
    prefs.end(); return pid;
}
