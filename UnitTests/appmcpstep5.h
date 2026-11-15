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

#ifndef APPMCPSTEP5_H
#define APPMCPSTEP5_H

// The shared helpers of the MCP tests of step 5 of phase 2 (UnitTestsAppMcpProtect, package P4; plan
// 6.3, 8, 9 with R5-12): the encrypted documents, the calls of the actions protect.* over HTTP and
// directly, the confirmation with the passwords (found in the tab, typed into as the user does) and
// the canary search of the results.

#include "appmcpstep4.h"
#include "apptestcertificatefolders.h"

#include "pdfappdocumentpermissions.h"
#include "pdfappmcpsecretconfirmation.h"
#include "pdfappcertificatedialogs.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfcertificatemanager.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumentreader.h"
#include "pdffluentwidgets.h"

#include <QPushButton>

namespace pdftest
{

/// Documents, calls and the confirmation of the MCP actions of step 5
class AppMcpStep5
{
public:
    AppMcpStep5() = delete;

    using Tool = pdf::PDFDocumentToolEncryption;

    /// The owner password of the encrypted samples
    static QString getOwnerPassword() { return QStringLiteral("Owner-Sample-71"); }

    /// A password, which must never appear in anything an assistant or a log gets (S5.2)
    static QString getCanary() { return QStringLiteral("Canary7Qx-pdf4qt-SECRET"); }

    /// The four tools of the core are registered, when the application has not registered them yet
    /// (package P2 registers them with their GUI)
    static void registerTools()
    {
        for (Tool::Mode mode : { Tool::Mode::Password, Tool::Mode::Permissions, Tool::Mode::Certificate, Tool::Mode::Unlock })
        {
            if (!pdf::PDFDocumentToolRegistry::isImplemented(Tool::getToolId(mode)))
            {
                pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<Tool>(mode));
            }
        }
    }

    /// A document of two pages; encrypted by AES 256-bit with the owner password only (it opens
    /// without a password) and \p permissions (ISO 32000-2, Table 22), when they are not 0
    static QString writeDocument(AppMcpFixture& fixture, const QString& name, uint32_t permissions = 0)
    {
        AppMcpImageDocument document;
        document.addPage("<< >>", "0 0 1 rg 20 20 100 100 re f");
        document.addPage("<< >>", "1 0 0 rg 40 40 80 80 re f");
        if (permissions)
        {
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = getOwnerPassword();
            settings.permissions = permissions;
            document.builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }
        return document.write(fixture.path(name));
    }

    /// Every permission of Table 22 allowed (bits 3 - 6, 9 - 12 and the reserved ones)
    static uint32_t getAllPermissions() { return 0xFFFFFFFCu; }

    /// The arguments of a call of an action of protect
    static QJsonObject arguments(const QString& action, const QJsonObject& parameters, const QJsonObject& common)
    {
        QJsonObject result = common;
        result.insert(QStringLiteral("action"), action);
        if (!parameters.isEmpty())
        {
            result.insert(action, parameters);
        }
        return result;
    }

    /// The visible confirmation with the passwords, waited for at most \p timeout ms (scaled)
    static pdf::PDFAppMcpSecretConfirmation* waitDialog(int timeout = 10000)
    {
        pdf::PDFAppMcpSecretConfirmation* dialog = nullptr;
        const bool isShown = QTest::qWaitFor([&dialog]() { dialog = AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>(); return dialog != nullptr; },
                                             timeout * AppDocumentFixture::getTimeoutFactor());
        if (isShown && dialog)
        {
            // The input delay of the confirmations (1 s) is not what these tests check
            dialog->setInputDelay(0);
        }
        return dialog;
    }

    /// The card can be typed into (package P2 has implemented PDFAppToolPasswordCard)
    static bool isCardImplemented(const pdf::PDFAppToolPasswordCard* card)
    {
        return card && card->getPasswordField();
    }

    /// Types the password into the card as the user does (the field and its repetition)
    static void type(pdf::PDFAppToolPasswordCard* card, const QString& password)
    {
        QVERIFY(card);
        for (pdf::PDFLineEdit* field : { card->getPasswordField(), card->getRepeatField() })
        {
            if (field)
            {
                field->setFocus();
                QTest::keyClicks(field, password);
            }
        }
    }

    /// Clicks the verb of the confirmation (Save Copy, Allow, Save)
    static void confirm(pdf::PDFAppMcpSecretConfirmation* dialog)
    {
        QPushButton* button = dialog->getAllowButton();
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
    }

    /// The facts of the body of a save of the confirmation ("key: value")
    static QStringList facts(const pdf::PDFAppMcpSecretConfirmation* dialog)
    {
        QStringList texts;
        if (const pdf::PDFKeyValueList* list = dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("factsList")))
        {
            for (int i = 0; i < list->getRowCount(); ++i)
            {
                texts << list->getKey(i) + QStringLiteral(": ") + list->getValue(i);
            }
        }
        return texts;
    }

    /// The whole text of a result (the JSON of the response with the content and the structured content)
    static QByteArray resultText(const pdf::PDFMcpCallResult& result)
    {
        return result.result + result.getFirstText().toUtf8() + result.errorMessage.toUtf8() + result.errorData;
    }

    /// Every activity row as text (the client, the action, the document, the detail)
    static QByteArray activityText(AppMcpFixture& fixture)
    {
        QByteArray text;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            text += row.client.toUtf8() + ' ' + row.action.toUtf8() + ' ' + row.document.toUtf8() + ' ' + QJsonDocument(row.detail).toJson(QJsonDocument::Compact) + '\n';
        }
        return text;
    }

    /// The written file is encrypted and opens by \p password (the reader of the core)
    static bool opensWithPassword(const QString& filePath, const QString& password, QString* reason = nullptr)
    {
        bool isPasswordAsked = false;
        pdf::PDFDocumentReader reader(nullptr, [&](bool* ok) { isPasswordAsked = true; *ok = true; return password; }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        const bool isRead = reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK;
        if (reason)
        {
            *reason = QStringLiteral("asked %1, result %2 (%3), pages %4").arg(isPasswordAsked).arg(int(reader.getReadingResult())).arg(reader.getErrorMessage())
                          .arg(isRead ? qint64(document.getCatalog()->getPageCount()) : qint64(-1));
        }
        return isPasswordAsked && isRead && document.getCatalog()->getPageCount() == 2;
    }

    static QByteArray readFile(const QString& filePath)
    {
        return AppMcpStep4::readFile(filePath);
    }

    /// The written file opens without a password
    static bool opensWithoutPassword(const QString& filePath)
    {
        bool isPasswordAsked = false;
        pdf::PDFDocumentReader reader(nullptr, [&](bool* ok) { isPasswordAsked = true; *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return !isPasswordAsked && reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK && document.getCatalog()->getPageCount() == 2;
    }
};

}   // namespace pdftest

#endif // APPMCPSTEP5_H
