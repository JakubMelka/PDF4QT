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

#ifndef APPTESTCERTIFICATEFOLDERS_H
#define APPTESTCERTIFICATEFOLDERS_H

// The certificate folders of a test of step 5 of phase 2 (the one shared helper; integration
// P2S5INT, item 3): a test never reads nor writes the certificates of the user (the folders of
// QStandardPaths, the Windows system store) and nothing of a test goes on the system disk.

#include "pdfapptoolcertificatelist.h"
#include "pdfcertificatemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

namespace pdftest
{

/// The certificate folders of PDF4QT and of the old Editor and Viewer (Q5.5) under a new temporary
/// folder of QDir::tempPath() (the TEMP of the test runner, build/work on E:), set by
/// PDFCertificateManager::setCertificateDirectories; the Windows system store is off
/// (PDFAppToolCertificateProvider::setSystemStoreIncluded); Create Certificate… and Import… are
/// allowed in the portable fixtures (PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed).
/// The folders of an outer object are set
/// again, when an inner one goes (nesting: a fixture inside of installForProcess); the outermost one
/// gives back the default folders of the user and the system store. GUI thread only.
class AppTestCertificateFolders
{
public:
    AppTestCertificateFolders() :
        m_outer(s_current)
    {
        const QDir root(m_folders.path());
        m_applicationFolder = root.absoluteFilePath(QStringLiteral("PDF4QT/certificates"));
        m_oldFolders = QStringList({ root.absoluteFilePath(QStringLiteral("PDF4QT Editor/certificates")), root.absoluteFilePath(QStringLiteral("PDF4QT Viewer/certificates")) });
        s_current = this;
        apply();
    }

    ~AppTestCertificateFolders()
    {
        if (s_current != this)
        {
            // An outer object goes before an inner one (post routines): it only leaves the chain
            for (AppTestCertificateFolders* inner = s_current; inner; inner = inner->m_outer)
            {
                if (inner->m_outer == this)
                {
                    inner->m_outer = m_outer;
                    break;
                }
            }
            return;
        }

        s_current = m_outer;
        if (m_outer)
        {
            m_outer->apply();
        }
        else
        {
            pdf::PDFCertificateManager::setCertificateDirectories(QString(), QStringList());
            pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(true);
            pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(false);
        }
    }

    AppTestCertificateFolders(const AppTestCertificateFolders&) = delete;
    AppTestCertificateFolders& operator=(const AppTestCertificateFolders&) = delete;

    /// The temporary folder has been created (QTemporaryDir::isValid)
    bool isValid() const { return m_folders.isValid(); }

    /// The folder of the application (PDFCertificateManager::getCertificateDirectory)
    const QString& getApplicationFolder() const { return m_applicationFolder; }

    /// The folders of the old Editor and Viewer (PDFCertificateManager::getOldCertificateDirectories)
    const QStringList& getOldFolders() const { return m_oldFolders; }

    /// Folders for the whole process (initTestCase of a test, which may show the certificates in any
    /// case): they live until the application object goes (qAddPostRoutine)
    static void installForProcess()
    {
        if (!s_process)
        {
            s_process = std::make_unique<AppTestCertificateFolders>();
            qAddPostRoutine(&AppTestCertificateFolders::uninstallForProcess);
        }
    }

    /// A self-signed certificate with its private key in \p folder, created at run time (never a
    /// committed key); its path, empty on a failure
    static QString create(const QString& folder, const QString& fileName, const QString& name, const QString& password, int rsaKeyLength = 1024)
    {
        QDir().mkpath(folder);
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(folder).absoluteFilePath(fileName);
        info.privateKeyPasword = password;
        info.certCommonName = name;
        info.certOrganization = QStringLiteral("Acme Corp");
        info.rsaKeyLength = rsaKeyLength;
        QString error;
        return pdf::PDFCertificateManager().createCertificate(info, &error) ? info.fileName : QString();
    }

private:
    void apply() const
    {
        pdf::PDFCertificateManager::setCertificateDirectories(m_applicationFolder, m_oldFolders);
        pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(false);

        // The fixtures start with --portable (a database in the memory), the folders are the test's:
        // Create Certificate… and Import… work as after a normal start (the rule of the portable mode
        // is checked by UnitTestsAppToolEncryption run:certificate-portable)
        pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(true);
    }

    static void uninstallForProcess()
    {
        s_process.reset();
    }

    QTemporaryDir m_folders;
    QString m_applicationFolder;
    QStringList m_oldFolders;
    AppTestCertificateFolders* m_outer = nullptr;

    static inline AppTestCertificateFolders* s_current = nullptr;
    static inline std::unique_ptr<AppTestCertificateFolders> s_process;
};

}   // namespace pdftest

#endif // APPTESTCERTIFICATEFOLDERS_H
