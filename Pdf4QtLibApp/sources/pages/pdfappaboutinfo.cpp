// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfappaboutinfo.h"
#include "pdfappcontext.h"
#include "pdfappworkspace.h"
#include "pdfupdatechecker.h"
#include "pdfwhatsnew.h"

#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QtEndian>

namespace pdf
{

/// Helpers of the information of About
class PDFAppAboutInfoHelper
{
public:
    PDFAppAboutInfoHelper() = delete;

    /// Resource of the bundled font Inter (Pdf4QtLibFluent/resources/fluent.qrc)
    static constexpr const char* INTER_RESOURCE = ":/pdf4qtfluent/fonts/Inter-Regular.ttf";

    /// Longest version text taken from a font
    static constexpr int MAXIMUM_FONT_VERSION_LENGTH = 32;

    static quint16 readUInt16(const QByteArray& data, qsizetype offset, bool* ok)
    {
        if (offset < 0 || offset + 2 > data.size())
        {
            *ok = false;
            return 0;
        }
        return qFromBigEndian<quint16>(data.constData() + offset);
    }

    static quint32 readUInt32(const QByteArray& data, qsizetype offset, bool* ok)
    {
        if (offset < 0 || offset + 4 > data.size())
        {
            *ok = false;
            return 0;
        }
        return qFromBigEndian<quint32>(data.constData() + offset);
    }

    /// Text of a record of the name table: platforms 0 and 3 are UTF-16BE, platform 1 is Latin-1
    static QString decodeName(const QByteArray& data, qsizetype offset, qsizetype length, quint16 platform)
    {
        if (offset < 0 || length <= 0 || offset + length > data.size())
        {
            return QString();
        }

        if (platform == 1)
        {
            return QString::fromLatin1(data.constData() + offset, length);
        }

        QString text;
        for (qsizetype i = 0; i + 1 < length; i += 2)
        {
            const char16_t code = char16_t((quint8(data[offset + i]) << 8) | quint8(data[offset + i + 1]));
            text += QChar(code);
        }
        return text;
    }

    /// "Version 4.001;git-..." -> "4.001" (digits and dots only)
    static QString extractVersion(const QString& text)
    {
        QString value = text.trimmed();
        if (value.startsWith(QLatin1String("Version"), Qt::CaseInsensitive))
        {
            value = value.mid(7).trimmed();
        }

        QString version;
        for (const QChar character : value)
        {
            if (!(character.isDigit() || character == QLatin1Char('.')) || version.size() >= MAXIMUM_FONT_VERSION_LENGTH)
            {
                break;
            }
            version += character;
        }
        return version;
    }
};

QString PDFAppAboutInfo::getVersion()
{
    return PDFAppFooter::getDisplayVersion();
}

QString PDFAppAboutInfo::getReleaseText(const PDFWhatsNewCatalog& catalog, const QVersionNumber& version)
{
    const PDFWhatsNewRelease* release = version.isNull() ? nullptr : catalog.findRelease(version);
    if (!release)
    {
        return QString();
    }
    if (release->isUnreleased || !release->date.isValid())
    {
        return tr("Not released yet");
    }
    return tr("Released %1").arg(PDFUpdateStatus::formatDate(release->date));
}

QString PDFAppAboutInfo::getHeroVersionText(const PDFWhatsNewCatalog& catalog, const QVersionNumber& version)
{
    const QString versionText = tr("Version %1").arg(version.isNull() ? getVersion() : PDFWhatsNewRelease::getDisplayVersion(version));
    const QString releaseText = getReleaseText(catalog, version);
    return releaseText.isEmpty() ? versionText : tr("%1 · %2").arg(versionText, releaseText);
}

QString PDFAppAboutInfo::getHeroVersionText()
{
    return getHeroVersionText(*PDFWhatsNewCatalog::getBuiltin(), PDFWhatsNewCatalog::getRunningVersion());
}

QString PDFAppAboutInfo::getBuildType()
{
#if defined(NDEBUG)
    return tr("Release");
#else
    return tr("Debug");
#endif
}

QString PDFAppAboutInfo::getVersionText()
{
    return tr("%1 (%2)").arg(getVersion(), getBuildType());
}

QString PDFAppAboutInfo::getBuildDate()
{
#if defined(PDF4QT_BUILD_DATE)
    return QString::fromUtf8(PDF4QT_BUILD_DATE).left(64);
#else
    return QString();
#endif
}

QString PDFAppAboutInfo::getCommit()
{
#if defined(PDF4QT_BUILD_COMMIT)
    return QString::fromUtf8(PDF4QT_BUILD_COMMIT).left(64);
#else
    return QString();
#endif
}

QString PDFAppAboutInfo::getCompiler()
{
#if defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1.%2").arg(_MSC_VER / 100).arg(_MSC_VER % 100, 2, 10, QLatin1Char('0'));
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return tr("Unknown compiler");
#endif
}

QString PDFAppAboutInfo::getCompilerText()
{
    return tr("%1, %2").arg(getCompiler(), QSysInfo::buildCpuArchitecture());
}

QString PDFAppAboutInfo::getQtVersionText(const QString& runTimeVersion, const QString& buildTimeVersion)
{
    if (runTimeVersion == buildTimeVersion || buildTimeVersion.isEmpty())
    {
        return runTimeVersion;
    }
    return tr("%1 (built with %2)").arg(runTimeVersion, buildTimeVersion);
}

QString PDFAppAboutInfo::getQtVersionText()
{
    return getQtVersionText(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR));
}

QString PDFAppAboutInfo::getPlatform()
{
    return QSysInfo::prettyProductName();
}

QString PDFAppAboutInfo::getSettingsText(const PDFDatabaseState& state)
{
    switch (state.mode)
    {
        case PDFDatabaseStorageMode::Disk:
            return QDir::toNativeSeparators(state.filePath);

        case PDFDatabaseStorageMode::Portable:
            return tr("Not saved (portable mode)");

        case PDFDatabaseStorageMode::ReadOnlyNewerSchema:
            return tr("%1 (read only)").arg(QDir::toNativeSeparators(state.filePath));

        case PDFDatabaseStorageMode::MemoryFallback:
            return tr("Not saved (%1 cannot be opened)").arg(QDir::toNativeSeparators(state.filePath));
    }

    return QString();
}

QString PDFAppAboutInfo::getLogText(const QString& logDirectory, bool isPortable)
{
    if (isPortable)
    {
        return tr("None (portable mode)");
    }
    if (logDirectory.isEmpty())
    {
        return tr("None");
    }
    return QDir::toNativeSeparators(logDirectory);
}

QList<PDFAppBuildInfoRow> PDFAppAboutInfo::getBuildRows(const PDFAppContext* context)
{
    QList<PDFAppBuildInfoRow> rows;
    rows.append({ tr("Version"), getVersionText() });

    const QString buildDate = getBuildDate();
    if (!buildDate.isEmpty())
    {
        rows.append({ tr("Build date"), buildDate });
    }

    const QString commit = getCommit();
    if (!commit.isEmpty())
    {
        rows.append({ tr("Commit"), commit });
    }

    rows.append({ tr("Compiler"), getCompilerText() });
    rows.append({ tr("Qt"), getQtVersionText() });
    rows.append({ tr("Platform"), getPlatform() });

    PDFDatabaseState state;
    bool isPortable = true;
    QString logDirectory;
    if (context)
    {
        state = context->getStorageState();
        isPortable = context->isPortable();
        // Only the folder of an installed log (no folder, when no log is written)
        logDirectory = context->getLogDirectory();
    }
    else
    {
        state.mode = PDFDatabaseStorageMode::Portable;
    }

    rows.append({ tr("Settings"), getSettingsText(state) });
    rows.append({ tr("Log folder"), getLogText(logDirectory, isPortable) });
    return rows;
}

QString PDFAppAboutInfo::toText(const QList<PDFAppBuildInfoRow>& rows)
{
    QStringList lines;
    lines << QStringLiteral("PDF4QT");
    for (const PDFAppBuildInfoRow& row : rows)
    {
        lines << QStringLiteral("%1: %2").arg(row.key, row.value);
    }
    return lines.join(QLatin1Char('\n'));
}

std::vector<PDFAppThirdPartyLibrary> PDFAppAboutInfo::getLibraries()
{
    std::vector<PDFAppThirdPartyLibrary> libraries = getLinkedLibraries();

    // The font of the user interface is a part of Pdf4QtLibFluent
    QFile fontFile(QString::fromLatin1(PDFAppAboutInfoHelper::INTER_RESOURCE));
    QByteArray fontData;
    if (fontFile.open(QFile::ReadOnly))
    {
        fontData = fontFile.readAll();
    }

    PDFAppThirdPartyLibrary inter;
    inter.name = QStringLiteral("Inter");
    inter.version = getFontVersion(fontData);
    inter.purpose = tr("Font of the user interface");
    inter.license = QStringLiteral("OFL-1.1");
    inter.homepage = QUrl(QStringLiteral("https://rsms.me/inter/"));
    libraries.push_back(inter);
    return libraries;
}

QString PDFAppAboutInfo::getFontVersion(const QByteArray& fontData)
{
    using Helper = PDFAppAboutInfoHelper;

    // Offset table: version, numTables, ... and the records of the tables (16 bytes)
    bool ok = true;
    const quint16 tableCount = Helper::readUInt16(fontData, 4, &ok);
    if (!ok)
    {
        return QString();
    }

    for (quint16 i = 0; i < tableCount; ++i)
    {
        const qsizetype recordOffset = 12 + qsizetype(i) * 16;
        const quint32 tag = Helper::readUInt32(fontData, recordOffset, &ok);
        const quint32 tableOffset = Helper::readUInt32(fontData, recordOffset + 8, &ok);
        if (!ok)
        {
            return QString();
        }

        if (tag != 0x6E616D65) // 'name'
        {
            continue;
        }

        const quint16 nameCount = Helper::readUInt16(fontData, qsizetype(tableOffset) + 2, &ok);
        const quint16 stringOffset = Helper::readUInt16(fontData, qsizetype(tableOffset) + 4, &ok);
        if (!ok)
        {
            return QString();
        }

        for (quint16 j = 0; j < nameCount; ++j)
        {
            const qsizetype nameOffset = qsizetype(tableOffset) + 6 + qsizetype(j) * 12;
            const quint16 platform = Helper::readUInt16(fontData, nameOffset, &ok);
            const quint16 nameId = Helper::readUInt16(fontData, nameOffset + 6, &ok);
            const quint16 length = Helper::readUInt16(fontData, nameOffset + 8, &ok);
            const quint16 offset = Helper::readUInt16(fontData, nameOffset + 10, &ok);
            if (!ok)
            {
                return QString();
            }

            if (nameId == 5 && (platform == 0 || platform == 1 || platform == 3))
            {
                const QString text = Helper::decodeName(fontData, qsizetype(tableOffset) + stringOffset + offset, length, platform);
                const QString version = Helper::extractVersion(text);
                if (!version.isEmpty())
                {
                    return version;
                }
            }
        }
        return QString();
    }

    return QString();
}

}   // namespace pdf
