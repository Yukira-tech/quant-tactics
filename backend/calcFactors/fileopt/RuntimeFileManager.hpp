/**
 * @file RuntimeFileManager.hpp
 * @brief 运行时文件管理器
 *
 * 层级：
 *   backend/calcFactors/fileopt/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/fileopt/RuntimeFileManager.hpp
 *
 * 模块作用：
 *   提供运行时文件的可靠读写能力，包括原子写、读全文件、
 *   文件存在性判断、心跳写入和过期文件清理。
 *
 * 使用者：
 *   DataWriter 通过本类的 AtomicWrite 将因子、信号、持仓等数据落盘。
 *   Go watcher 依赖心跳文件与文件修改时间感知模块存活状态。
 *
 * 项目角色：
 *   跨语言文件通信的基础设施，保证 C++/Go/Agent 之间数据交换的原子性。
 *
 * 引入说明：
 *   依赖 FileGuard 和 FileSystem。
 *   依赖标准库 string、cstdio、ctime。
 *   C++17 下额外依赖 filesystem、chrono。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "FileGuard.hpp"
#include "FileSystem.hpp"

#include <string>
#include <cstdio>
#include <ctime>

#if CPPSTD_HAS_CPP17
#include <filesystem>
#include <chrono>
#endif

namespace fileopt {

/**
 * @brief 运行时文件管理器
 *
 * 所有实时输出文件都通过原子写落地，保证读方永远看不到半截数据。
 * 同时提供心跳与清理机制，便于多模块感知彼此存活状态。
 */
class RuntimeFileManager {
public:
#if CPPSTD_HAS_CPP17
    namespace fs = std::filesystem;

    /**
     * @brief 原子写入文件（C++17 版本）
     * @param filepath 目标文件路径
     * @param content  完整文本内容
     * @return true 写入成功；false 写入失败
     * @note 先写临时文件，再 rename 覆盖正式文件
     */
    static bool AtomicWrite(const fs::path& filepath, const std::string& content) {
        fs::path tmpPath = filepath.string() + ".tmp";

        // 写临时文件
        {
            FileGuard fg;
            FILE* fp = fg.Open(tmpPath.string().c_str(), "wb");
            if (!fp) return false;
            if (content.empty()) return false;
            size_t written = fwrite(content.data(), 1, content.size(), fp);
            if (written != content.size()) {
                fg.~FileGuard();  // 显式析构，触发 fclose
                FileSystem::Remove(tmpPath);
                return false;
            }
        } // fg 析构，自动 fclose

        // 原子替换目标文件
        std::error_code ec;
        fs::rename(tmpPath, filepath, ec);
        if (ec) {
            FileSystem::Remove(tmpPath);
            return false;
        }
        return true;
    }

    /**
     * @brief 读取整个文件内容（C++17 版本）
     * @param filepath 文件路径
     * @return 文件内容；失败返回空字符串
     */
    static std::string ReadAll(const fs::path& filepath) {
        std::string content;
        FileGuard fg;
        FILE* fp = fg.Open(filepath.string().c_str(), "rb");
        if (!fp) return content;

#define TMP_BUF_SIZE 4096
        char buf[TMP_BUF_SIZE];
        size_t read_len;
        while ((read_len = fread(buf, 1, TMP_BUF_SIZE, fp)) > 0) {
            content.append(buf, read_len);
        }
#undef TMP_BUF_SIZE
        return content;
    }

    /**
     * @brief 判断文件是否存在（C++17 版本）
     * @param filepath 文件路径
     * @return true 存在；false 不存在
     */
    static bool FileExists(const fs::path& filepath) {
        std::error_code ec;
        return fs::exists(filepath, ec);
    }

    /**
     * @brief 获取文件最后修改时间（C++17 版本）
     * @param filepath 文件路径
     * @return 秒级时间戳；失败返回 0
     */
    static std::time_t LastModified(const fs::path& filepath) {
        std::error_code ec;
        auto t = fs::last_write_time(filepath, ec);
        if (ec) return 0;
#if defined(_MSC_VER)
        auto utc_tp = std::chrono::file_clock::to_utc(t);
        auto sys_tp = std::chrono::utc_clock::to_sys(utc_tp);
#else
        auto sys_tp = std::chrono::file_clock::to_sys(t);
#endif
        return std::chrono::system_clock::to_time_t(sys_tp);
    }

    /**
     * @brief 写入心跳文件（C++17 版本）
     * @param dir 运行时目录
     * @param moduleName 模块名，如 cpp_runtime / go_server / calc_agent
     * @return true 写入成功；false 写入失败
     */
    static bool Heartbeat(const fs::path& dir, const std::string& moduleName) {
        fs::path hbPath = dir / (moduleName + ".heartbeat");
        std::string content = std::to_string(std::time(nullptr));
        return AtomicWrite(hbPath, content);
    }

    /**
     * @brief 清理过期文件（C++17 版本）
     * @param dir 目标目录
     * @param maxAgeSeconds 最大存活时间（秒）
     */
    static void Cleanup(const fs::path& dir, double maxAgeSeconds) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;

        auto now = std::chrono::system_clock::now();
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) continue;
            auto t = entry.last_write_time(ec);
            if (ec) continue;
#if defined(_MSC_VER)
            auto utc_tp = std::chrono::file_clock::to_utc(t);
            auto sys_tp = std::chrono::utc_clock::to_sys(utc_tp);
#else
            auto sys_tp = std::chrono::file_clock::to_sys(t);
#endif
            auto age = std::chrono::duration<double>(now - sys_tp).count();
            if (age > maxAgeSeconds) {
                fs::remove(entry.path(), ec);
            }
        }
    }
#endif

    // C 风格兼容接口，无 C++17 时降级使用

    /**
     * @brief 原子写入文件（C 字符串版本）
     * @param filepath C 字符串路径
     * @param content 写入内容
     * @return true 写入成功；false 写入失败
     */
    static bool AtomicWrite(const char* filepath, const char* content) {
        std::string tmpPath = std::string(filepath) + ".tmp";
        {
            FILE* fp = fopen(tmpPath.c_str(), "wb");
            if (!fp) return false;
            size_t len = strlen(content);
            size_t written = fwrite(content, 1, len, fp);
            fclose(fp);
            if (written != len) {
                FileSystem::Remove(tmpPath.c_str());
                return false;
            }
        }
        if (rename(tmpPath.c_str(), filepath) != 0) {
            FileSystem::Remove(tmpPath.c_str());
            return false;
        }
        return true;
    }

    /**
     * @brief 读取整个文件内容（C 字符串版本）
     * @param filepath C 字符串路径
     * @return 文件内容
     */
    static std::string ReadAll(const char* filepath) {
        std::string content;
        FILE* fp = fopen(filepath, "rb");
        if (!fp) return content;
#define TMP_BUF_SIZE 4096
        char buf[TMP_BUF_SIZE];
        size_t read_len;
        while ((read_len = fread(buf, 1, TMP_BUF_SIZE, fp)) > 0) {
            content.append(buf, read_len);
        }
#undef TMP_BUF_SIZE
        fclose(fp);
        return content;
    }

    /**
     * @brief 判断文件是否存在（C 字符串版本）
     * @param filepath C 字符串路径
     * @return true 存在；false 不存在
     */
    static bool FileExists(const char* filepath) {
        FILE* fp = fopen(filepath, "rb");
        if (fp) {
            fclose(fp);
            return true;
        }
        return false;
    }

    /**
     * @brief 写入心跳文件（C 字符串版本）
     * @param dir 运行时目录
     * @param moduleName 模块名
     * @return true 写入成功；false 写入失败
     */
    static bool Heartbeat(const char* dir, const char* moduleName) {
        std::string hbPath = std::string(dir) + "/" + std::string(moduleName) + ".heartbeat";
        std::string content = std::to_string(std::time(nullptr));
        return AtomicWrite(hbPath.c_str(), content.c_str());
    }
};

} // namespace fileopt