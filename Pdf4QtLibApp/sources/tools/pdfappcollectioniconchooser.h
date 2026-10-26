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

#ifndef PDFAPPCOLLECTIONICONCHOOSER_H
#define PDFAPPCOLLECTIONICONCHOOSER_H

#include "pdfappglobal.h"

#include "pdfcomponentbuttons.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <vector>

class QAbstractButton;
class QButtonGroup;

namespace pdf
{

class PDFIconChoice;

/// Icon chooser of the collections editor (Figma Manage collections 283:1888,
/// field-icon 284:2225): the ten icons of PDFAppBuiltinToolCollections::getIconChoices
/// as PDFIconChoice in an exclusive group (one stop of Tab, the arrows move the
/// selection), gap 6. The row wraps in a narrow editor (PDFAppFlowLayout); the
/// badges of a row are centered (the selected choice is 4 px larger).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCollectionIconChooser : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap between the icons (284:2227)
    static constexpr int SPACING = 6;

    explicit PDFAppCollectionIconChooser(QWidget* parent = nullptr);
    virtual ~PDFAppCollectionIconChooser() override;

    /// Selected icon (always one of the allowed icons)
    QString getIconName() const;

    /// Selects the icon without the signal iconChosen; an icon, which is not
    /// allowed, selects the default icon (folder)
    void setIconName(const QString& iconName);

    /// Choice of the icon, nullptr = none
    PDFIconChoice* getChoice(const QString& iconName) const;

    /// Name of the icon for assistive technologies and its tooltip ("Camera")
    static QString getIconText(const QString& iconName);

Q_SIGNALS:
    /// The user has chosen the icon (the mouse, the arrows, Space)
    void iconChosen(const QString& iconName);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void onButtonToggled(QAbstractButton* button, bool isChecked);

    QPointer<QButtonGroup> m_group;
    std::vector<QPointer<PDFIconChoice>> m_choices;
    QString m_iconName;
    bool m_isUpdating = false;
};

}   // namespace pdf

#endif // PDFAPPCOLLECTIONICONCHOOSER_H
