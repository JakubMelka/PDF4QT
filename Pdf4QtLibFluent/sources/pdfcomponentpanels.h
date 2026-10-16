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

#ifndef PDFCOMPONENTPANELS_H
#define PDFCOMPONENTPANELS_H

#include "pdffluentglobal.h"
#include "pdffluentwidgets.h"

#include <QTabBar>

#include <vector>

namespace pdf
{

/// Editable value in a toolbar (component Toolbar field): the current page with
/// the page count, or the zoom. The value is Semi Bold 13, the muted suffix
/// ("/ 1592") follows it 4 px after. The field is 32 px high, at least 64 px
/// wide, and it is as wide as its value and its suffix (10 px from the border).
/// The focus ring of 2 px lies inside of the field, the disabled field is
/// filled with bg/muted and its texts are text/disabled.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolBarField : public PDFLineEdit
{
    Q_OBJECT

private:
    using BaseClass = PDFLineEdit;

public:
    explicit PDFToolBarField(QWidget* parent = nullptr);
    explicit PDFToolBarField(const QString& text, const QString& suffix = QString(), QWidget* parent = nullptr);

    /// Suffix drawn after the value ("/ 1592", "%"), it is not a part of the edited text.
    /// It is also the description of the field for assistive technologies.
    const QString& getSuffix() const { return m_suffix; }
    void setSuffix(const QString& suffix);

    bool isSuffixVisible() const { return m_suffixVisible; }
    void setSuffixVisible(bool visible);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();
    void updateTextInsets();
    qreal getValueWidth() const;
    qreal getSuffixWidth() const;

    QString m_suffix;
    bool m_suffixVisible = true;
};

/// Rows of labels and values of a card (component Key value row): build
/// information, facts of a tool, properties of a document. One widget draws all
/// rows of the card. A row is 28 px high, the label (Regular 12 text/tertiary)
/// has the width 120 and it wraps, the value (Medium 12 text/primary) is aligned
/// to the right and elided, its whole text is the tooltip of an elided value.
/// The context menu of a row copies its value.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFKeyValueList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFKeyValueList(QWidget* parent = nullptr);

    /// Adds a row
    /// \returns Index of the row
    int addRow(const QString& key, const QString& value);

    /// Changes the value of the row
    void setValue(int index, const QString& value);

    /// Removes all rows
    void clear();

    int getRowCount() const { return static_cast<int>(m_rows.size()); }
    QString getKey(int index) const;
    QString getValue(int index) const;

    /// Returns the rectangle of the row in the coordinates of the widget
    QRect getRowRect(int index) const;

    /// Returns the index of the row at the position, -1 if there is no row
    int getRowAt(const QPoint& position) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Row
    {
        QString key;
        QString value;
    };

    int getRowHeight(int index) const;
    void updateDescription();

    std::vector<Row> m_rows;
};

/// Tabs of the documents (component Tab): a tab shows the lock of a locked
/// document (amber in every state), the dot of unsaved changes (in the color of
/// the label) and the progress of a running tool (line of 2 px at the bottom).
/// Every tab is 32 px high, also without the close button.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDocumentTabBar : public QTabBar
{
    Q_OBJECT

private:
    using BaseClass = QTabBar;

public:
    explicit PDFDocumentTabBar(QWidget* parent = nullptr);

    bool isTabLocked(int index) const;
    void setTabLocked(int index, bool locked);

    bool isTabUnsaved(int index) const;
    void setTabUnsaved(int index, bool unsaved);

    /// Progress of the tab in the range 0 to 1, a negative value hides the line
    qreal getTabProgress(int index) const;
    void setTabProgress(int index, qreal progress);

    /// Shows or hides the close button of the tab (the Home tab has none)
    void setTabClosable(int index, bool closable);
    bool isTabClosable(int index) const;

protected:
    virtual QSize tabSizeHint(int index) const override;
    virtual QSize minimumTabSizeHint(int index) const override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void tabInserted(int index) override;
    virtual void tabRemoved(int index) override;

private:
    struct TabState
    {
        bool locked = false;
        bool unsaved = false;
        qreal progress = -1.0;
    };

    const TabState* getTabState(int index) const;
    TabState* getTabState(int index);
    int getExtraWidth(int index) const;
    void updateAccessibleName(int index);
    void changeTabState(int index);

    /// State of the tabs by their position. QTabBar keeps the data of a tab
    /// (tabData) for the application, so the state is held here.
    std::vector<TabState> m_tabs;
};

}   // namespace pdf

#endif // PDFCOMPONENTPANELS_H
