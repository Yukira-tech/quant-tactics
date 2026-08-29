/**
 * @file FileGuard.hpp
 * @brief 纯 RAII 文件句柄封装
 *
 * 层级：
 *   backend/calcFactors/fileopt/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/fileopt/FileGuard.hpp
 *
 * 模块作用：
 *   对 C 原生 FILE* 做 RAII 封装，托管文件句柄生命周期。
 *   提供打开、读写一行、定位、获取大小等基础操作。
 *
 * 使用者：
 *   DataLoader、DataWriter 等 fileopt 层模块直接使用本类。
 *
 * 项目角色：
 *   文件操作层的最底层工具，所有文件读写都建立在它之上。
 *
 * 引入说明：
 *   依赖标准库 cstdlib、string、cstring、stdexcept、cstdio。
 *   C++17 下额外依赖 filesystem、string_view、optional。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <cstdlib>
#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdio>

#if CPPSTD_HAS_CPP17
#include <filesystem>
#include <string_view>
#include <optional>
#endif

namespace fileopt {

/**
 * @brief 对 FILE* 的 RAII 封装
 *
 * 析构时自动关闭文件句柄。支持移动构造与移动赋值，禁止拷贝。
 * 同时提供 C 字符串路径与 C++17 filesystem::path 两套打开接口。
 */
class FileGuard {
public:
#if CPPSTD_HAS_CPP17
    namespace fs = std::filesystem;
#endif

    /**
     * @brief 构造空的 FileGuard
     * @note 内部句柄初始化为 nullptr，需要调用 Open 后才持有文件
     */
    explicit FileGuard() noexcept : file_(nullptr) {}

    /**
     * @brief 移动构造
     * @param rhs 源对象，转移后其句柄置空
     */
    FileGuard(FileGuard&& rhs) noexcept : file_(rhs.file_) {
        rhs.file_ = nullptr;
    }

    /**
     * @brief 移动赋值
     * @param rhs 源对象
     * @return 本对象引用
     * @note 若本对象原持有句柄，会先自动关闭，防止泄漏
     */
    FileGuard& operator=(FileGuard&& rhs) noexcept {
        if (this != &rhs) {
            SafeCloseFile(file_);
            file_ = rhs.file_;
            rhs.file_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief 析构，自动关闭文件句柄
     */
    ~FileGuard() {
        SafeCloseFile(file_);
    }

#if CPPSTD_HAS_CPP17
    /**
     * @brief 打开文件（C++17 path 版本）
     * @param filepath std::filesystem 路径对象
     * @param mode 打开模式字符串
     * @return 成功返回 FILE*；失败返回 std::nullopt
     * @note 成功后句柄由本对象接管，析构时自动关闭
     */
    std::optional<FILE*> Open(const fs::path& filepath, std::string_view mode) {
        if (!CheckMode(mode)) return std::nullopt;
        FILE* result = OpenFileImpl(PathToString(filepath), std::string(mode));
        if (!result) return std::nullopt;
        file_ = result;
        return result;
    }
#endif

    /**
     * @brief 打开文件（C 字符串路径版本）
     * @param filepath C 字符串路径
     * @param mode 打开模式字符串
     * @return 成功返回 FILE*；失败返回 nullptr
     * @note 成功后句柄由本对象接管，析构时自动关闭
     */
    FILE* Open(const char* filepath, const char* mode) {
        if (!CheckMode(mode)) return nullptr;
        FILE* result = OpenFileImpl(std::string(filepath), std::string(mode));
        if (result) file_ = result;
        return result;
    }

    /**
     * @brief 写入一行文本
     * @param text 需要写入的 C 字符串
     * @return fputs 返回值，失败返回 EOF
     * @note 不会自动追加换行符
     */
    int WriteLine(const char* text) {
        if (!file_) return EOF;
        return fputs(text, file_);
    }

    /**
     * @brief 读取一行到外部缓冲区
     * @param buf 用户提供的 char 缓冲区
     * @param buf_size 缓冲区字节大小
     * @return 成功返回 buf 指针；失败返回 nullptr
     */
    char* ReadLine(char* buf, int buf_size) {
        if (!file_) return nullptr;
        return fgets(buf, buf_size, file_);
    }

    /**
     * @brief 读取一整行，返回 std::string
     * @return 读到的字符串；句柄无效返回空字符串
     * @note 循环读取直到捕获换行符
     */
    std::string ReadLine() {
        if (!file_) return {};
#define TMP_BUF_SIZE 1024
        char buf[TMP_BUF_SIZE];
        std::string res;
        while (fgets(buf, TMP_BUF_SIZE, file_)) {
            res += buf;
            if (res.back() == '\n') break;
        }
#undef TMP_BUF_SIZE
        return res;
    }

    /**
     * @brief 文件指针跳转到文件开头
     * @param filepath 已打开的有效 FILE* 句柄
     * @return true 成功；false 失败
     */
    bool SeekBegin(FILE* filepath) { return SeekImpl(filepath, 0L, SEEK_SET); }

    /**
     * @brief 文件指针跳转到文件末尾
     * @param filepath 已打开的有效 FILE* 句柄
     * @return true 成功；false 失败
     */
    bool SeekEnd(FILE* filepath) { return SeekImpl(filepath, 0L, SEEK_END); }

    /**
     * @brief 文件指针跳转到相对文件开头的指定偏移
     * @param filepath 已打开的有效 FILE* 句柄
     * @param offset 字节偏移
     * @return true 成功；false 失败
     */
    bool SeekSet(FILE* filepath, long offset) { return SeekImpl(filepath, offset, SEEK_SET); }

    /**
     * @brief 获取打开文件的总字节数
     * @param filepath 已打开的有效 FILE* 句柄
     * @return 文件大小；失败返回 -1L
     * @note 不改变原读写指针位置
     */
    long GetFileSize(FILE* filepath) {
        long cur = ftell(filepath);
        if (!SeekEnd(filepath)) return -1L;
        long size = ftell(filepath);
        SeekSet(filepath, cur);
        return size;
    }

private:
    // 安全关闭文件句柄，关闭后置空
    void SafeCloseFile(FILE*& fp) {
        if (fp != nullptr) {
            fclose(fp);
            fp = nullptr;
        }
    }

    // fseek 底层封装
    bool SeekImpl(FILE* fp, long offset, int origin) {
        if (fp == nullptr) return false;
        return fseek(fp, offset, origin) == 0;
    }

    // 模式校验核心，白名单维护在此处
    bool CheckModeCore(const std::string& mode_str) {
        if (mode_str.empty()) return false;
        if (mode_str == "r"
            || mode_str == "w"
            || mode_str == "r+"
            || mode_str == "w+"
            || mode_str == "rb"
            || mode_str == "wb"
            || mode_str == "a"
            || mode_str == "ab") {
            return true;
        }
        return false;
    }

#if CPPSTD_HAS_CPP17
    // fs::path 转 std::string
    std::string PathToString(const fs::path& p) {
        return p.string();
    }
#endif

    // 统一 fopen 底层实现
    FILE* OpenFileImpl(const std::string& path, const std::string& mode) {
        return fopen(path.c_str(), mode.c_str());
    }

#if CPPSTD_HAS_CPP17
    // 打开模式校验（string_view 版本）
    bool CheckMode(std::string_view mode) {
        if (mode.empty()) return false;
        return CheckModeCore(std::string(mode));
    }
#endif

    // 打开模式校验（C 字符串版本）
    bool CheckMode(const char* mode) {
        if (!mode) return false;
        return CheckModeCore(std::string(mode));
    }

private:
    FILE* file_;

    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};

} // namespace fileopt
