#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include <string>
#include <vector>

class Crypto {
public:
    static std::string encrypt(const std::string& plaintext, const std::string& key);
    static std::string decrypt(const std::string& ciphertext, const std::string& key);
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);
    
private:
    static std::string deriveKey(const std::string& password);
    static void xorEncryptDecrypt(std::vector<unsigned char>& data, const std::string& key);
    static std::string base64Encode(const std::string& input);
    static std::string base64Decode(const std::string& encoded);
};

#endif
