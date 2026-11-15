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

#include "pdfapptoolpermissionlist.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcomponentrows.h"
#include "pdfdocumenttool_encryption.h"

#include <QEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <vector>

namespace pdf
{

/// The rows of the list, no state
class PDFAppToolPermissionListHelper
{
public:
    PDFAppToolPermissionListHelper() = delete;

    static constexpr const char* KEY_PROPERTY = "pdf4qtPermission";

    /// A row of 432:2105: the key, its group (0 Printing, 1 Changing, 2 Copying) and its icon
    struct Row
    {
        const char* key;
        int group;
        const char* iconName;
    };

    /// The rows in the order of 432:2105 (the parameters of getPermissionParameters, then the
    /// accessibility)
    static std::vector<Row> getRows()
    {
        using Tool = PDFDocumentToolEncryption;
        return { { Tool::PARAMETER_PRINT_LOW, 0, "print-draft" },
                 { Tool::PARAMETER_PRINT_HIGH, 0, "printer" },
                 { Tool::PARAMETER_MODIFY, 1, "edit" },
                 { Tool::PARAMETER_ASSEMBLE, 1, "pages" },
                 { Tool::PARAMETER_ANNOTATE, 1, "message-square" },
                 { Tool::PARAMETER_FILL_FORMS, 1, "form-fill" },
                 { Tool::PARAMETER_COPY, 2, "copy" },
                 { PDFAppToolPermissionList::ACCESSIBILITY_ROW, 2, "accessibility" } };
    }

    static QString getTitle(const QString& key)
    {
        using Tool = PDFDocumentToolEncryption;
        static constexpr const char* CONTEXT = "pdf::PDFAppToolPermissionList";
        static const std::map<QString, const char*> titles = {
            { QString::fromLatin1(Tool::PARAMETER_PRINT_LOW), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Print in low resolution") },
            { QString::fromLatin1(Tool::PARAMETER_PRINT_HIGH), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Print in high resolution") },
            { QString::fromLatin1(Tool::PARAMETER_MODIFY), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Modify document contents") },
            { QString::fromLatin1(Tool::PARAMETER_ASSEMBLE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Assemble the document") },
            { QString::fromLatin1(Tool::PARAMETER_ANNOTATE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Edit comments and form fields") },
            { QString::fromLatin1(Tool::PARAMETER_FILL_FORMS), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Fill in forms") },
            { QString::fromLatin1(Tool::PARAMETER_COPY), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Copy and extract content") },
            { QString::fromLatin1(PDFAppToolPermissionList::ACCESSIBILITY_ROW), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Access for screen readers") },
        };
        const auto it = titles.find(key);
        return it != titles.end() ? QCoreApplication::translate(CONTEXT, it->second) : QString();
    }

    static QString getDescription(const QString& key, bool isCompact)
    {
        using Tool = PDFDocumentToolEncryption;
        static constexpr const char* CONTEXT = "pdf::PDFAppToolPermissionList";

        // The compact rows of 433:4426 say three of them shorter
        static const std::map<QString, const char*> compactDescriptions = {
            { QString::fromLatin1(Tool::PARAMETER_ASSEMBLE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Insert, rotate and delete pages") },
            { QString::fromLatin1(Tool::PARAMETER_ANNOTATE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Add or change annotations") },
            { QString::fromLatin1(Tool::PARAMETER_COPY), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Copy text and images elsewhere") },
        };
        const auto compactIt = compactDescriptions.find(key);
        if (isCompact && compactIt != compactDescriptions.end())
        {
            return QCoreApplication::translate(CONTEXT, compactIt->second);
        }

        // 432:2105; the accessibility is "Always allowed in PDF 2.0" (Q5.2: ISO 32000-2 Table 22, bit 10)
        static const std::map<QString, const char*> descriptions = {
            { QString::fromLatin1(Tool::PARAMETER_PRINT_LOW), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Draft quality only") },
            { QString::fromLatin1(Tool::PARAMETER_PRINT_HIGH), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Full quality printing") },
            { QString::fromLatin1(Tool::PARAMETER_MODIFY), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Edit text, images and other page content") },
            { QString::fromLatin1(Tool::PARAMETER_ASSEMBLE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Insert, rotate and delete pages, edit bookmarks") },
            { QString::fromLatin1(Tool::PARAMETER_ANNOTATE), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Add or change annotations and interactive items") },
            { QString::fromLatin1(Tool::PARAMETER_FILL_FORMS), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Fill existing form fields and sign") },
            { QString::fromLatin1(Tool::PARAMETER_COPY), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Copy text and images to other applications") },
            { QString::fromLatin1(PDFAppToolPermissionList::ACCESSIBILITY_ROW), QT_TRANSLATE_NOOP("pdf::PDFAppToolPermissionList", "Always allowed in PDF 2.0") },
        };
        const auto it = descriptions.find(key);
        return it != descriptions.end() ? QCoreApplication::translate(CONTEXT, it->second) : QString();
    }

    static QString getGroupTitle(int group)
    {
        switch (group)
        {
            case 0:
                return PDFAppToolPermissionList::tr("Printing");
            case 1:
                return PDFAppToolPermissionList::tr("Changing");
            case 2:
                return PDFAppToolPermissionList::tr("Copying");
            default:
                break;
        }
        return QString();
    }
};

PDFAppToolPermissionList::PDFAppToolPermissionList(Layout layout, QWidget* parent) :
    BaseClass(parent),
    m_layout(layout)
{
    using Helper = PDFAppToolPermissionListHelper;

    setObjectName(QStringLiteral("permissionList"));
    QVBoxLayout* listLayout = new QVBoxLayout(this);
    listLayout->setContentsMargins(0, 0, 0, 0);

    // 432:2105: three cards 12 apart (card-Printing, card-Changing, card-Copying), a caption of
    // 21 and the rows of 44, 2 apart, padding 8 + the border; 433:4426: the rows only, 2 apart,
    // without captions - the collapsible card of the GUI holds them
    listLayout->setSpacing(m_layout == Layout::Grouped ? 12 : 2);
    QWidget* card = nullptr;
    QVBoxLayout* cardLayout = listLayout;
    int group = -1;
    for (const Helper::Row& row : Helper::getRows())
    {
        const QString key = QString::fromLatin1(row.key);
        if (!card || (m_layout == Layout::Grouped && row.group != group))
        {
            if (m_layout == Layout::Grouped)
            {
                PDFAppToolCard* groupCard = new PDFAppToolCard(this);
                groupCard->setObjectName(QStringLiteral("permissionCard-%1").arg(row.group));
                groupCard->getBodyLayout()->setContentsMargins(9, 9, 9, 9);
                groupCard->getBodyLayout()->setSpacing(2);
                listLayout->addWidget(groupCard);
                card = groupCard;
                cardLayout = groupCard->getBodyLayout();

                PDFTileRow* caption = new PDFTileRow(QString(), QString(), QString(), PDFTileRow::Kind::Caption, card);
                caption->setObjectName(QStringLiteral("permissionCaption-%1").arg(row.group));
                caption->setClickable(false);
                cardLayout->addWidget(caption);
                m_captions[QString::number(row.group)] = caption;
            }
            else
            {
                card = this;
            }
            group = row.group;
        }

        PDFTileRow* tileRow = new PDFTileRow(QString(), QString(), QString::fromLatin1(row.iconName), PDFTileRow::Kind::ListItem, card);
        tileRow->setObjectName(QStringLiteral("permission-") + key);
        tileRow->setProperty(Helper::KEY_PROPERTY, key);
        tileRow->setCheckBoxVisible(true);
        connect(tileRow, &PDFTileRow::checkBoxToggled, this, &PDFAppToolPermissionList::onCheckBoxToggled);
        connect(tileRow, &PDFTileRow::clicked, this, &PDFAppToolPermissionList::onRowClicked);
        cardLayout->addWidget(tileRow);
        m_rows[key] = tileRow;
    }

    // Q5.2: the accessibility is checked and disabled, it is no value of the list
    if (PDFTileRow* accessibility = getRow(QString::fromLatin1(ACCESSIBILITY_ROW)))
    {
        accessibility->setCheckBoxEnabled(false);
        accessibility->setClickable(false);
    }

    retranslate();
    refresh();
}

PDFAppToolPermissionList::~PDFAppToolPermissionList() = default;

QVariantMap PDFAppToolPermissionList::applyChange(QVariantMap values, const QString& parameter, bool isChecked)
{
    using Tool = PDFDocumentToolEncryption;
    const QString printLow = QString::fromLatin1(Tool::PARAMETER_PRINT_LOW);
    const QString printHigh = QString::fromLatin1(Tool::PARAMETER_PRINT_HIGH);
    const QString annotate = QString::fromLatin1(Tool::PARAMETER_ANNOTATE);
    const QString fillForms = QString::fromLatin1(Tool::PARAMETER_FILL_FORMS);

    values.insert(parameter, isChecked);

    // K5.8, Table 22: bit 12 means nothing without bit 3 - high checks low, unchecking low unchecks high
    if (parameter == printHigh && isChecked)
    {
        values.insert(printLow, true);
    }
    if (parameter == printLow && !isChecked)
    {
        values.insert(printHigh, false);
    }

    // Bit 6 allows filling of the form fields too: Fill in forms is checked with it (and cannot
    // be unchecked while bit 6 is on, refresh disables it)
    if (values.value(annotate).toBool())
    {
        values.insert(fillForms, true);
    }

    // Bit 4 does not include the assembly (bit 11, "even if bit 4 is clear"): no dependency
    return values;
}

void PDFAppToolPermissionList::setValues(const QVariantMap& values)
{
    m_values.clear();
    for (const auto& item : PDFDocumentToolEncryption::getPermissionParameters())
    {
        const QString key = QString::fromLatin1(item.first);
        m_values.insert(key, values.value(key).toBool());
    }
    refresh();
}

QVariantMap PDFAppToolPermissionList::getValues() const
{
    return m_values;
}

int PDFAppToolPermissionList::getAllowedCount() const
{
    // The accessibility is always allowed (Q5.2): it counts in "N of 8"
    int count = 1;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
    {
        count += it.value().toBool() ? 1 : 0;
    }
    return count;
}

PDFTileRow* PDFAppToolPermissionList::getRow(const QString& parameter) const
{
    const auto it = m_rows.find(parameter);
    return it != m_rows.end() ? it->second.data() : nullptr;
}

void PDFAppToolPermissionList::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppToolPermissionList::retranslate()
{
    using Helper = PDFAppToolPermissionListHelper;

    for (const auto& [key, row] : m_rows)
    {
        if (row)
        {
            row->setTitle(Helper::getTitle(key));
            row->setDescription(Helper::getDescription(key, m_layout == Layout::Compact));
        }
    }
    for (const auto& [group, caption] : m_captions)
    {
        if (caption)
        {
            caption->setTitle(Helper::getGroupTitle(group.toInt()));
        }
    }
}

void PDFAppToolPermissionList::refresh()
{
    const QString annotate = QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_ANNOTATE);
    const QString fillForms = QString::fromLatin1(PDFDocumentToolEncryption::PARAMETER_FILL_FORMS);
    const bool isAnnotate = m_values.value(annotate).toBool();
    for (const auto& [key, row] : m_rows)
    {
        if (!row)
        {
            continue;
        }

        const QSignalBlocker blocker(row);
        if (key == QLatin1String(ACCESSIBILITY_ROW))
        {
            row->setCheckBoxChecked(true);
            continue;
        }

        // K5.8: Fill in forms checked and disabled while Edit comments and form fields is checked
        const bool isLockedByAnnotate = key == fillForms && isAnnotate;
        row->setCheckBoxEnabled(!isLockedByAnnotate);
        row->setClickable(!isLockedByAnnotate);
        row->setCheckBoxChecked(m_values.value(key).toBool() || isLockedByAnnotate);
        row->update();
    }
}

void PDFAppToolPermissionList::toggle(const QString& parameter, bool isChecked)
{
    if (!m_values.contains(parameter))
    {
        return;
    }

    // The state first, the signal last: its slot writes the model (and may rebuild anything)
    m_values = applyChange(m_values, parameter, isChecked);
    refresh();
    const QVariantMap values = m_values;
    Q_EMIT valuesEdited(values);
}

void PDFAppToolPermissionList::onCheckBoxToggled(bool isChecked)
{
    QObject* row = sender();
    if (row)
    {
        toggle(row->property(PDFAppToolPermissionListHelper::KEY_PROPERTY).toString(), isChecked);
    }
}

void PDFAppToolPermissionList::onRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (row && row->isCheckBoxEnabled())
    {
        toggle(row->property(PDFAppToolPermissionListHelper::KEY_PROPERTY).toString(), !row->isCheckBoxChecked());
    }
}

}   // namespace pdf

#include "moc_pdfapptoolpermissionlist.cpp"
