#pragma once
#include "ggzip/IEngine.h"
#include "ggzip/SecureDirectory.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <unordered_map>

namespace ggzip::files {

struct Identity {
    bool exists = false;
    BY_HANDLE_FILE_INFORMATION info{};
};

Identity identity(const std::filesystem::path& path);
bool same(const Identity& first, const Identity& second);
void safeParents(const std::filesystem::path& path);
void makeDirectories(const std::filesystem::path& path);
std::vector<std::filesystem::path> family(const std::filesystem::path& base);
std::filesystem::path renamed(const std::filesystem::path& path,
                             const std::function<bool(const std::filesystem::path&)>& occupied);
bool verified(const std::filesystem::path& path, const Entry& entry, bool requireCrc,
              const std::atomic_bool* cancel = nullptr, Identity* verifiedState = nullptr);

class Workspace {
public:
    enum class Purpose { PrivateStaging, Publication };
    explicit Workspace(const std::filesystem::path& parent, Purpose purpose = Purpose::PrivateStaging);
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    std::filesystem::path root;
    bool preserve = false;
    void adopt(const std::filesystem::path& path);
    void adoptOutput(const std::filesystem::path& path);
    std::filesystem::path snapshot(const std::filesystem::path& source,
                                   const std::filesystem::path& relative,
                                   const std::atomic_bool& cancel);
    std::filesystem::path write(const std::filesystem::path& relative, std::string_view bytes);
    std::shared_ptr<DirectoryLock> directoryLock(const std::filesystem::path& path, bool create = false);
private:
    struct Record { std::filesystem::path path; Identity state; };
    Identity rootState_;
    std::vector<Record> records_;
    std::unordered_map<std::filesystem::path, std::size_t> recordIndex_;
    std::unique_ptr<DirectoryLock> lock_;
    std::unordered_map<std::filesystem::path, std::shared_ptr<DirectoryLock>> directoryLocks_;
};

class Resolver {
public:
    Resolver(ConflictPolicy policy, ConflictFn callback);
    ConflictPolicy choose(const Conflict& conflict);
private:
    ConflictPolicy policy_;
    ConflictFn callback_;
    bool allFiles_ = false;
    bool allDirectories_ = false;
    ConflictPolicy files_ = ConflictPolicy::Reject;
    ConflictPolicy directories_ = ConflictPolicy::Reject;
};

void commitFamily(Workspace& workspace,
                  const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& outputs,
                  const std::vector<std::filesystem::path>& previous,
                  const std::vector<Identity>& expected,
                  const std::vector<Identity>& expectedOutputs = {},
                  const std::atomic_bool* cancel = nullptr);

}
