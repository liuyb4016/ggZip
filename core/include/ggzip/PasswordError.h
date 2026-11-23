#pragma once
#include "ArchiveError.h"

namespace ggzip {

class PasswordError : public ArchiveError {
public:
    using ArchiveError::ArchiveError;
};

}
