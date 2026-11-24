#pragma once
#include "ArchiveError.h"
#include "IEngine.h"

namespace ggzip {

class ConflictError : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
    const char* code() const noexcept override { return "GGZIP_CONFLICT"; }
};

class OperationError : public ArchiveError {
public:
    OperationSummary summary;
    explicit OperationError(OperationSummary result)
        : ArchiveError(result.diagnostic), summary(std::move(result)) {}
};

}
