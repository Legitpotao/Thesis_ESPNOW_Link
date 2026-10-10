#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h> // Add this!

Preferences preferences; // Create the Preferences object

// Define states matching the flowchart
enum DeviceState {
  STATE_START,
  
  // Transmit Branch
  STATE_TX_ASK_MAC,
  STATE_TX_WAIT_MAC,
  STATE_TX_SEND_PING,
  STATE_TX_WAIT_REPLY,
  STATE_TX_WAIT_SIMULINK,
  STATE_TX_STREAMING,
  
  // Receive Branch
  STATE_RX_DISPLAY_MAC,
  STATE_RX_WAIT_PING,
  STATE_RX_STREAMING
};

DeviceState currentState = STATE_START;

// Variables for connection
uint8_t peerMAC[6];
esp_now_peer_info_t peerInfo;

// Handshake messages
const char* PING_MSG = "PING";
const char* PONG_MSG = "PONG";

// Callback when data is sent (ESP32 Core 3.x signature)
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Can be used for debugging packet loss later
}

// Callback when data is received (ESP32 Core 3.x signature)
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  // Extract the MAC address from the new info struct
  const uint8_t *mac = info->src_addr;

  String msg = "";
  for (int i = 0; i < len; i++) {
    msg += (char)incomingData[i];
  }

  // Handle Receive Mode Handshake
  if (currentState == STATE_RX_WAIT_PING && msg == PING_MSG) {
    Serial.println("Test ping received! Replying with ping...");
    
    // Save the sender's MAC globally so we can reply now and stream later
    memcpy(peerMAC, mac, 6); 
      
    // Register the sender as a peer 
    memcpy(peerInfo.peer_addr, peerMAC, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
    }
    
    // Send PONG back using the correctly saved MAC
    esp_now_send(peerMAC, (uint8_t *)PONG_MSG, strlen(PONG_MSG));
    Serial.println("Connection successful.");

    preferences.putChar("mode", 'R'); 
    preferences.putBytes("mac", peerMAC, 6);

    currentState = STATE_RX_STREAMING;
  }
  
  // Handle Transmit Mode Handshake (waiting for reply)
  else if (currentState == STATE_TX_WAIT_REPLY && msg == PONG_MSG) {

    Serial.println("Reply received! Connection successful.");
    Serial.println("Waiting for stream data from Simulink..."); 

    preferences.putChar("mode", 'T');
    preferences.putBytes("mac", peerMAC, 6);

    currentState = STATE_TX_WAIT_SIMULINK;
  }

  // Handle Streaming Data
  else if (currentState == STATE_RX_STREAMING || currentState == STATE_TX_STREAMING) {
    // Stream received data directly to MATLAB Simulink via Serial
    Serial.write(incomingData, len);
  }
}

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Open the NVS memory space named "trainer_cfg" in Read/Write mode (false)
  preferences.begin("trainer_cfg", false);
  
  Serial.println("\n--- ESP-NOW INITIALIZED ---");
  Serial.println("Type 'CLEAR' within 3 seconds to clear saved configuration...");
  
// 3-Second Wait Window
  unsigned long startTime = millis();
  String inputBuffer = "";
  bool clearMemory = false;
  
  while (millis() - startTime < 3000) {
    if (Serial.available() > 0) {
      char input = Serial.read();
      inputBuffer += input;
      
      // Look for a highly specific string instead of a single byte
      if (inputBuffer.indexOf("CLEAR") >= 0) {
        clearMemory = true;
        break;
      }
    }
  }

  // Handle Memory Clearing
  if (clearMemory) {
    preferences.clear();
    Serial.println("Memory wiped. Starting fresh.");
  }

  // Try to load saved mode ('T' or 'R')
  char savedMode = preferences.getChar("mode", '\0'); // '\0' is default if nothing is saved

  if (savedMode == 'T') {
    Serial.println("Loaded TRANSMIT mode from memory.");
    preferences.getBytes("mac", peerMAC, 6); // Load saved MAC
    
    // Re-register the peer
    memcpy(peerInfo.peer_addr, peerMAC, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    
    Serial.println("Ready for Simulink data...");
    currentState = STATE_TX_STREAMING; // Jump straight to streaming
    
  } else if (savedMode == 'R') {
    Serial.println("Loaded RECEIVE mode from memory.");
    preferences.getBytes("mac", peerMAC, 6); // Load saved MAC
    
    // Re-register the peer
    memcpy(peerInfo.peer_addr, peerMAC, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    
    Serial.println("Ready for incoming ESP-NOW data...");
    currentState = STATE_RX_STREAMING; // Jump straight to streaming
    
  } else {
    // If no memory is saved, go to the standard startup
    Serial.println("No configuration found.");
    Serial.println("Transmit or receive mode? (Enter 'T' or 'R'):");
    currentState = STATE_START;
  }
}

void loop() {
  switch (currentState) {
    
    case STATE_START:
      if (Serial.available() > 0) {
        char mode = Serial.read();
        if (mode == 'T' || mode == 't') {
          currentState = STATE_TX_ASK_MAC;
        } else if (mode == 'R' || mode == 'r') {
          currentState = STATE_RX_DISPLAY_MAC;
        }
      }
      break;

    // ---------------- TRANSMIT BRANCH ----------------
    case STATE_TX_ASK_MAC:
      Serial.println("Transmit Mode selected.");
      Serial.println("Enter RX MAC Address (Format: AA:BB:CC:DD:EE:FF) :");
      currentState = STATE_TX_WAIT_MAC;
      break;

    case STATE_TX_WAIT_MAC:
      if (Serial.available() > 0) {
        String macStr = Serial.readStringUntil('\n');
        macStr.trim();
        
        // Parse MAC address string
        if (macStr.length() == 17) {
          sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", 
                 &peerMAC[0], &peerMAC[1], &peerMAC[2], 
                 &peerMAC[3], &peerMAC[4], &peerMAC[5]);
                 
          // Register peer
          memcpy(peerInfo.peer_addr, peerMAC, 6);
          peerInfo.channel = 0;  
          peerInfo.encrypt = false;
          
          if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            currentState = STATE_TX_SEND_PING;
          } else {
            Serial.println("Failed to add peer. Try again:");
          }
        }
      }
      break;

    case STATE_TX_SEND_PING:
      Serial.println("Sending test ping...");
      esp_now_send(peerMAC, (uint8_t *)PING_MSG, strlen(PING_MSG));
      Serial.println("If none is received, keep waiting...");
      currentState = STATE_TX_WAIT_REPLY;
      break;

    case STATE_TX_WAIT_REPLY:
      // Waiting handled in OnDataRecv callback
      break;

    case STATE_TX_WAIT_SIMULINK:
      // The ESP32 will sit quietly here without flooding the Serial port
      if (Serial.available() > 0) {
        Serial.println("Data received from Simulink! Streaming started.");
        currentState = STATE_TX_STREAMING;
      }
      break;

    case STATE_TX_STREAMING:
      // Stream transmit data to MATLAB Simulink (via ESP-NOW)
      if (Serial.available() > 0) {
        size_t len = Serial.available();
        uint8_t sbuf[250]; // ESP-NOW max payload is 250 bytes
        len = Serial.readBytes(sbuf, min(len, sizeof(sbuf)));
        esp_now_send(peerMAC, sbuf, len);
      }
      break;

    // ---------------- RECEIVE BRANCH ----------------
    case STATE_RX_DISPLAY_MAC:
      Serial.println("Receive Mode selected.");
      Serial.print("Display MAC Address: ");
      Serial.println(WiFi.macAddress());
      Serial.println("Waiting for test ping...");
      currentState = STATE_RX_WAIT_PING;
      break;

    case STATE_RX_WAIT_PING:
      // Waiting handled in OnDataRecv callback
      break;

    case STATE_RX_STREAMING:
      // Data reception and pushing to Simulink is handled automatically 
      // in the OnDataRecv callback to ensure it streams in real-time.
      break;
  }
}