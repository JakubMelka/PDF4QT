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

#ifndef PDFAPPTOOLPERMISSIONLIST_H
#define PDFAPPTOOLPERMISSIONLIST_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include <map>

namespace pdf
{

class PDFTileRow;

/// The list of the permissions of Restrict Permissions and Encrypt with Certificate (plan of step 5
/// of phase 2, 4.2, 5.2, K5.7, K5.8, owner answer Q5.2; package P2): 8 rows with a tile (the model and
/// PDFTileRowDelegate), grouped Printing / Changing / Copying (432:2105) or compact without the groups
/// (433:4426). The dependencies of Table 22 (K5.8): checking high resolution checks low, unchecking
/// low unchecks high; Edit comments and form fields checks and disables Fill in forms; Access for
/// screen readers is checked and disabled ("Always allowed in PDF 2.0", Q5.2) and counts in "N of 8".
/// The values are the Bool permission parameters of PDFDocumentToolEncryption
/// (getPermissionParameters); the GUI keeps them in the parameter model (Undo / Redo of the list).
/// A child of its canvas or card, held by the GUI in QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPermissionList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// The rows grouped (432:2105) or compact (433:4426)
    enum class Layout
    {
        Grouped,
        Compact
    };

    /// The rows of the list: the 7 parameters and the accessibility
    static constexpr int PERMISSION_COUNT = 8;

    explicit PDFAppToolPermissionList(Layout layout, QWidget* parent);
    virtual ~PDFAppToolPermissionList() override;

    Layout getLayout() const { return m_layout; }

    /// Shows the values of the permission parameters (a preset, Undo, Redo); emits nothing
    void setValues(const QVariantMap& values);

    /// The shown values of the permission parameters
    QVariantMap getValues() const;

    /// The allowed actions of PERMISSION_COUNT, the accessibility included ("3 of 8 actions")
    int getAllowedCount() const;

    /// The row of a permission parameter, ACCESSIBILITY_ROW for the accessibility (nullptr = none)
    PDFTileRow* getRow(const QString& parameter) const;

    /// The key of the row of the accessibility (no parameter, bit 10 is always set)
    static constexpr const char* ACCESSIBILITY_ROW = "accessibility";

    /// The values after the user has set \p parameter to \p isChecked, with the dependencies of
    /// Table 22 (K5.8): high resolution needs low resolution (bit 12 only with bit 3); Edit comments
    /// and form fields includes Fill in forms (bit 6 allows filling of the fields)
    static QVariantMap applyChange(QVariantMap values, const QString& parameter, bool isChecked);

Q_SIGNALS:
    /// The user has changed a row: the new values with the dependencies of K5.8 applied (the GUI
    /// writes them to the model as one step); the last call of the change
    void valuesEdited(const QVariantMap& values);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void refresh();
    void toggle(const QString& parameter, bool isChecked);
    void onCheckBoxToggled(bool isChecked);
    void onRowClicked();

    Layout m_layout;
    QVariantMap m_values;
    std::map<QString, QPointer<PDFTileRow>> m_rows;
    std::map<QString, QPointer<PDFTileRow>> m_captions;
};

}   // namespace pdf

#endif // PDFAPPTOOLPERMISSIONLIST_H
