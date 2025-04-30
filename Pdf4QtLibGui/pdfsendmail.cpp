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

#include "pdfsendmail.h"

#include <QDir>
#include <QWidget>
#include <QFileInfo>

#include "pdfdbgheap.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#include <MAPI.h>
#include <string>
#endif

namespace pdfviewer
{

bool PDFSendMail::sendMail(QWidget* parent, QString subject, QString fileName)
{
#ifdef Q_OS_WIN

#if !defined(PDF4QT_COMPILER_MINGW)
    QFileInfo fileInfo(fileName);
    std::wstring subjectString = subject.toStdWString();
    std::wstring fileNameString = fileInfo.fileName().toStdWString();
    std::wstring filePathString = QDir::toNativeSeparators(fileInfo.absoluteFilePath()).toStdWString();

    std::array<wchar_t, MAX_PATH> systemDirectoryBuffer = { };
    if (!::GetSystemDirectoryW(systemDirectoryBuffer.data(), uint(systemDirectoryBuffer.size())))
    {
        return false;
    }

    QString systemDirectory = QString::fromWCharArray(systemDirectoryBuffer.data());
    QString mapiDllPath = QString("%1\\MAPI32.dll").arg(systemDirectory);
    QFileInfo mapiDllPathInfo(mapiDllPath);
    QString mapiDllPathCorrected = mapiDllPathInfo.absoluteFilePath();

    std::vector<wchar_t> mapiDllPathWchar(mapiDllPathCorrected.size() + 1);
    qsizetype length = mapiDllPathCorrected.toWCharArray(mapiDllPathWchar.data());
    mapiDllPathWchar[length] = 0;

    HMODULE mapiLib = ::LoadLibraryW(mapiDllPathWchar.data());
    if (!mapiLib)
    {
        return false;
    }

    LPMAPISENDMAILW SendMail;
    SendMail = (LPMAPISENDMAILW)GetProcAddress(mapiLib, "MAPISendMailW");
    if (!SendMail)
    {
        ::FreeLibrary(mapiLib);
        return false;
    }

    // Jakub Melka: now, we have function for mail sending, we can use it!
    // First, we must fill the info structures.
    MapiFileDescW fileDesc;
    ::ZeroMemory(&fileDesc, sizeof(fileDesc));

    fileDesc.nPosition = ULONG(-1);
    fileDesc.lpszFileName = const_cast<PWSTR>(fileNameString.c_str());
    fileDesc.lpszPathName = const_cast<PWSTR>(filePathString.c_str());

    MapiMessageW message;
    ::ZeroMemory(&message, sizeof(message));

    message.lpszSubject = const_cast<PWSTR>(subjectString.c_str());
    message.nFileCount = 1;
    message.lpFiles = &fileDesc;

    const int returnCode = SendMail(0, parent->winId(), &message, MAPI_LOGON_UI | MAPI_DIALOG, 0);

    ::FreeLibrary(mapiLib);

    switch (returnCode)
    {
        case SUCCESS_SUCCESS:
        case MAPI_USER_ABORT:
        case MAPI_E_LOGIN_FAILURE:
            return true;

        default:
            return false;
    }
#endif

    return false;
#elif defined(Q_OS_UNIX)
    // TODO
    return false;
#else
    static_assert(false, "Implement this for another OS!");
    return false;
#endif
}

}   // namespace pdfviewer
