#pragma once
// ggZip 统一错误类型。消息为 UTF-8 编码。
// 注意：不叫 Error.h/Error——MinGW/Windows 头文件里存在同名宏与符号，容易冲突。
#include <stdexcept>
#include <utility>

namespace ggzip {

class ArchiveError : public std::runtime_error {
public:
    explicit ArchiveError(std::string utf8Message)
        : std::runtime_error(std::move(utf8Message)) {}
    virtual const char* code() const noexcept { return "GGZIP_OPERATION_FAILED"; }
};

} // namespace ggzip
