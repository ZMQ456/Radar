#include "ble_tlv_protocol.h"

namespace BleProto {//BLE协议命名空间

static uint16_t readBe16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static int16_t readBeI16(const uint8_t* p) {
    return static_cast<int16_t>(readBe16(p));
}

static uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           p[3];
}

FrameParser::FrameParser() {}

void FrameParser::reset() {
    buffer.clear();
}

bool FrameParser::input(const uint8_t* bytes, size_t len, Frame& outFrame) {
    if (bytes != nullptr && len > 0) {
        buffer.insert(buffer.end(), bytes, bytes + len);
    }
    return tryParseOne(outFrame);
}

bool FrameParser::tryParseOne(Frame& outFrame) {
    while (buffer.size() >= 2) {
        if (buffer[0] == SOF1 && buffer[1] == SOF2) {
            break;
        }
        buffer.erase(buffer.begin());
    }

    if (buffer.size() < 10) {
        return false;
    }

    uint16_t dataLen = readBe16(&buffer[6]);
    size_t fullLen = 2 + 1 + 1 + 1 + 1 + 2 + dataLen + 2;
    if (buffer.size() < fullLen) {
        return false;
    }

    uint16_t expectedCrc = readBe16(&buffer[fullLen - 2]);
    uint16_t actualCrc = crc16Ccitt(&buffer[2], fullLen - 4);
    if (expectedCrc != actualCrc) {
        buffer.erase(buffer.begin());
        return false;
    }

    outFrame.version = buffer[2];
    outFrame.cmd = buffer[3];
    outFrame.flags = buffer[4];
    outFrame.seq = buffer[5];
    outFrame.data.assign(buffer.begin() + 8, buffer.begin() + 8 + dataLen);

    buffer.erase(buffer.begin(), buffer.begin() + fullLen);
    return true;
}

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendI16(std::vector<uint8_t>& out, int16_t value) {
    appendU16(out, static_cast<uint16_t>(value));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendU64(std::vector<uint8_t>& out, uint64_t value) {
    out.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

void appendString(std::vector<uint8_t>& out, const String& s) {
    appendBytes(out, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

void appendTlvU8(std::vector<uint8_t>& out, uint8_t type, uint8_t value) {
    out.push_back(type);
    appendU16(out, 1);
    out.push_back(value);
}

void appendTlvU16(std::vector<uint8_t>& out, uint8_t type, uint16_t value) {
    out.push_back(type);
    appendU16(out, 2);
    appendU16(out, value);
}

void appendTlvI16(std::vector<uint8_t>& out, uint8_t type, int16_t value) {
    out.push_back(type);
    appendU16(out, 2);
    appendI16(out, value);
}

void appendTlvU32(std::vector<uint8_t>& out, uint8_t type, uint32_t value) {
    out.push_back(type);
    appendU16(out, 4);
    appendU32(out, value);
}

void appendTlvU64(std::vector<uint8_t>& out, uint8_t type, uint64_t value) {
    out.push_back(type);
    appendU16(out, 8);
    appendU64(out, value);
}

void appendTlvString(std::vector<uint8_t>& out, uint8_t type, const String& value) {
    out.push_back(type);
    appendU16(out, static_cast<uint16_t>(value.length()));
    appendString(out, value);
}

void appendTlvBlock(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value) {
    out.push_back(type);
    appendU16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool readTlv(const std::vector<uint8_t>& data, size_t& offset, uint8_t& type, uint16_t& len, const uint8_t*& value) {
    if (offset + 3 > data.size()) {
        return false;
    }

    type = data[offset++];
    len = readBe16(&data[offset]);
    offset += 2;

    if (offset + len > data.size()) {
        return false;
    }

    value = &data[offset];
    offset += len;
    return true;
}

std::vector<uint8_t> encodeFrame(const Frame& frame) {
    std::vector<uint8_t> out;
    out.push_back(SOF1);
    out.push_back(SOF2);
    out.push_back(frame.version);
    out.push_back(frame.cmd);
    out.push_back(frame.flags);
    out.push_back(frame.seq);
    appendU16(out, static_cast<uint16_t>(frame.data.size()));
    out.insert(out.end(), frame.data.begin(), frame.data.end());

    uint16_t crc = crc16Ccitt(&out[2], out.size() - 2);
    appendU16(out, crc);
    return out;
}

static String bytesToString(const uint8_t* data, uint16_t len) {
    String out;
    out.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        out += static_cast<char>(data[i]);
    }
    return out;
  }
}