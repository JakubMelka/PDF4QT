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

#include "pdfapptoolpathrules.h"
#include "pdfappsysteminfo.h"
#include "pdfrepositoryhelper.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace pdf
{

/// Helpers of the rules of the paths
class PDFAppToolPathRulesHelper
{
public:
    PDFAppToolPathRulesHelper() = delete;

    /// A character, which is never a part of a name of a file written by a tool:
    /// the control characters, the bidirectional controls (they hide the extension
    /// of a name) and, for a name usable on Windows, its reserved characters
    static bool isInvalidCharacter(char32_t character)
    {
        if (character < 0x20 || (character >= 0x7F && character <= 0x9F))
        {
            return true;
        }

        switch (character)
        {
            case 0x061C: // Arabic letter mark
            case 0x200E: // Left-to-right mark
            case 0x200F: // Right-to-left mark
                return true;

            default:
                break;
        }

        if ((character >= 0x202A && character <= 0x202E) || (character >= 0x2066 && character <= 0x2069))
        {
            return true;
        }

#ifdef Q_OS_WIN
        switch (character)
        {
            case U'<':
            case U'>':
            case U'"':
            case U'|':
            case U'?':
            case U'*':
                return true;

            default:
                break;
        }
#endif
        return false;
    }
};

PDFAppToolPathRules::NameProblem PDFAppToolPathRules::checkFileName(const QString& fileName)
{
    if (fileName.isEmpty() || fileName == QLatin1String(".") || fileName == QLatin1String(".."))
    {
        return NameProblem::Empty;
    }

    if (fileName.size() > MAXIMUM_FILE_NAME_LENGTH)
    {
        return NameProblem::TooLong;
    }

    if (fileName.contains(QLatin1Char('/')))
    {
        return NameProblem::Folder;
    }

#ifdef Q_OS_WIN
    // Only Windows reads these as a part of a path: on other systems they are characters
    // of a valid name ("12:30 report.pdf" on Linux)
    if (fileName.contains(QLatin1Char('\\')))
    {
        return NameProblem::Folder;
    }

    if (fileName.contains(QLatin1Char(':')))
    {
        return NameProblem::Stream;
    }
#endif

    for (const char32_t character : fileName.toUcs4())
    {
        if (PDFAppToolPathRulesHelper::isInvalidCharacter(character))
        {
            return NameProblem::InvalidCharacter;
        }
    }

#ifdef Q_OS_WIN
    if (fileName.endsWith(QLatin1Char('.')) || fileName.endsWith(QLatin1Char(' ')))
    {
        return NameProblem::TrailingDotOrSpace;
    }
#endif

    if (isReservedDeviceName(fileName))
    {
        return NameProblem::ReservedDevice;
    }

    return NameProblem::None;
}

QString PDFAppToolPathRules::getNameProblemText(NameProblem problem)
{
    switch (problem)
    {
        case NameProblem::None:
            break;
        case NameProblem::Empty:
            return tr("Enter a name of the file.");
        case NameProblem::TooLong:
            return tr("The name of the file is too long.");
        case NameProblem::Folder:
            return tr("The name of a file cannot contain a folder.");
        case NameProblem::Stream:
            return tr("The name of a file cannot contain a colon.");
        case NameProblem::ReservedDevice:
            return tr("This name is reserved by the system. Choose another name.");
        case NameProblem::TrailingDotOrSpace:
            return tr("The name of a file cannot end with a dot or a space.");
        case NameProblem::InvalidCharacter:
            return tr("The name of the file contains a character, which cannot be used.");
    }
    return QString();
}

bool PDFAppToolPathRules::isReservedDeviceName(const QString& fileName)
{
    const QString base = fileName.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    static const QStringList names = { QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
                                       QStringLiteral("CONIN$"), QStringLiteral("CONOUT$") };
    if (names.contains(base))
    {
        return true;
    }

    // COM1-9, LPT1-9 (also the superscript digits Windows accepts)
    if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))))
    {
        const QChar digit = base[3];
        return (digit >= u'1' && digit <= u'9') || digit == QChar(0x00B9) || digit == QChar(0x00B2) || digit == QChar(0x00B3);
    }
    return false;
}

bool PDFAppToolPathRules::hasStream(const QString& cleanPath)
{
    // A second ':' is an NTFS stream ("C:/a.pdf:stream")
    const qsizetype colon = cleanPath.indexOf(QLatin1Char(':'));
    return colon >= 0 && cleanPath.indexOf(QLatin1Char(':'), colon + 1) >= 0;
}

bool PDFAppToolPathRules::isNetworkPath(const QString& path)
{
    return PDFAppNetworkPaths::isNetworkPathByName(path) || PDFAppNetworkPaths().isNetworkPath(path);
}

bool PDFAppToolPathRules::isDeviceOrNetworkPath(const QString& path)
{
    if (PDFRepositoryHelper::isDevicePath(path) || isNetworkPath(path))
    {
        return true;
    }

#ifdef Q_OS_WIN
    // Qt removes the prefix \??\ of the object manager, when it opens a path: \??\UNC\host
    // is \\host, and cleaning makes /x/..//host the same - the cleaned path decides too,
    // before anything touches it (QDir::fromNativeSeparators removes the prefix itself,
    // so the raw text is compared)
    const QString slashPath = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (slashPath.startsWith(QLatin1String("/?\?/")) || cleanPath.startsWith(QLatin1String("/?\?/")) ||
        PDFRepositoryHelper::isDevicePath(cleanPath) || isNetworkPath(cleanPath))
    {
        return true;
    }
#endif

    return false;
}

QString PDFAppToolPathRules::resolveJunctions(const QString& canonicalPath)
{
#ifdef Q_OS_WIN
    QString current = canonicalPath;
    for (int depth = 0; depth < 32; ++depth)
    {
        const QStringList parts = current.split(QLatin1Char('/'));
        QString prefix = parts.value(0);
        bool isReplaced = false;
        for (qsizetype i = 1; i < parts.size() && !isReplaced; ++i)
        {
            prefix += QLatin1Char('/') + parts[i];
            const QFileInfo info(prefix);
            if (!info.isJunction())
            {
                continue;
            }

            // A mount point of a volume is a junction too: Qt gives its first
            // mount path - the drive of the volume, or the folder itself for a
            // volume without a drive letter - or "Volume{...}", which does not
            // exist, without one. The folder is the root of the volume then, not
            // a way to another place (only an administrator mounts a volume; a
            // junction to itself or to nothing reaches nothing either; review
            // RFW7-M - every file on such a volume was refused)
            const QString rawTarget = info.junctionTarget();
            const QString target = QFileInfo(rawTarget).canonicalFilePath();
            const bool isVolumeName = target.isEmpty() && QFileInfo(rawTarget).fileName().startsWith(QLatin1String("Volume{"), Qt::CaseInsensitive);
            if (isVolumeName || target.compare(prefix, Qt::CaseInsensitive) == 0)
            {
                continue;
            }
            if (target.isEmpty())
            {
                return QString();
            }
            const QStringList rest = parts.mid(i + 1);
            current = rest.isEmpty() ? target : target + QLatin1Char('/') + rest.join(QLatin1Char('/'));
            isReplaced = true;
        }
        if (!isReplaced)
        {
            return current;
        }
    }
    return QString();
#else
    return canonicalPath;
#endif
}

}   // namespace pdf
