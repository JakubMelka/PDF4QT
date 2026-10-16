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

#ifndef PDFCOMPONENTROWS_H
#define PDFCOMPONENTROWS_H

#include "pdffluentglobal.h"

#include <QAbstractButton>

class QButtonGroup;
class QVBoxLayout;

namespace pdf
{

/// Item of a navigation (components Nav item and Settings nav item). The item
/// is checkable, the checked item is the current page. The selected item does
/// not react to the pointer. The focus ring of 2 px lies inside of the item.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFNavItem : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    /// Look of the item
    enum class Kind
    {
        Home,       ///< Nav item: 190 x 38, icon 20, label Regular 13 (Semi Bold, when selected)
        Settings    ///< Settings nav item: 300 x 52, icon 20, name Semi Bold 13 and a subtitle Regular 11
    };

    explicit PDFNavItem(QWidget* parent = nullptr);
    explicit PDFNavItem(const QString& text, const QString& iconName, Kind kind = Kind::Home, QWidget* parent = nullptr);

    Kind getKind() const { return m_kind; }
    void setKind(Kind kind);

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    /// Subtitle of the item of the settings (one line summary of the section).
    /// It is also the description of the item for assistive technologies.
    const QString& getSubtitle() const { return m_subtitle; }
    void setSubtitle(const QString& subtitle);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void nextCheckState() override;

private:
    void init();

    QString m_iconName;
    QString m_subtitle;
    Kind m_kind = Kind::Home;
};

/// Column of navigation items with a single selection. The items can be split
/// into groups divided by a line (the navigation of the Home screen has three
/// groups). The arrows up and down move the selection over the borders of the groups.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFNavList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFNavList(QWidget* parent = nullptr);
    explicit PDFNavList(PDFNavItem::Kind kind, QWidget* parent = nullptr);

    /// Adds an item to the last group. The first item becomes the current one.
    /// \returns Index of the item
    int addItem(const QString& text, const QString& iconName, const QString& subtitle = QString());

    /// Starts a new group of items, it is divided from the previous one by a line
    void addGroup();

    int getCount() const;
    PDFNavItem* getItem(int index) const;

    int getCurrentIndex() const;
    void setCurrentIndex(int index);

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QVBoxLayout* m_layout;
    QButtonGroup* m_group;
    PDFNavItem::Kind m_kind = PDFNavItem::Kind::Home;
};

}   // namespace pdf

#endif // PDFCOMPONENTROWS_H
