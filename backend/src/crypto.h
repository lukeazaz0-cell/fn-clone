// Minimal crypto helpers: SHA-256, HMAC, PBKDF2 and secure random tokens.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace backend {

std::vector<uint8_t> sha256(const uint8_t* data, size_t len);
std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const uint8_t* data, size_t len);
std::vector<uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<uint8_t>& salt, int iterations, size_t outLen);
std::string toHex(const std::vector<uint8_t>& v);
std::vector<uint8_t> fromHex(const std::string& s);
std::vector<uint8_t> randomBytes(size_t n);
std::string randomToken(size_t bytes = 24);
bool constantTimeEquals(const std::string& a, const std::string& b);

// "pbkdf2$<iterations>$<salt hex>$<hash hex>"
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& stored);

} // namespace backend
