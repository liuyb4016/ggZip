#pragma once
#include "ggzip/IEngine.h"
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>

class PreviewSession {
public:
    using Reader = std::function<void(const ggzip::DataFn&)>;

    explicit PreviewSession(const QString& temporaryParent = {});
    ~PreviewSession();
    PreviewSession(const PreviewSession&) = delete;
    PreviewSession& operator=(const PreviewSession&) = delete;

    QString extract(const std::wstring& member, const Reader& reader,
                    std::uint64_t maximumBytes = 200ull * 1024 * 1024);
    bool discard(const QString& filePath);
    QString rootPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
