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

#include "pdfrepositoryhelper.h"
#include "pdfdatabasestatement.h"

#include <QDir>
#include <QFileInfo>
#include <QVariant>

namespace pdf
{

QString PDFRepositoryHelper::normalizePath(const QString& path)
{
    // QFileInfo makes the absolute path of a Windows path without a drive ("/a") or
    // with "." / ".." by GetFullPathName, which fails for 32767 characters - Qt then
    // reads the first character of an empty string and crashes. The two characters of
    // the drive and the terminating zero are kept free.
    constexpr int maximumInputLength = MAXIMUM_PATH_LENGTH - 3;
    if (path.isEmpty() || path.size() > maximumInputLength || path.contains(QChar(0)))
    {
        return QString();
    }

    const QString fromNative = QDir::fromNativeSeparators(path);
    if (!QDir::isAbsolutePath(fromNative))
    {
        return QString();
    }

    const QFileInfo info(fromNative);
    QString result = info.canonicalFilePath();
    if (result.isEmpty())
    {
        result = QDir::cleanPath(info.absoluteFilePath());
    }

    if (!isValidStoredPath(result))
    {
        return QString();
    }
    return result;
}

bool PDFRepositoryHelper::isValidStoredPath(const QString& path)
{
    return !path.isEmpty() && path.size() <= MAXIMUM_PATH_LENGTH && !path.contains(QChar(0)) && QDir::isAbsolutePath(path) && !isDevicePath(path);
}

bool PDFRepositoryHelper::isDevicePath(const QString& path)
{
    // Device namespaces of Windows (\\.\PhysicalDrive0, \\.\pipe\..., \\?\GLOBALROOT\...),
    // also with mixed separators, which Windows accepts as well. A file opened by
    // the application never has such a path (the dialogs, the drop and the command
    // line give normal paths); a row of the database with it is not a document.
    if (path.size() < 4)
    {
        return false;
    }

    QString prefix = path.left(4);
    prefix.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return prefix == QLatin1String("//./") || prefix == QLatin1String("//?/");
}

bool PDFRepositoryHelper::isSamePath(const QString& first, const QString& second)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return QString::compare(QDir::cleanPath(QDir::fromNativeSeparators(first)), QDir::cleanPath(QDir::fromNativeSeparators(second)), sensitivity) == 0;
}

QString PDFRepositoryHelper::normalizeName(const QString& name)
{
    QString result = sanitizeStoredName(name);
    if (name.trimmed().size() > MAXIMUM_NAME_LENGTH)
    {
        return QString();
    }
    return result;
}

QString PDFRepositoryHelper::sanitizeStoredName(const QString& name)
{
    QString result = name.left(MAXIMUM_NAME_LENGTH * 4);
    for (QChar& character : result)
    {
        const QChar::Category category = character.category();
        if (category == QChar::Other_Control || category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
        {
            character = QLatin1Char(' ');
        }
    }

    result = result.trimmed();
    if (result.size() > MAXIMUM_NAME_LENGTH)
    {
        result.truncate(MAXIMUM_NAME_LENGTH);

        // Do not cut a surrogate pair into halves
        if (!result.isEmpty() && result.back().isHighSurrogate())
        {
            result.chop(1);
        }
        result = result.trimmed();
    }
    return result;
}

QString PDFRepositoryHelper::getFileName(const QString& path)
{
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString name = QFileInfo(cleanPath).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(cleanPath) : name;
}

QString PDFRepositoryHelper::getFolder(const QString& path)
{
    // By the text only: QFileInfo would add the current drive to "/a/b.pdf" on Windows
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const qsizetype index = cleanPath.lastIndexOf(QLatin1Char('/'));
    if (index < 0)
    {
        return QString();
    }

    QString folder = cleanPath.left(index);
    if (folder.isEmpty() || (folder.size() == 2 && folder.at(1) == QLatin1Char(':')))
    {
        // Root of a Unix file system or of a Windows drive
        folder += QLatin1Char('/');
    }
    return folder;
}

qint64 PDFRepositoryHelper::readInteger(const PDFDatabaseStatement& statement, int column, qint64 defaultValue, qint64 minimum, qint64 maximum)
{
    const QVariant value = statement.getVariant(column);
    if (value.typeId() != QMetaType::LongLong)
    {
        return defaultValue;
    }

    const qint64 integer = value.toLongLong();
    if (integer < minimum || integer > maximum)
    {
        return defaultValue;
    }
    return integer;
}

QString PDFRepositoryHelper::readText(const PDFDatabaseStatement& statement, int column, int maximumLength)
{
    const QVariant value = statement.getVariant(column);
    if (value.typeId() != QMetaType::QString)
    {
        return QString();
    }

    QString text = value.toString();
    if (text.size() > maximumLength)
    {
        return QString();
    }
    return text;
}

bool PDFRepositoryHelper::containsText(const QString& text, const QString& query)
{
    const QString trimmedQuery = query.trimmed();
    return trimmedQuery.isEmpty() || text.contains(trimmedQuery, Qt::CaseInsensitive);
}

}   // namespace pdf
