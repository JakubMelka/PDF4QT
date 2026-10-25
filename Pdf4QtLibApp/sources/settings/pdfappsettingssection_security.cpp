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

#include "pdfappsettingssection_security.h"
#include "pdfappcertificatetable.h"
#include "pdfappsettingrow.h"

#include "pdfdocumenttoolsearch.h"

#include <QCoreApplication>

namespace pdf
{

PDFAppSecuritySettingsSection::PDFAppSecuritySettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppSecuritySettingsSection::~PDFAppSecuritySettingsSection() = default;

void PDFAppSecuritySettingsSection::finishContent()
{
    // group-Trusted certificates 147:3781: the header with its hint and the table
    // in its own card (outside of the card of the rows of the group)
    PDFAppSettingGroup* group = addGroup(getCertificatesGroupKey(), tr("Trusted certificates"), PDFAppSettingsSectionRegistry::getGroupHint(getCertificatesGroupKey()));
    if (!group)
    {
        return;
    }

    m_certificateTable = new PDFAppCertificateTable(getContext(), group);
    group->insertWidget(m_certificateTable);
    connect(m_certificateTable.data(), &PDFAppCertificateTable::statusMessageRequested, this, &PDFAppSecuritySettingsSection::statusMessageRequested);
}

QString PDFAppSecuritySettingsSection::getRestoreDefaultsText(int count) const
{
    if (count == 1)
    {
        return tr("This setting of the %1 section goes back to its default value. Your trusted certificates, the other sections, your history and your shortcuts stay as they are.").arg(getTitle());
    }
    return tr("These %1 settings of the %2 section go back to their default values. Your trusted certificates, the other sections, your history and your shortcuts stay as they are.").arg(count).arg(getTitle());
}

QWidget* PDFAppSecuritySettingsSection::findAnchorWidget(const QString& anchor) const
{
    if (anchor == getCertificatesAnchor() && m_certificateTable)
    {
        return m_certificateTable;
    }
    return BaseClass::findAnchorWidget(anchor);
}

int PDFAppSecuritySettingsSection::applyFilter(const PDFSearchQuery& query)
{
    int matches = BaseClass::applyFilter(query);
    if (query.isEmpty() || !m_certificateTable)
    {
        return matches;
    }

    // The table is shown by a query of its texts (the group has no rows, so the
    // base hides it with its table while the page is filtered)
    PDFAppSettingGroup* group = findGroup(getCertificatesGroupKey());
    if (!group || !group->isHidden())
    {
        // A query of the section itself shows the whole group already
        return matches;
    }

    const QStringList titles = { tr("Trusted certificates"), QStringLiteral("Trusted certificates") };
    const QStringList keywords = { tr("Remove"), QStringLiteral("remove"), QStringLiteral("certificates"), QStringLiteral("trusted"),
                                   QStringLiteral("signatures"), QStringLiteral("system store"), QStringLiteral("AATL") };
    const std::vector<PDFSearchField> fields = { PDFSearchField(titles, 100, 90, 60, true), PDFSearchField(keywords, 50, 45, 30) };
    if (query.match(fields).has_value())
    {
        group->setHidden(false);
        m_certificateTable->setHidden(false);
        ++matches;
    }
    return matches;
}

void PDFAppSecuritySettingsSection::refresh(const QString& topic, const QString& key)
{
    QPointer<PDFAppSecuritySettingsSection> guard(this);
    BaseClass::refresh(topic, key);
    if (guard && m_certificateTable)
    {
        m_certificateTable->refresh(topic, key);
    }
}

PDFAppSettingsSection* PDFAppSecuritySettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppSecuritySettingsSection(context, sectionId, parent);
}

}   // namespace pdf
