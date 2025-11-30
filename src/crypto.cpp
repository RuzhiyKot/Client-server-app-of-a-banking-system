#include "crypto.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>

std::string Crypto::encrypt(const std::string& plaintext, const std::string& key) {
    std::vector<unsigned char> data(plaintext.begin(), plaintext.end());
    xorEncryptDecrypt(data, key);
    return base64Encode(std::string(data.begin(), data.end()));
}

std::string Crypto::decrypt(const std::string& ciphertext, const std::string& key) {
    std::string decoded = base64Decode(ciphertext);
    std::vector<unsigned char> data(decoded.begin(), decoded.end());
    xorEncryptDecrypt(data, key);
    return std::string(data.begin(), data.end());
}

std::string Crypto::hashPassword(const std::string& password) {
    // Простой хэш для демонстрации
    unsigned long hash = 5381;
    for (char c : password) {
        hash = ((hash << 5) + hash) + c;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

bool Crypto::verifyPassword(const std::string& password, const std::string& hash) {
    return hashPassword(password) == hash;
}

std::string Crypto::deriveKey(const std::string& password) {
    std::string key = password;
    while (key.length() < 32) {
        key += password;
    }
    return key.substr(0, 32);
}

void Crypto::xorEncryptDecrypt(std::vector<unsigned char>& data, const std::string& key) {
    std::string derivedKey = deriveKey(key);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= derivedKey[i % derivedKey.length()];
    }
}

std::string Crypto::base64Encode(const std::string& input) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string encoded;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    size_t in_len = input.size();
    const char* bytes_to_encode = input.c_str();
    
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (j = 0; j < i + 1; j++)
            encoded += base64_chars[char_array_4[j]];
        
        while(i++ < 3)
            encoded += '=';
    }
    
    return encoded;
}

std::string Crypto::base64Decode(const std::string& encoded) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    auto is_base64 = [](unsigned char c) {
        return (isalnum(c) || (c == '+') || (c == '/'));
    };
    
    size_t in_len = encoded.size();
    int i = 0, j = 0, in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string decoded;
    
    while (in_len-- && (encoded[in_] != '=') && is_base64(encoded[in_])) {
        char_array_4[i++] = encoded[in_]; in_++;
        if (i == 4) {
            for (i = 0; i <4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                decoded += char_array_3[i];
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j <4; j++)
            char_array_4[j] = 0;
        
        for (j = 0; j <4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (j = 0; j < i - 1; j++)
            decoded += char_array_3[j];
    }
    
    return decoded;
}
