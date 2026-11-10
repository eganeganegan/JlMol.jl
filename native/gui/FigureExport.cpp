#include "gui/FigureExport.h"

#include "gui/MolecularViewport.h"

#include <QBuffer>
#include <QImageWriter>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QtEndian>

#include <array>
#include <cstring>

namespace jlmol {
namespace {

bool writePng(QIODevice& output, const QImage& image, QString& error) {
    QImageWriter writer(&output, "png");
    if (writer.write(image)) return true;
    error = writer.errorString();
    return false;
}

bool writeSvg(QIODevice& output, MolecularViewport& viewport, const QImage& image, bool labels, QString& error) {
    QByteArray png;
    QBuffer embedded(&png);
    if (!embedded.open(QIODevice::WriteOnly) || !writePng(embedded, image, error)) return false;
    const auto width = QString::number(image.width());
    const auto height = QString::number(image.height());
    const auto xlink = QStringLiteral("http://www.w3.org/1999/xlink");
    QXmlStreamWriter xml(&output);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeNamespace(xlink, QStringLiteral("xlink"));
    xml.writeAttribute(QStringLiteral("width"), width);
    xml.writeAttribute(QStringLiteral("height"), height);
    xml.writeAttribute(QStringLiteral("viewBox"), QStringLiteral("0 0 %1 %2").arg(width, height));
    xml.writeEmptyElement(QStringLiteral("image"));
    xml.writeAttribute(QStringLiteral("width"), width);
    xml.writeAttribute(QStringLiteral("height"), height);
    xml.writeAttribute(xlink, QStringLiteral("href"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
    if (labels) viewport.writeSvgLabels(xml, image.size());
    xml.writeEndElement();
    xml.writeEndDocument();
    return !xml.hasError();
}

template<class Integer>
void appendBigEndian(QByteArray& bytes, Integer value) {
    char encoded[sizeof(value)];
    qToBigEndian(value, encoded);
    bytes.append(encoded, sizeof(value));
}

quint32 updateCrc(quint32 crc, QByteArrayView bytes) {
    static constexpr auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 i = 0; i < values.size(); ++i) {
            auto value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
            values[i] = value;
        }
        return values;
    }();
    for (const auto byte : bytes) crc = table[(crc ^ static_cast<unsigned char>(byte)) & 0xff] ^ (crc >> 8);
    return crc;
}

bool writeChunk(QIODevice& output, QByteArrayView type, QByteArrayView payload) {
    QByteArray length, crc;
    appendBigEndian(length, static_cast<quint32>(payload.size()));
    appendBigEndian(crc, updateCrc(updateCrc(0xffffffffU, type), payload) ^ 0xffffffffU);
    return output.write(length) == length.size() && output.write(type.data(), type.size()) == type.size() &&
        (payload.empty() || output.write(payload.data(), payload.size()) == payload.size()) && output.write(crc) == crc.size();
}

QByteArray compressFrame(const QImage& image) {
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) return {};
    const auto stride = rgba.width() * 4;
    QByteArray scanlines(qsizetype(stride + 1) * rgba.height(), Qt::Uninitialized);
    for (int y = 0; y < rgba.height(); ++y) {
        const auto* source = rgba.constScanLine(y);
        auto* row = scanlines.data() + qsizetype(y) * (stride + 1);
        *row++ = 1; // PNG Sub filter, four bytes per pixel.
        std::memcpy(row, source, 4);
        for (int x = 4; x < stride; ++x) row[x] = static_cast<char>(source[x] - source[x - 4]);
    }
    // qCompress prefixes its zlib stream with the original length. PNG stores only the stream.
    auto compressed = qCompress(scanlines, 3);
    if (compressed.size() <= 4) return {};
    compressed.remove(0, 4);
    return compressed;
}

} // namespace

bool saveFigure(MolecularViewport& viewport, const QString& path, const FigureOptions& options, QString& error) {
    auto image = viewport.renderImage(options.pixels, options.transparent, options.labels && !options.svg, error);
    if (image.isNull()) return false;
    constexpr int dotsPerMeter = 11811; // 300 dots per inch.
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    const bool written = options.svg ? writeSvg(output, viewport, image, options.labels, error) : writePng(output, image, error);
    if (written && output.commit()) return true;
    if (error.isEmpty()) error = output.errorString();
    return false;
}

bool saveAnimatedPng(const QString& path, QSize pixels, int frameCount, int fps,
                     const std::function<QImage(int)>& renderFrame, QString& error) {
    error.clear();
    if (!MolecularViewport::supportsImageSize(pixels) || frameCount < 1 || frameCount > 2000 || fps < 1 || fps > 60) {
        error = QStringLiteral("Invalid animation size, frame count, or frame rate.");
        return false;
    }
    QSaveFile output(path);
    const auto failedWrite = [&] {
        error = output.errorString();
        return false;
    };
    if (!output.open(QIODevice::WriteOnly) || output.write("\x89PNG\r\n\x1a\n", 8) != 8) return failedWrite();
    QByteArray header, animation;
    appendBigEndian(header, static_cast<quint32>(pixels.width()));
    appendBigEndian(header, static_cast<quint32>(pixels.height()));
    header.append("\x08\x06\0\0\0", 5); // RGBA8, deflate, standard filter, no interlace.
    appendBigEndian(animation, static_cast<quint32>(frameCount));
    appendBigEndian(animation, quint32{0}); // Loop indefinitely.
    if (!writeChunk(output, "IHDR", header) || !writeChunk(output, "sRGB", QByteArray(1, '\0')) ||
        !writeChunk(output, "acTL", animation)) return failedWrite();

    // Each frame replaces the entire canvas. The first also serves as a static PNG preview.
    quint32 sequence = 0;
    for (int frame = 0; frame < frameCount; ++frame) {
        const auto image = renderFrame(frame);
        if (image.isNull()) return false;
        if (image.size() != pixels) {
            error = QStringLiteral("The rendered frame has the wrong dimensions.");
            return false;
        }
        auto compressed = compressFrame(image);
        if (compressed.isEmpty()) {
            error = QStringLiteral("Could not compress the animation frame.");
            return false;
        }
        QByteArray control;
        appendBigEndian(control, sequence++);
        control.append(header.first(8));
        appendBigEndian(control, quint32{0}); // X offset.
        appendBigEndian(control, quint32{0}); // Y offset.
        appendBigEndian(control, quint16{1});
        appendBigEndian(control, static_cast<quint16>(fps));
        control.append(2, '\0'); // Keep frame, replace canvas.
        if (!writeChunk(output, "fcTL", control)) return failedWrite();
        if (frame == 0) {
            if (!writeChunk(output, "IDAT", compressed)) return failedWrite();
        } else {
            QByteArray prefix;
            appendBigEndian(prefix, sequence++);
            compressed.prepend(prefix);
            if (!writeChunk(output, "fdAT", compressed)) return failedWrite();
        }
    }
    if (!writeChunk(output, "IEND", {}) || !output.commit()) return failedWrite();
    return true;
}

} // namespace jlmol
