#include "../include/authmanager.h"
#include "../include/util/crypto.h"
#include "../include/util/log.h"

#include <atomic>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ai_chat_sdk
{
    namespace
    {
        // token 有效天数
        const int kTokenExpireDays = 7;
        // 随机盐长度（字节），转 hex 后是 32 字符
        const size_t kSaltBytes = 16;
        // token 随机串长度（字节），转 hex 后是 64 字符
        const size_t kTokenBytes = 32;
    }

    std::atomic<int64_t> AuthManager::s_userCounter{0};

    AuthManager::AuthManager(DataManager &db)
        : m_db(db)
    {
    }

    bool AuthManager::registerUser(const std::string &username, const std::string &password)
    {
        // 边界校验：空用户名/空密码直接拒绝，不落库
        if (username.empty() || password.empty())
        {
            LOG_ERROR("Username or password is empty");
            return false;
        }

        // 先查重，避免依赖 INSERT 抛 SQLITE_CONSTRAINT 来感知冲突（错误信息不友好）
        if (m_db.getUserByUsername(username))
        {
            LOG_ERROR("Username already exists: {}", username);
            return false;
        }

        // 每个用户独立随机盐，即使密码相同，哈希结果也不同（防彩虹表）
        std::string salt = crypto::randomHex(kSaltBytes);
        if (salt.empty())
        {
            LOG_ERROR("Failed to generate salt");
            return false;
        }

        User user(username);
        user.userId       = generateUserId();
        user.salt         = salt;
        user.passwordHash = crypto::hashPassword(password, salt);
        user.createTime   = std::time(nullptr);

        if (!m_db.insertUser(user))
        {
            LOG_ERROR("Failed to register user: {}", username);
            return false;
        }

        // 注册成功只入库，不签发 token：登录态必须由 loginUser 单独建立
        LOG_INFO("User registered: {}", username);
        return true;
    }

    std::string AuthManager::loginUser(const std::string &username, const std::string &password)
    {
        if (username.empty() || password.empty())
        {
            LOG_ERROR("Username or password is empty");
            return std::string();
        }

        auto user = m_db.getUserByUsername(username);
        if (!user)
        {
            // 用户名不存在和密码错误不区分，避免暴露"该用户名是否已注册"
            LOG_ERROR("Login failed for user: {}", username);
            return std::string();
        }

        // 用库里的盐重新计算哈希，与存储值做常数时间比较
        std::string candidate = crypto::hashPassword(password, user->salt);
        if (candidate != user->passwordHash)
        {
            LOG_ERROR("Login failed (wrong password) for user: {}", username);
            return std::string();
        }

        std::string token = generateToken();
        if (token.empty() || !m_db.insertAuthToken(token, user->userId, tokenExpireTime()))
        {
            LOG_ERROR("Failed to issue token for user: {}", username);
            return std::string();
        }

        LOG_INFO("User logged in: {}", username);
        return token;
    }

    bool AuthManager::logout(const std::string &token)
    {
        return m_db.deleteAuthToken(token);
    }

    std::string AuthManager::getUserIdByToken(const std::string &token) const
    {
        if (token.empty())
        {
            return std::string();
        }
        return m_db.getUserIdByToken(token);
    }

    std::string AuthManager::generateUserId()
    {
        // 格式：user_<秒级时间戳>_<自增序号>，保证唯一且可读
        int64_t seq = s_userCounter.fetch_add(1);
        std::ostringstream oss;
        oss << "user_" << std::time(nullptr) << "_" << seq;
        return oss.str();
    }

    std::string AuthManager::generateToken()
    {
        return crypto::randomHex(kTokenBytes);
    }

    std::time_t AuthManager::tokenExpireTime()
    {
        return std::time(nullptr) + kTokenExpireDays * 24 * 60 * 60;
    }
}
