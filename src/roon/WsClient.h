#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace roon {

// Minimal ws:// client for Roon's API; messages assemble in PSRAM since a zone list runs to tens of KB.
class WsClient {
public:
  ~WsClient();
  bool connect(const IPAddress &ip, uint16_t port, const char *path);
  void close();
  bool connected();
  bool sendBinary(const uint8_t *data, size_t len);
  bool sendPing();
  uint32_t lastRxMs() const { return lastRxMs_; }

  // A complete message, or nullptr if none has arrived. It stays valid until the next poll().
  // Sets `failed` when the connection broke.
  const uint8_t *poll(size_t &len, bool &failed);

private:
  bool sendFrame(uint8_t opcode, const uint8_t *data, size_t len);
  bool readExact(uint8_t *dst, size_t len);
  bool reserve(size_t len);

  WiFiClient client_;
  uint8_t *buf_ = nullptr;
  size_t cap_ = 0;
  size_t len_ = 0;
  uint32_t lastRxMs_ = 0;
};

} // namespace roon
