#include "WsClient.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/base64.h>

namespace roon {

namespace {
constexpr size_t kMaxMessage = 512 * 1024;
constexpr uint32_t kReadTimeoutMs = 3000;
} // namespace

WsClient::~WsClient() {
  close();
  heap_caps_free(buf_);
}

bool WsClient::connect(const IPAddress &ip, uint16_t port, const char *path) {
  close();
  client_.setTimeout(kReadTimeoutMs / 1000);
  if (!client_.connect(ip, port, 3000)) return false;
  client_.setNoDelay(true);

  uint8_t nonce[16];
  esp_fill_random(nonce, sizeof(nonce));
  unsigned char key[32];
  size_t keyLen = 0;
  mbedtls_base64_encode(key, sizeof(key), &keyLen, nonce, sizeof(nonce));
  key[keyLen] = 0;

  client_.printf("GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                 path, ip.toString().c_str(), port, key);

  String status = client_.readStringUntil('\n');
  if (status.indexOf(" 101") < 0) {
    close();
    return false;
  }
  for (;;) { // rest of the headers; the accept hash isn't worth checking on a LAN socket
    String line = client_.readStringUntil('\n');
    if (line.length() == 0 && !client_.connected()) {
      close();
      return false;
    }
    if (line == "\r" || line.length() == 0) break;
  }
  lastRxMs_ = millis();
  return true;
}

void WsClient::close() {
  if (client_.connected()) {
    static const uint8_t kClose[] = {0x88, 0x80, 0, 0, 0, 0};
    client_.write(kClose, sizeof(kClose));
  }
  client_.stop();
  len_ = 0;
}

bool WsClient::connected() { return client_.connected(); }

bool WsClient::sendBinary(const uint8_t *data, size_t len) { return sendFrame(0x2, data, len); }

bool WsClient::sendPing() { return sendFrame(0x9, nullptr, 0); }

bool WsClient::sendFrame(uint8_t opcode, const uint8_t *data, size_t len) {
  uint8_t header[14];
  size_t h = 0;
  header[h++] = 0x80 | opcode;
  if (len < 126) {
    header[h++] = 0x80 | len;
  } else if (len < 65536) {
    header[h++] = 0x80 | 126;
    header[h++] = len >> 8;
    header[h++] = len & 0xFF;
  } else {
    header[h++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) header[h++] = i < 4 ? (len >> (8 * i)) & 0xFF : 0;
  }
  uint8_t mask[4];
  esp_fill_random(mask, sizeof(mask));
  memcpy(header + h, mask, 4);
  h += 4;
  if (client_.write(header, h) != h) return false;

  uint8_t chunk[256];
  for (size_t off = 0; off < len;) {
    size_t n = min(sizeof(chunk), len - off);
    for (size_t i = 0; i < n; i++) chunk[i] = data[off + i] ^ mask[(off + i) & 3];
    if (client_.write(chunk, n) != n) return false;
    off += n;
  }
  return true;
}

bool WsClient::readExact(uint8_t *dst, size_t len) {
  uint32_t lastByteAt = millis();
  size_t got = 0;
  while (got < len) {
    int avail = client_.available();
    if (avail <= 0) {
      if (!client_.connected() || millis() - lastByteAt > kReadTimeoutMs) return false;
      delay(1);
      continue;
    }
    int n = client_.read(dst + got, min((size_t)avail, len - got));
    if (n <= 0) return false;
    got += n;
    lastByteAt = millis();
  }
  lastRxMs_ = lastByteAt;
  return true;
}

bool WsClient::reserve(size_t len) {
  if (len <= cap_) return true;
  if (len > kMaxMessage) return false;
  size_t cap = max(len, cap_ ? cap_ * 2 : (size_t)16384);
  uint8_t *grown = (uint8_t *)heap_caps_realloc(buf_, cap, MALLOC_CAP_SPIRAM);
  if (!grown) return false;
  buf_ = grown;
  cap_ = cap;
  return true;
}

const uint8_t *WsClient::poll(size_t &len, bool &failed) {
  failed = false;
  len = 0;
  // A message can span several frames, and control frames can arrive between them.
  for (;;) {
    if (len_ == 0 && client_.available() < 2) {
      failed = !client_.connected();
      return nullptr;
    }
    uint8_t hdr[2];
    if (!readExact(hdr, 2)) break;
    bool fin = hdr[0] & 0x80;
    uint8_t opcode = hdr[0] & 0x0F;
    uint64_t size = hdr[1] & 0x7F;
    if (size == 126) {
      uint8_t ext[2];
      if (!readExact(ext, 2)) break;
      size = (ext[0] << 8) | ext[1];
    } else if (size == 127) {
      uint8_t ext[8];
      if (!readExact(ext, 8)) break;
      size = 0;
      for (int i = 0; i < 8; i++) size = (size << 8) | ext[i];
    }
    uint8_t mask[4] = {};
    bool masked = hdr[1] & 0x80;
    if (masked && !readExact(mask, 4)) break;

    if (opcode >= 0x8) { // control frames are small and never fragmented
      uint8_t payload[125];
      if (size > sizeof(payload) || !readExact(payload, size)) break;
      if (masked) {
        for (size_t i = 0; i < size; i++) payload[i] ^= mask[i & 3];
      }
      if (opcode == 0x8) break;
      if (opcode == 0x9) sendFrame(0xA, payload, size);
      continue;
    }

    if (opcode != 0x0) len_ = 0; // a new message
    if (!reserve(len_ + size) || !readExact(buf_ + len_, size)) break;
    if (masked) {
      for (size_t i = 0; i < size; i++) buf_[len_ + i] ^= mask[i & 3];
    }
    len_ += size;
    if (fin) {
      len = len_;
      len_ = 0;
      return buf_;
    }
  }
  failed = true;
  close();
  return nullptr;
}

} // namespace roon
