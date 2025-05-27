#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <TM1637Display.h>

// Pins for TM1637 4-digit display (CLK and DIO) and LEDs
#define TM1637_CLK  16
#define TM1637_DIO  17
#define LED_GREEN    2
#define LED_RED      4

#define DEVICE_ID 1      // 1–3 for the three display nodes
const int NUM_DISPLAYS = 3;

Preferences prefQueue;
std::vector<uint16_t> queueVec;

// Broadcast address and message struct (same format as above)
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
typedef struct {
  uint8_t action;
  uint16_t number;
  uint8_t uid[8];
} __attribute__((packed)) ESPNOWMessage;

// TM1637 display object
TM1637Display display(TM1637_CLK, TM1637_DIO);

void setup() {
  Serial.begin(115200);

  // **ESP-NOW setup (station mode)**
  WiFi.mode(WIFI_STA);
  uint8_t baseMAC[6] = {0x24,0x6F,0x28,0xAA,0x00, uint8_t(10+DEVICE_ID)};
  esp_wifi_set_mac(WIFI_IF_STA, baseMAC);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while(true);
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0; peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // Load queue from Preferences
  prefQueue.begin("queueData", false);
  uint16_t count = prefQueue.getUShort("qlen", 0);
  if (count > 0) {
    std::vector<uint16_t> temp(count);
    prefQueue.getBytes("queue", temp.data(), count * sizeof(uint16_t));
    queueVec = temp;
    Serial.printf("Display %d loaded queue len=%u\n", DEVICE_ID, count);
  }

  // Initialize display and LEDs
  display.setBrightness(7);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  // Show the current head (if any) on startup
  updateDisplay();
}

void loop() {
  // Display nodes simply wait for ESP-NOW updates
}

// ESP-NOW receive callback
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  ESPNOWMessage msg;
  memcpy(&msg, incomingData, sizeof(msg));

  if (msg.action == 2) {
    // UID->number mapping (ignore on display)
    return;
  }
  else if (msg.action == 0) {
    // A number was added
    uint16_t num = msg.number;
    bool found = false;
    for (uint16_t q : queueVec) {
      if (q == num) { found = true; break; }
    }
    if (!found) {
      queueVec.push_back(num);
      prefQueue.putUShort("qlen", queueVec.size());
      prefQueue.putBytes("queue", queueVec.data(), queueVec.size()*sizeof(uint16_t));
      Serial.printf("Display %d: added %u\n", DEVICE_ID, num);
      updateDisplay();
    }
  }
  else if (msg.action == 1) {
    // The head number was removed
    uint16_t num = msg.number;
    if (!queueVec.empty() && queueVec.front() == num) {
      queueVec.erase(queueVec.begin());
      prefQueue.putUShort("qlen", queueVec.size());
      prefQueue.putBytes("queue", queueVec.data(), queueVec.size()*sizeof(uint16_t));
      Serial.printf("Display %d: removed %u\n", DEVICE_ID, num);
      updateDisplay();
    }
  }
}

// Update the TM1637 display and LEDs based on current queue
void updateDisplay() {
  if (queueVec.empty()) {
    // No patients: clear display, show red LED
    display.clear();
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, LOW);
    return;
  }
  uint16_t head = queueVec.front();
  // Determine which display should show this head (1-indexed)
  int showId = ((head - 1) % NUM_DISPLAYS) + 1;
  if (DEVICE_ID == showId) {
    // This display is responsible: show number
    display.showNumberDec(head, true);  // true = leading zeros off by default
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
  } else {
    // Not this display’s turn: blank
    display.clear();
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
  }
}
