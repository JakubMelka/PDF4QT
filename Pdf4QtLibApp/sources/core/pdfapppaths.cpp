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

#include "pdfapppaths.h"

#include <QDir>
#include <QStandardPaths>

namespace pdf
{

QString PDFAppPaths::getDataDirectory()
{
    const QString overriddenDirectory = qEnvironmentVariable("PDF4QT_APP_DATA_DIRECTORY");
    if (!overriddenDirectory.isEmpty())
    {
        return QDir::cleanPath(QDir(overriddenDirectory).absolutePath());
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString PDFAppPaths::getDatabaseFilePath()
{
    const QString directory = getDataDirectory();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("pdf4qt.db"));
}

QString PDFAppPaths::getLogDirectory()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // Logs are state data of XDG (owner decision of 19. 10. 2026)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_DATA_DIRECTORY"))
    {
        const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
        return stateDirectory.isEmpty() ? QString() : QDir(stateDirectory).filePath(QStringLiteral("logs"));
    }
#endif

    const QString directory = getDataDirectory();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("logs"));
}

}   // namespace pdf
