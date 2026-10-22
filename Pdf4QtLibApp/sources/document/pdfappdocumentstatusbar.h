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

#ifndef PDFAPPDOCUMENTSTATUSBAR_H
#define PDFAPPDOCUMENTSTATUSBAR_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFLabel;

/// Status bar of the document view (Figma 193:1319: 28 px, bg/sidebar with the
/// border border/default at the top, the message Regular 11 text/muted 14 px from
/// the leading edge, the items of the packages at the trailing side with 12 px
/// between them and 8 px from the edge: signatures 266:7859 (D), rendering issues
/// 193:1321 (F)). Items without anything to report are hidden by their package.
/// The page item ("Page 12 (xii) of 240", Status bar item 190:658) is shown by the
/// view for documents with page labels; a click on it opens Go to.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentStatusBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Height of the status bar
    static constexpr int HEIGHT = 28;

    /// Place of an item
    enum class Area
    {
        Leading,    ///< After the message (left in left-to-right)
        Trailing    ///< At the trailing edge (right in left-to-right), in the order of adding
    };

    explicit PDFAppDocumentStatusBar(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentStatusBar() override;

    /// Adds the item (the status bar takes it over); nullptr is ignored
    void addItem(QWidget* item, Area area);

    /// Items in the order of adding
    std::vector<QWidget*> getItems() const;

    /// Text of the message part ("Ready", "12 (xii) of 240"); \p timeout in
    /// milliseconds restores the permanent message (0 = the message is permanent)
    void showMessage(const QString& text, int timeout = 0);

    /// Shown message
    QString getMessage() const;

    /// Text of the page item (empty = the item is hidden)
    void setPageText(const QString& text);
    QString getPageText() const;

    /// The page item (a Status bar item, leading after the message)
    QToolButton* getPageItem() const;

Q_SIGNALS:
    /// The page item has been clicked
    void pageItemClicked();

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onMessageTimeout();

    QPointer<QHBoxLayout> m_layout;
    QPointer<PDFLabel> m_message;
    QPointer<QToolButton> m_pageItem;
    QPointer<QTimer> m_messageTimer;
    QString m_permanentMessage;
    int m_leadingCount = 0;
    std::vector<QPointer<QWidget>> m_items;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSTATUSBAR_H
