#include "videodownloader/pecheck.h"

#include <QByteArray>
#include <QFile>
#include <QSysInfo>
#include <QtEndian>

namespace {

constexpr quint16 kMachineI386 = 0x014c;
constexpr quint16 kMachineAmd64 = 0x8664;
constexpr quint16 kMachineArm64 = 0xAA64;
constexpr quint16 kMagicPe32 = 0x010B;
constexpr quint16 kMagicPe32Plus = 0x020B;
constexpr quint16 kCharExecutable = 0x0002; // IMAGE_FILE_EXECUTABLE_IMAGE
constexpr quint16 kCharDll = 0x2000;        // IMAGE_FILE_DLL
constexpr int kSectionHeaderSize = 40;
constexpr int kMaxSections = 96; // limite del formato

PeCheck::Verdict invalid(QString *reason, const QString &text)
{
    if (reason) {
        *reason = text;
    }
    return PeCheck::Verdict::Invalid;
}

// No se pudo LEER: no se sabe si el archivo esta bien o mal. No es lo mismo que invalido.
PeCheck::Verdict unreadable(QString *reason, const QFile &file)
{
    if (reason) {
        *reason = QStringLiteral("the file cannot be read (%1)").arg(file.errorString());
    }
    return PeCheck::Verdict::Unreadable;
}

quint16 u16(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData()) + offset);
}

quint32 u32(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData()) + offset);
}

} // namespace

namespace PeCheck {

Verdict inspect(const QString &path, Kind kind, QString *reason)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return unreadable(reason, file);
    }
    const qint64 length = file.size();

    // Cabecera DOS: 64 bytes, firma "MZ" y, en 0x3C, donde empieza la cabecera PE.
    if (length < 64) {
        return invalid(reason, QStringLiteral("the file is only %1 bytes long").arg(length));
    }
    const QByteArray dos = file.read(64);
    if (dos.size() < 64) {
        return unreadable(reason, file); // el tamano alcanza: lo que fallo es la lectura
    }
    if (dos.at(0) != 'M' || dos.at(1) != 'Z') {
        return invalid(reason, QStringLiteral("it is not a Windows program (no MZ signature)"));
    }
    const qint64 peOffset = qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(dos.constData()) + 0x3C);
    // Firma (4) + cabecera COFF (20).
    if (peOffset < 64 || peOffset + 24 > length) {
        return invalid(reason, QStringLiteral("it has no PE header (DOS or 16-bit program, or a cut file)"));
    }
    if (!file.seek(peOffset)) {
        return unreadable(reason, file);
    }
    const QByteArray coff = file.read(24);
    if (coff.size() < 24) {
        return unreadable(reason, file);
    }
    if (u32(coff, 0) != 0x00004550) { // "PE\0\0"
        return invalid(reason, QStringLiteral("it has no PE signature (DOS or 16-bit program)"));
    }
    const quint16 machine = u16(coff, 4);
    const int sectionCount = u16(coff, 6);
    const int optionalSize = u16(coff, 20);
    const quint16 characteristics = u16(coff, 22);

    // Optional header: hace falta hasta SizeOfHeaders (offset 60), que esta en el mismo lugar
    // en PE32 y en PE32+.
    if (optionalSize < 64 || sectionCount > kMaxSections) {
        return invalid(reason, QStringLiteral("its PE header is not valid"));
    }
    const qint64 restSize = optionalSize + qint64(sectionCount) * kSectionHeaderSize;
    if (peOffset + 24 + restSize > length) {
        return invalid(reason, QStringLiteral("the file is cut (its headers are incomplete)"));
    }
    const QByteArray rest = file.read(restSize);
    if (rest.size() < restSize) {
        return unreadable(reason, file);
    }
    const quint16 magic = u16(rest, 0);
    const bool hostIsArm64 = QSysInfo::currentCpuArchitecture() == QLatin1String("arm64");
    const bool knownPair = (machine == kMachineAmd64 && magic == kMagicPe32Plus)
                           || (machine == kMachineI386 && magic == kMagicPe32)
                           || (hostIsArm64 && machine == kMachineArm64 && magic == kMagicPe32Plus);
    if (!knownPair) {
        return invalid(reason, QStringLiteral("it is built for another kind of computer (machine 0x%1)")
                                   .arg(machine, 4, 16, QLatin1Char('0')));
    }
    const bool isDll = (characteristics & kCharDll) != 0;
    if (!(characteristics & kCharExecutable) || (kind == Kind::Program && isDll)
        || (kind == Kind::Library && !isDll)) {
        return invalid(reason, kind == Kind::Program ? QStringLiteral("it is not a program file")
                                                     : QStringLiteral("it is not a library file"));
    }

    // Un archivo recortado no se carga: todo lo que la cabecera ubica por offset de archivo
    // tiene que estar adentro del archivo.
    const quint32 sizeOfHeaders = u32(rest, 60);
    if (sizeOfHeaders > length) {
        return invalid(reason, QStringLiteral("the file is cut (its headers are incomplete)"));
    }
    for (int i = 0; i < sectionCount; ++i) {
        const qsizetype section = optionalSize + qsizetype(i) * kSectionHeaderSize;
        const qint64 rawSize = u32(rest, section + 16);
        const qint64 rawOffset = u32(rest, section + 20);
        if (rawSize > 0 && rawOffset + rawSize > length) {
            return invalid(reason, QStringLiteral("the file is cut (%1 of %2 bytes)").arg(length).arg(rawOffset + rawSize));
        }
    }
    return Verdict::Valid;
}

} // namespace PeCheck
