#pragma once
#include <span>
#include <string_view>

namespace ggzip {

enum class DiagnosticLanguage { SimplifiedChinese, English };

struct DiagnosticTranslation {
    std::string_view source;
    std::string_view english;
};

void setDiagnosticLanguage(DiagnosticLanguage language) noexcept;
DiagnosticLanguage diagnosticLanguage() noexcept;
const char* diagnostic(const char* source) noexcept;
std::span<const DiagnosticTranslation> diagnosticCatalog() noexcept;

}
