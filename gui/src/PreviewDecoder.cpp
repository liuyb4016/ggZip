#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <cstdint>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    const QStringList arguments = application.arguments();
    if (arguments.size() > 2) QCoreApplication::setLibraryPaths({arguments[2]});
    const std::uint64_t limit = arguments.size() > 1 ? arguments[1].toULongLong() : 64000000;
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    QImageReader reader(&input);
    QImageReader::setAllocationLimit(1024);
    reader.setDecideFormatFromContent(true);
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || static_cast<std::uint64_t>(dimensions.width()) * dimensions.height() > limit)
        return 3;
    QImage image = reader.read();
    if (image.isNull()) return 4;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    const std::uint64_t pixels = static_cast<std::uint64_t>(image.width()) * image.height();
    if (pixels > limit || image.sizeInBytes() != static_cast<qsizetype>(pixels * 4)) return 5;
    const std::uint32_t header[] = {0x475a5056, static_cast<std::uint32_t>(image.width()),
                                   static_cast<std::uint32_t>(image.height())};
    if (std::fwrite(header, 1, sizeof(header), stdout) != sizeof(header)) return 6;
    if (std::fwrite(image.constBits(), 1, static_cast<std::size_t>(image.sizeInBytes()), stdout) !=
        static_cast<std::size_t>(image.sizeInBytes())) return 6;
    return 0;
}
