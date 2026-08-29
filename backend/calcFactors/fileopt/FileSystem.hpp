/**
 * @file FileSystem.hpp
 * @brief 文件系统操作工具
 *
 * 层级：
 *   backend/calcFactors/fileopt/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/fileopt/FileSystem.hpp
 *
 * 模块作用：
 *   提供文件复制、删除、修改时间查询、绝对路径打印等文件系统级操作。
 *   全部为静态方法，不持有文件句柄。
 *
 * 使用者：
 *   RuntimeFileManager 与 fileopt 层其他模块需要文件系统操作时调用本类。
 *
 * 项目角色：
 *   文件操作层的辅助工具，和 FileGuard 的句柄管理职责互补。
 *
 * 引入说明：
 *   依赖标准库 cstdio、string、ctime、iostream。
 *   C++17 下额外依赖 filesystem、chrono。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <cstdio>
#include <string>
#include <ctime>
#include <iostream>

#if CPPSTD_HAS_CPP17
#include <filesystem>
#include <chrono>
#endif

namespace fileopt {

/**
 * @brief 无状态文件系统操作工具类
 *
 * 只负责文件系统级操作，不打开、不持有文件读写句柄。
 */
class FileSystem {
public:
#if CPPSTD_HAS_CPP17
    namespace fs = std::filesystem;

    /**
     * @brief 复制文件（C++17 版本）
     * @param src 源文件路径
     * @param dst 目标文件路径
     * @param ec  错误码引用，出错信息写入 ec，不抛异常
     * @note 若目标文件已存在，会覆盖
     */
    static void CopyFile(const fs::path& src, const fs::path& dst, std::error_code& ec) {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }
#endif

    /**
     * @brief 复制文件（C 字符串版本）
     * @param src 源文件路径
     * @param dst 目标文件路径
     * @return true 复制成功；false 打开文件失败
     * @note 二进制模式拷贝，缓冲区 4096 字节；读写过程错误不检测
     */
    static bool CopyFile(const char* src, const char* dst) {
        FILE* in = fopen(src, "rb");
        if (!in) return false;
        FILE* out = fopen(dst, "wb");
        if (!out) {
            fclose(in);
            return false;
        }
        bool ok = BinaryCopy(in, out);
        fclose(in);
        fclose(out);
        return ok;
    }

#if CPPSTD_HAS_CPP17
    /**
     * @brief 删除文件（C++17 版本）
     * @param filepath 文件路径
     * @return true 删除成功；false 删除失败
     * @note 只能删除普通文件，不能删除目录
     */
    static bool Remove(const fs::path& filepath) {
        return fs::remove(filepath);
    }
#endif

    /**
     * @brief 删除文件（C 字符串版本）
     * @param filepath 文件路径
     * @return true 删除成功；false 删除失败
     */
    static bool Remove(const char* filepath) {
        int ret = remove(filepath);
        if (ret == 0) return true;
        return false;
    }

#if CPPSTD_HAS_CPP17
    /**
     * @brief 打印文件的最后修改时间
     * @param filepath 文件路径
     * @note 文件不存在时会抛出 filesystem 异常
     */
    static void PrintLastWritTime(const fs::path& filepath) {
        auto time = fs::last_write_time(filepath);
        std::time_t t = FileTimeToTimeT(time);
        std::cout << "Revise time: " << std::ctime(&t);
    }

    /**
     * @brief 将路径转为绝对路径并打印
     * @param filepath 可相对可绝对的路径对象
     */
    static void PrintAbsolute(const fs::path& filepath) {
        std::string path = PathToString(fs::absolute(filepath));
        std::cout << "Path: " << path << "\n";
    }
#endif

private:
    // 在两个已打开文件句柄间做二进制拷贝
    static bool BinaryCopy(FILE* in, FILE* out) {
#define TMP_BUF_SIZE 4096
        char buf[TMP_BUF_SIZE];
        size_t read_len;
        while ((read_len = fread(buf, 1, TMP_BUF_SIZE, in)) > 0) {
            fwrite(buf, 1, read_len, out);
        }
#undef TMP_BUF_SIZE
        return true;
    }

#if CPPSTD_HAS_CPP17
    // 将 fs::path 转为 std::string
    static std::string PathToString(const fs::path& p) {
        return p.string();
    }

    // 处理 MSVC/GCC 差异，将 file_time_type 转为 time_t
    static std::time_t FileTimeToTimeT(fs::file_time_type ft) {
#if defined(_MSC_VER)
        auto utc_tp = std::chrono::file_clock::to_utc(ft);
        auto sys_tp = std::chrono::utc_clock::to_sys(utc_tp);
#else
        auto sys_tp = std::chrono::file_clock::to_sys(ft);
#endif
        return std::chrono::system_clock::to_time_t(sys_tp);
    }
#endif
};

} // namespace fileopt