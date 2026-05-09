// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "launchapplication.h"

#include "pdfsettings.h"

#include <QMessageBox>
#include <QProcess>
#include <QWidget>

bool LaunchApplication::start(const QString& program, const QStringList& extraArguments, QWidget* parent)
{
    QStringList arguments;
    const QString settingsPath = pdf::PDFSettings::getSettingsPath();
    if (!settingsPath.isEmpty())
    {
        arguments << QString("--config=%1").arg(settingsPath);
    }
    arguments << extraArguments;

#ifndef Q_OS_WIN
    QString appDir = qgetenv("APPDIR");
#if defined(PDF4QT_FLATPAK_BUILD)
    QString flatpakAppDir = "/app";
#else
    QString flatpakAppDir;
#endif
    QString internalToolPath;

    if (!flatpakAppDir.isEmpty())
    {
        internalToolPath = QString("%1/bin/%2").arg(flatpakAppDir, program);
    }
    else if (!appDir.isEmpty())
    {
        internalToolPath = QString("%1/usr/bin/%2").arg(appDir, program);
    }
    else
    {
        internalToolPath = QString("./%1").arg(program);
    }

    qint64 pid = 0;
    if (!QProcess::startDetached(internalToolPath, arguments, QString(), &pid))
    {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Failed to start process '%1'").arg(internalToolPath));
        return false;
    }
#else
    if (!QProcess::startDetached(program, arguments))
    {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Failed to start process '%1'").arg(program));
        return false;
    }
#endif

    return true;
}
