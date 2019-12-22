//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfsendmail.h"

#include <QDir>
#include <QWidget>
#include <QFileInfo>

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
    QFileInfo fileInfo(fileName);
    std::wstring subjectString = subject.toStdWString();
    std::wstring fileNameString = fileInfo.fileName().toStdWString();
    std::wstring filePathString = QDir::toNativeSeparators(fileInfo.absoluteFilePath()).toStdWString();


    HMODULE mapiLib = ::LoadLibrary(L"MAPI32.DLL");
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

    return false;
#else
    static_assert(false, "Implement this for another OS!");
    return false;
#endif
}

}   // namespace pdfviewer
