#pragma once

#include "common.h"
#include "datamanager.h"
#include <string>
#include <atomic>

namespace ai_chat_sdk
{
    // AuthManager 职责：用户注册、登录、登出、token 校验
    // 密码不落明文，只存 SHA256(盐 + 密码)；token 为随机串，带过期时间
    // 本身无状态、无锁，线程安全由底层 DataManager 保证
    class AuthManager
    {
    public:
        explicit AuthManager(DataManager &db);

        // 注册新用户。成功返回 true，失败返回 false
        // 注册不签发 token：用户必须再走一次登录流程，避免"注册即登录"
        // 失败原因：用户名已存在 / 用户名或密码为空 / 数据库错误
        bool registerUser(const std::string &username, const std::string &password);

        // 登录。成功返回 token，失败返回空串
        std::string loginUser(const std::string &username, const std::string &password);

        // 登出（删除 token）。token 不存在返回 false
        bool logout(const std::string &token);

        // 校验 token，返回对应用户 ID；token 无效或过期返回空串
        std::string getUserIdByToken(const std::string &token) const;

    private:
        // 生成新的 userId
        std::string generateUserId();
        // 生成新的登录 token（32 字节随机 hex）
        std::string generateToken();
        // token 有效期（秒）
        static std::time_t tokenExpireTime();

        DataManager &m_db;
        static std::atomic<int64_t> s_userCounter;
    };
}
