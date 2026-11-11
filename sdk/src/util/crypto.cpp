#include "../include/util/crypto.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <sstream>
#include <iomanip>
#include <vector>

namespace ai_chat_sdk
{
    namespace crypto
    {
        namespace
        {
            // 把字节数组转成小写 hex 字符串
            std::string bytesToHex(const unsigned char *data, size_t len)
            {
                static const char *digits = "0123456789abcdef";
                std::string result;
                result.reserve(len * 2);
                for (size_t i = 0; i < len; ++i)
                {
                    result.push_back(digits[data[i] >> 4]);
                    result.push_back(digits[data[i] & 0x0f]);
                }
                return result;
            }
        }

        std::string randomHex(size_t count)
        {
            if (count == 0)
            {
                return std::string();
            }
            std::vector<unsigned char> buf(count);
            // RAND_bytes 失败会返回 0，此时返回空串让调用方感知到异常
            if (RAND_bytes(buf.data(), static_cast<int>(count)) != 1)
            {
                return std::string();
            }
            return bytesToHex(buf.data(), count);
        }

        std::string sha256Hex(const std::string &input)
        {
            unsigned char digest[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char *>(input.data()),
                   input.size(),
                   digest);
            return bytesToHex(digest, SHA256_DIGEST_LENGTH);
        }

        std::string hashPassword(const std::string &password, const std::string &salt)
        {
            return sha256Hex(salt + password);
        }
    }
}
