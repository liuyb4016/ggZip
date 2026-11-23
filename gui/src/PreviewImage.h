#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <cstdint>

QImage decodePreviewImage(const QString& path, std::uint64_t pixels, std::uint64_t deadline,
                         const std::atomic_bool& cancel);
