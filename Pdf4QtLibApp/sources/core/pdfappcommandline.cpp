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

#include "pdfappcommandline.h"
#include "pdfappinstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace pdf
{

QString PDFAppCommandLineError::getMessage() const
{
    // The argument may be anything: it is shortened for the message
    QString shownArgument = argument;
    if (shownArgument.size() > 260)
    {
        shownArgument = shownArgument.left(257) + QStringLiteral("...");
    }

    switch (kind)
    {
        case Kind::UnknownArgument:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "Unknown argument %1.").arg(shownArgument);
        case Kind::MissingValue:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "The argument %1 needs a value.").arg(shownArgument);
        case Kind::InvalidPage:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "%1 is not a valid page number.").arg(shownArgument);
        case Kind::FileNotFound:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "The file %1 does not exist.").arg(QDir::toNativeSeparators(shownArgument));
        case Kind::NotAFile:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "%1 is not a file.").arg(QDir::toNativeSeparators(shownArgument));
        case Kind::TooManyFiles:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "Too many files. At most %1 files can be opened at once.").arg(PDFAppCommandLine::MAXIMUM_FILES);
        case Kind::InvalidArgument:
            return QCoreApplication::translate("pdf::PDFAppCommandLine", "Invalid argument %1.").arg(shownArgument);
    }

    return QString();
}

QString PDFAppCommandLine::getUsage()
{
    return QCoreApplication::translate("pdf::PDFAppCommandLine", "Usage: PDF4QT [file...] [--page <n>] [--portable] [--new-window]");
}

void PDFAppCommandLine::addError(PDFAppCommandLineError::Kind kind, const QString& argument)
{
    // Errors are limited as well (random input must not grow the list without limit)
    if (m_errors.size() >= size_t(MAXIMUM_FILES) && kind != PDFAppCommandLineError::Kind::TooManyFiles)
    {
        return;
    }

    PDFAppCommandLineError error;
    error.kind = kind;
    error.argument = argument.left(MAXIMUM_ARGUMENT_LENGTH);
    m_errors.push_back(std::move(error));
}

void PDFAppCommandLine::parsePage(const QString& value, const QString& argument)
{
    bool ok = false;
    const qlonglong page = value.trimmed().toLongLong(&ok, 10);
    if (!ok || page < 1 || page > MAXIMUM_PAGE)
    {
        addError(PDFAppCommandLineError::Kind::InvalidPage, argument);
        return;
    }
    m_page = int(page);
}

void PDFAppCommandLine::addFile(const QString& argument, const QString& workingDirectory)
{
    if (argument.isEmpty() || argument.size() > MAXIMUM_ARGUMENT_LENGTH || argument.contains(QChar(0)))
    {
        addError(PDFAppCommandLineError::Kind::InvalidArgument, argument);
        return;
    }

    QString path = argument;
    if (argument.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
    {
        // Desktop environments may pass URIs (Exec=... %U)
        const QUrl url(argument);
        if (!url.isValid() || !url.isLocalFile())
        {
            addError(PDFAppCommandLineError::Kind::InvalidArgument, argument);
            return;
        }
        path = url.toLocalFile();
    }

    if (path.isEmpty())
    {
        addError(PDFAppCommandLineError::Kind::InvalidArgument, argument);
        return;
    }

    const QDir baseDirectory(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    const QString absolutePath = QDir::cleanPath(baseDirectory.absoluteFilePath(path));
    if (PDFAppInstanceProtocol::isDevicePath(path) || !PDFAppInstanceProtocol::isAcceptedPath(absolutePath))
    {
        // Device paths of Windows (\\.\PhysicalDrive0, \\?\GLOBALROOT\...) are not documents
        addError(PDFAppCommandLineError::Kind::InvalidArgument, argument);
        return;
    }

    const QFileInfo fileInfo(absolutePath);

    if (fileInfo.exists() && fileInfo.isDir())
    {
        addError(PDFAppCommandLineError::Kind::NotAFile, absolutePath);
        return;
    }

    QString filePath = absolutePath;
    if (fileInfo.exists())
    {
        const QString canonicalPath = fileInfo.canonicalFilePath();
        if (!canonicalPath.isEmpty())
        {
            filePath = canonicalPath;
        }
    }
    else
    {
        addError(PDFAppCommandLineError::Kind::FileNotFound, absolutePath);
    }

    if (m_files.contains(filePath))
    {
        return;
    }

    if (m_files.size() >= MAXIMUM_FILES)
    {
        if (m_files.size() == MAXIMUM_FILES)
        {
            bool isReported = false;
            for (const PDFAppCommandLineError& error : m_errors)
            {
                isReported = isReported || error.kind == PDFAppCommandLineError::Kind::TooManyFiles;
            }
            if (!isReported)
            {
                addError(PDFAppCommandLineError::Kind::TooManyFiles, filePath);
            }
        }
        return;
    }

    m_files << filePath;
}

PDFAppCommandLine PDFAppCommandLine::parse(const QStringList& arguments, const QString& workingDirectory)
{
    PDFAppCommandLine commandLine;
    bool isOptionsEnd = false;

    // The first argument is the program
    for (qsizetype i = 1; i < arguments.size(); ++i)
    {
        const QString& argument = arguments[i];

        if (isOptionsEnd || !argument.startsWith(QLatin1Char('-')) || argument == QLatin1String("-"))
        {
            commandLine.addFile(argument, workingDirectory);
            continue;
        }

        if (argument == QLatin1String("--"))
        {
            isOptionsEnd = true;
        }
        else if (argument == QLatin1String("--portable"))
        {
            commandLine.m_portable = true;
        }
        else if (argument == QLatin1String("--new-window"))
        {
            commandLine.m_newWindow = true;
        }
        else if (argument == QLatin1String("--page"))
        {
            if (i + 1 >= arguments.size())
            {
                commandLine.addError(PDFAppCommandLineError::Kind::MissingValue, argument);
            }
            else
            {
                ++i;
                commandLine.parsePage(arguments[i], arguments[i]);
            }
        }
        else if (argument.startsWith(QLatin1String("--page=")))
        {
            commandLine.parsePage(argument.mid(7), argument);
        }
        else
        {
            commandLine.addError(PDFAppCommandLineError::Kind::UnknownArgument, argument);
        }
    }

    return commandLine;
}

QStringList PDFAppCommandLine::getErrorMessages() const
{
    QStringList messages;
    for (const PDFAppCommandLineError& error : m_errors)
    {
        messages << error.getMessage();
    }
    return messages;
}

}   // namespace pdf
