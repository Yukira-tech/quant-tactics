/**
 * @file PrintLog.hpp
 * @brief 轻量级日志工具
 *
 * 层级：
 *   backend/calcFactors/utils/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/utils/PrintLog.hpp
 *
 * 模块作用：
 *   提供带时间戳的 info / warn / error 三个日志级别输出。
 *
 * 使用者：
 *   C++ 各模块通过本类打印运行日志。
 *
 * 项目角色：
 *   全局日志输出工具，不依赖第三方库。
 *
 * 引入说明：
 *   依赖标准库 cstdio、ctime、string。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <cstdio>
#include <ctime>
#include <string>

namespace utils {

/**
 * @brief 带时间戳的轻量日志工具，纯静态方法，禁止实例化
 */
class PrintLog {
public:
    PrintLog() = delete;

    /**
     * @brief 输出 info 级别日志
     * @param msg 日志内容
     */
    static void info(const std::string& msg) {
        std::printf("[INFO][%s] %s\n", timestamp().c_str(), msg.c_str());
    }

    /**
     * @brief 输出 warn 级别日志
     * @param msg 日志内容
     */
    static void warn(const std::string& msg) {
        std::printf("[WARN][%s] %s\n", timestamp().c_str(), msg.c_str());
    }

    /**
     * @brief 输出 error 级别日志
     * @param msg 日志内容
     */
    static void error(const std::string& msg) {
        std::fprintf(stderr, "[ERROR][%s] %s\n", timestamp().c_str(), msg.c_str());
    }

private:
    // 生成当前时间字符串，格式 YYYY-MM-DD HH:MM:SS
    static std::string timestamp() {
        std::time_t now = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return std::string(buf);
    }
};

} // namespace utils
