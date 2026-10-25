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

#ifndef PDFAPPSETTINGSSECTION_SECURITY_H
#define PDFAPPSETTINGSSECTION_SECURITY_H

#include "pdfappglobal.h"
#include "pdfappsettingssection.h"
#include "pdfappcertificatetable.h"

#include <QPointer>

namespace pdf
{

/// Section Security (147:3457): the generic rows of the links and actions and of
/// the signature verification, and the group Trusted certificates with the table
/// of the certificates (PDFAppCertificateTable, owner decision Q4: the own
/// certificates can be removed, the system ones are only listed). Restore
/// defaults keeps the certificates (its text says so). The table is found by the
/// anchor "trusted-certificates" and by the filter of its texts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSecuritySettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    explicit PDFAppSecuritySettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppSecuritySettingsSection() override;

    /// Key of the group of the table and the anchor of the table
    static QString getCertificatesGroupKey() { return QStringLiteral("Trusted certificates"); }
    static QString getCertificatesAnchor() { return QStringLiteral("trusted-certificates"); }

    PDFAppCertificateTable* getCertificateTable() const { return m_certificateTable; }

    virtual QString getRestoreDefaultsText(int count) const override;
    virtual QWidget* findAnchorWidget(const QString& anchor) const override;
    virtual int applyFilter(const PDFSearchQuery& query) override;
    virtual void refresh(const QString& topic, const QString& key) override;

protected:
    virtual void finishContent() override;

private:
    QPointer<PDFAppCertificateTable> m_certificateTable;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSecuritySettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_SECURITY_H
