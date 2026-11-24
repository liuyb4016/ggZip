#pragma once
#include "ArchiveError.h"

namespace ggzip {

class PasswordError : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
    const char* code() const noexcept override { return "GGZIP_PASSWORD_REQUIRED"; }
};

}
