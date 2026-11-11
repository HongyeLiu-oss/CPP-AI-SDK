#pragma once

#include <string>

namespace ai_chat_sdk
{
    // 密码哈希与随机串工具（无状态，全部为纯函数）
    namespace crypto
    {
        // 生成 count 字节的加密级随机数，转成小写 hex 字符串（2*count 个字符）
        std::string randomHex(size_t count);

        // 计算 SHA256 并返回小写 hex 字符串（64 个字符）
        std::string sha256Hex(const std::string &input);

        // 加盐哈希：sha256Hex(salt + password)
        std::string hashPassword(const std::string &password, const std::string &salt);
    }
}
