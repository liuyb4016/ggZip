#pragma once
// 密码管理器：DPAPI（CryptProtectData，当前用户作用域）加密存储。
// 默认位置 %APPDATA%\ggZip\passwords.dat；测试可注入自定义目录。
#include <optional>
#include <string>
#include <vector>

namespace ggzip {

struct PasswordEntry {
    std::wstring label;      // 用户可读标识（如压缩包文件名或备注）
    std::wstring password;
};

class PasswordStore {
public:
    // baseDir 为空时使用 %APPDATA%\ggZip；传入目录用于测试隔离
    explicit PasswordStore(std::wstring baseDir = std::wstring());

    // 文件不存在返回空库；解密失败抛 ArchiveError（可能由其他用户账户创建）
    std::vector<PasswordEntry> load() const;
    void save(const std::vector<PasswordEntry>& entries) const;

    // 覆盖同 label 旧条目
    void add(const std::wstring& label, const std::wstring& password) const;
    bool remove(const std::wstring& label) const;  // 返回是否删除了条目
    std::optional<std::wstring> find(const std::wstring& label) const;

    std::wstring storeFilePath() const;

private:
    std::wstring dir_;  // 存储目录（含 ggZip 目录名）
};

} // namespace ggzip
