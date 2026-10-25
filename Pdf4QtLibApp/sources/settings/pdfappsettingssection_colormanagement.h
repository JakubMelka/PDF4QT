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

#ifndef PDFAPPSETTINGSSECTION_COLORMANAGEMENT_H
#define PDFAPPSETTINGSSECTION_COLORMANAGEMENT_H

#include "pdfappglobal.h"
#include "pdfappcolorprofiles.h"
#include "pdfappsettingssection.h"

#include <QFutureWatcher>
#include <QPointer>

#include <memory>

namespace pdf
{

/// Section Color management (146:3260, package B of step 5): the generic rows of
/// the color management, the combo box of the system with the version of Little
/// CMS (944:9100) and the combo boxes of the four profiles (output, device gray,
/// RGB, CMYK) with the lists of PDFCMSManager - the first profile of a list is the
/// built-in default (stored as ""), a stored profile, which this computer does not
/// have, is "(not available)". The lists are read in a worker thread
/// (PDFAppColorProfiles::scan) at the opening and after each change of the folder
/// of the profiles; a folder, which is not read (a network folder, too many or too
/// large files), is told in the description of its row. Generic disables the
/// other rows (the dependency of the definitions).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorManagementSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    explicit PDFAppColorManagementSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppColorManagementSettingsSection() override;

    /// Keys of the four profiles
    static QStringList getProfileKeys();

    /// The lists of the profiles are being read
    bool isScanning() const;

    /// Number of the finished readings of the lists (tests)
    int getScanCount() const { return m_scanCount; }

    /// Lists of the last finished reading (nullptr before the first one)
    std::shared_ptr<const PDFAppColorProfileList> getProfileList() const;

    virtual void refresh(const QString& topic, const QString& key) override;

    /// The profiles are named as their combo boxes name them (Restore defaults): a
    /// stored id - a path or a text of another version - is never shown raw
    virtual std::vector<PDFAppSettingChange> getChangedValues() const override;

protected:
    virtual PDFAppSettingRow* createRow(const PDFSettingDefinition& definition) override;
    virtual void finishContent() override;

private:
    /// The definition shown by a combo box of the provider (the texts and the
    /// dependency of the definition of the registry)
    static PDFSettingDefinition createOptionDefinition(const PDFSettingDefinition& definition, std::shared_ptr<const PDFSettingOptionsProvider> provider);

    /// Starts the reading of the lists (a reading in progress is repeated after it ends)
    void startScan();
    void onScanFinished();
    void updateProfileRows();
    void updateDirectoryDescription();

    std::shared_ptr<PDFAppColorProfileOptionsProvider> m_profileProvider;
    std::shared_ptr<PDFAppColorSystemOptionsProvider> m_systemProvider;
    QPointer<QFutureWatcher<PDFAppColorProfileList>> m_watcher;
    QString m_scannedDirectory;
    bool m_isRescanRequested = false;
    int m_scanCount = 0;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorManagementSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_COLORMANAGEMENT_H
