/**
 * MULTI-SLAVE Modbus RTU to TCP Gateway
 * WITH STATIC IP CONFIGURATION
 */

#include <WiFi.h>

// ============= WiFi Configuration - STATIC IP =============
const char* ssid = "PRG Auto";
const char* password = "PRG@Auto905";

// STATIC IP CONFIGURATION - CHANGE THESE TO MATCH YOUR NETWORK!
IPAddress local_IP(192, 168, 137, 152);     // ESP32's fixed IP address
IPAddress gateway(192, 168, 137, 1);        // Your router's IP
IPAddress subnet(255, 255, 255, 0);       // Usually 255.255.255.0
IPAddress primaryDNS(8, 8, 8, 8);         // Optional: Google DNS
IPAddress secondaryDNS(8, 8, 4, 4);       // Optional: Google DNS

const int modbusTCPPort = 502;

// RS485 Pins
#define RS485_RX_PIN 17
#define RS485_TX_PIN 16
#define RTU_BAUD 9600

// Performance settings
#define RESPONSE_TIMEOUT 500
#define INTER_FRAME_DELAY 10

// ============= Global Objects =============
WiFiServer server(modbusTCPPort);
WiFiClient client;

// Buffers
uint8_t tcpBuffer[256];
uint8_t rtuFrame[256];
uint8_t response[256];

// Statistics
unsigned long packetsForwarded = 0;
unsigned long packetsFailed = 0;
unsigned long lastStatsTime = 0;

// Slave discovery tracking
bool slaveDetected[256] = {false};
int activeSlaves = 0;

// ============= CRC Calculation =============
uint16_t calculateCRC(uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc = crc >> 1;
      }
    }
  }
  return crc;
}

// ============= Setup =============
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n🌐 MULTI-SLAVE Modbus Gateway");
  Serial.println("=================================");
  Serial.println("✅ Supports ANY slave ID (1-247)");
  Serial.println("📡 STATIC IP CONFIGURATION");
  
  // Initialize RS485
  Serial2.begin(RTU_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  Serial.println("✅ RS485 initialized");
  
  // ============= CONFIGURE STATIC IP =============
  Serial.println("\n📡 Configuring STATIC IP...");
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("❌ Static IP configuration failed!");
    Serial.println("⚠️ Using DHCP instead...");
  } else {
    Serial.print("✅ Static IP configured: ");
    Serial.println(local_IP);
  }
  
  // Connect to WiFi
  Serial.print("\nConnecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.println("\n📡 NETWORK CONFIGURATION:");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("   Subnet Mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("   DNS Server: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("   MAC Address: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("\n❌ WiFi Failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
  
  // Start TCP server
  server.begin();
  server.setNoDelay(true);
  
  Serial.println("\n✅ TCP Server started on port 502");
  Serial.println("🌐 Ready for MULTIPLE Modbus slaves");
  Serial.print("🚀 Gateway ready at: ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(modbusTCPPort);
  Serial.println("\n⏳ Waiting for connections...\n");
  
  lastStatsTime = millis();
}

// ============= Main Loop =============
void loop() {
  // Handle new client connections
  if (server.hasClient()) {
    WiFiClient newClient = server.available();
    
    if (!client || !client.connected()) {
      if (client) client.stop();
      client = newClient;
      client.setTimeout(1000);
      Serial.printf("✅ Client connected from %s\n", client.remoteIP().toString().c_str());
    } else {
      newClient.stop();
    }
  }
  
  // Process client requests
  if (client && client.connected() && client.available()) {
    int len = client.read(tcpBuffer, sizeof(tcpBuffer));
    
    if (len >= 8 && tcpBuffer[2] == 0 && tcpBuffer[3] == 0) {
      uint8_t unitId = tcpBuffer[6];
      
      if (unitId >= 1 && unitId <= 247) {
        
        if (!slaveDetected[unitId]) {
          slaveDetected[unitId] = true;
          activeSlaves++;
          Serial.printf("🔍 NEW SLAVE DETECTED: ID %d (Total: %d)\n", unitId, activeSlaves);
          
          Serial.print("   Active slaves: ");
          for (int i = 1; i <= 247; i++) {
            if (slaveDetected[i]) {
              Serial.printf("%d ", i);
            }
          }
          Serial.println();
        }
        
        processModbusRequest(len, unitId);
      } 
      else if (unitId == 0) {
        Serial.println("📢 Broadcast message received");
        processModbusRequest(len, unitId);
      }
    }
  }
  
  // Clean up disconnected client
  if (client && !client.connected()) {
    Serial.printf("❌ Client disconnected\n");
    client.stop();
  }
  
  // Print stats every 5 minutes
  if (millis() - lastStatsTime > 300000) {
    lastStatsTime = millis();
    Serial.printf("\n📊 GATEWAY STATS:\n");
    Serial.printf("   Forwarded: %u, Failed: %u\n", packetsForwarded, packetsFailed);
    Serial.printf("   Active Slaves: %d\n", activeSlaves);
    Serial.printf("   Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("   IP Address: %s\n", WiFi.localIP().toString().c_str());
    
    if (activeSlaves > 0) {
      Serial.print("   Slave IDs: ");
      for (int i = 1; i <= 247; i++) {
        if (slaveDetected[i]) {
          Serial.printf("%d ", i);
        }
      }
      Serial.println();
    }
  }
  
  delay(10);
}

// ============= Process Modbus Request =============
void processModbusRequest(int tcpLen, uint8_t slaveId) {
  int pduLen = tcpLen - 6;
  uint8_t* pdu = &tcpBuffer[6];
  
  uint16_t crc = calculateCRC(pdu, pduLen);
  memcpy(rtuFrame, pdu, pduLen);
  rtuFrame[pduLen] = crc & 0xFF;
  rtuFrame[pduLen + 1] = (crc >> 8) & 0xFF;
  
  while(Serial2.available()) Serial2.read();
  delay(INTER_FRAME_DELAY);
  
  Serial2.write(rtuFrame, pduLen + 2);
  Serial2.flush();
  
  if (slaveId == 0) {
    packetsForwarded++;
    return;
  }
  
  unsigned long timeout = millis() + RESPONSE_TIMEOUT;
  int respLen = 0;
  
  while (millis() < timeout && respLen < sizeof(response)) {
    if (Serial2.available()) {
      response[respLen++] = Serial2.read();
    }
    delay(1);
  }
  
  if (respLen >= 4) {
    uint16_t recvCRC = (response[respLen-1] << 8) | response[respLen-2];
    uint16_t calcCRC = calculateCRC(response, respLen - 2);
    
    if (recvCRC == calcCRC) {
      if (response[0] == slaveId) {
        uint8_t tcpResponse[256];
        
        tcpResponse[0] = tcpBuffer[0];
        tcpResponse[1] = tcpBuffer[1];
        tcpResponse[2] = 0;
        tcpResponse[3] = 0;
        tcpResponse[4] = ((respLen - 2) >> 8) & 0xFF;
        tcpResponse[5] = (respLen - 2) & 0xFF;
        
        memcpy(&tcpResponse[6], response, respLen - 2);
        
        client.write(tcpResponse, 6 + (respLen - 2));
        packetsForwarded++;
        return;
      }
    }
  }
  
  packetsFailed++;
}

// ============= End of Code =============