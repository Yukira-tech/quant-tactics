/**
 * @file VersionConfig.hpp
 * @brief 项目版本与环境检查
 *
 * 层级：
 *   backend/calcFactors/config/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/config/VersionConfig.hpp
 *
 * 模块作用：
 *   提供平台与 C++ 标准检测宏，以及 DEBUG_ASSERT 调试断言。
 *
 * 使用者：
 *   其余所有头文件通过包含本文件获得 CPPSTD_HAS_* 版本宏与 DEBUG_ASSERT。
 *
 * 项目角色：
 *   全局环境检查基座，保证项目在最低平台和标准版本上安全编译。
 *
 * 引入说明：
 *   依赖标准库 cstdlib 和 cstdio。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

#pragma once

#include <cstdlib>   // abort
#include <cstdio>    // fprintf, stderr

// ---------------------------------------------------------------------------
// 1. Windows 版本检查（低于 Win7 报错）
// ---------------------------------------------------------------------------
#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #error "未定义 _WIN32_WINNT！请在项目属性中设置 Windows 目标版本！"
    #elif _WIN32_WINNT < 0x0601
        #error "Windows 版本太低！需要 Windows 7 (0x0601) 或更高！"
    #endif
#endif

// ---------------------------------------------------------------------------
// 2. Linux 版本检查（glibc 低于 2.17 报错，2.17 是 C++11 线程的基线）
// ---------------------------------------------------------------------------
#ifdef __linux__
    #include <features.h>
    #if defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 17))
        #error "Linux glibc 版本太低！需要 glibc 2.17 或更高版本！"
    #endif
#endif

// ---------------------------------------------------------------------------
// 3. macOS 版本检查（低于 10.12 报错，10.12 是完整支持 C++11 的稳健基线）
// ---------------------------------------------------------------------------
#ifdef __APPLE__
    #ifndef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
        #error "未定义 macOS 最低部署目标！请在 Xcode 项目中设置 Deployment Target！"
    #elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101200
        #error "macOS 版本太低！需要 macOS 10.12 (101200) 或更高版本！"
    #endif
#endif

// ---------------------------------------------------------------------------
// 4. 提取当前 C++ 标准版本
// ---------------------------------------------------------------------------
#if defined(_MSVC_LANG)
    #define CPPSTD_VERSION _MSVC_LANG
#elif defined(__cplusplus)
    #define CPPSTD_VERSION __cplusplus
#else
    #define CPPSTD_VERSION 0
#endif

// ---------------------------------------------------------------------------
// 5. C++ 版本检测宏（供业务代码分支使用）
// ---------------------------------------------------------------------------
#define CPPSTD_HAS_CPP11   (CPPSTD_VERSION >= 201103L)
#define CPPSTD_HAS_CPP14   (CPPSTD_VERSION >= 201402L)
#define CPPSTD_HAS_CPP17   (CPPSTD_VERSION >= 201703L)
#define CPPSTD_HAS_CPP20   (CPPSTD_VERSION >= 202002L)
#define CPPSTD_HAS_CPP23   (CPPSTD_VERSION >= 202302L)

// ---------------------------------------------------------------------------
// 6. 强制最低 C++11（低于 11 直接报错）
// ---------------------------------------------------------------------------
#if !CPPSTD_HAS_CPP11
    #error "此项目需要 C++11 或更高版本！"
#endif

// ---------------------------------------------------------------------------
// 7. 带日志的调试断言宏
//
//    宏名：DEBUG_ASSERT
//    Debug 模式：打印文件名/行号/函数/表达式，失败后 abort
//    Release 模式：空操作，无任何开销
// ---------------------------------------------------------------------------
#ifndef NDEBUG   // Debug 模式（NDEBUG 未定义）
    #define DEBUG_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                std::fprintf(stderr, \
                    "[DEBUG_ASSERT] 表达式失败: %s\n" \
                    "  文件: %s\n" \
                    "  行号: %d\n" \
                    "  函数: %s\n", \
                    #expr, __FILE__, __LINE__, __func__); \
                std::abort(); \
            } \
        } while(0)
#else            // Release 模式（NDEBUG 已定义）
    #define DEBUG_ASSERT(expr) ((void)0)
#endif