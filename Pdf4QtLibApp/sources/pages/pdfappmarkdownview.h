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

#ifndef PDFAPPMARKDOWNVIEW_H
#define PDFAPPMARKDOWNVIEW_H

#include "pdfappglobal.h"
#include "pdfappmarkdown.h"

#include <QList>
#include <QRect>
#include <QWidget>

#include <memory>

namespace pdf
{

class PDFAppMarkdownLayout;

/// View of a Markdown document (the user manual /help, the release notes of What's
/// New; Figma pdf4qt-help 955:152873, pdf4qt-whatsnew-notes 989:152125): a card
/// bg/surface with the border border/default (radius 12, paddings 20 / 24 / 24 / 24),
/// whose blocks are laid out and painted by the view itself - paragraphs Regular 13 / 20
/// text/secondary, headings Semi Bold 14 text/primary, lists with the markers in a
/// column of 18 px (numbers Medium text/accent, bullets text/tertiary), tables
/// (header bg/subtle, Semi Bold 12, rows 12 / 17 divided by border/divider), callouts
/// (> [!NOTE] ...: the hue of the kind, an icon of 16 px, a title Semi Bold 12),
/// code (bg/subtle, a fixed font), block quotes and pictures of the resources.
///
/// Every color is a token of the active theme taken at the layout (QTextLayout with
/// explicit formats, never a QTextDocument or a palette), so nothing is drawn from the
/// palette of the application (no leak color). The height follows the width.
///
/// Links are reached by Tab (each link is one stop, the focus ring is drawn around it
/// after the keyboard) and activated by the mouse, Enter or Space: the view emits
/// linkActivated - routes and https addresses are opened by the page through the
/// navigator, anchors scroll the page. The signal may delete the view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppMarkdownView(QWidget* parent = nullptr);
    virtual ~PDFAppMarkdownView() override;

    /// Paddings of the card with its border of 1 px (markdown 955:152873: the
    /// border, then the paddings 20 / 24 / 24 inside of it)
    static constexpr int PADDING_TOP = 1 + 20;
    static constexpr int PADDING_SIDE = 1 + 24;
    static constexpr int PADDING_BOTTOM = 1 + 24;

    /// Parses and shows the Markdown text
    void setMarkdown(const QString& text);

    /// Shows the parsed document
    void setDocument(const PDFAppMarkdownDocument& document);
    const PDFAppMarkdownDocument& getDocument() const { return m_document; }

    /// Folder of the resources, against which the relative paths of the pictures
    /// are resolved (":/help/"). Only pictures of the resources are shown.
    void setResourceBase(const QString& base);
    const QString& getResourceBase() const { return m_resourceBase; }

    /// The card around the content is drawn (default true)
    void setCardVisible(bool isVisible);
    bool isCardVisible() const { return m_isCardVisible; }

    /// Top of the heading of the anchor in the coordinates of the view, -1 if there is none
    int getAnchorPosition(const QString& anchor) const;

    /// Links of the document in their order (only accepted targets)
    int getLinkCount() const;
    QString getLinkTarget(int index) const;
    PDFAppMarkdownLinkKind getLinkKind(int index) const;

    /// Rectangles of the link in the coordinates of the view
    QList<QRect> getLinkRects(int index) const;

    /// Link, which has the keyboard focus (-1 = none)
    int getFocusedLink() const { return m_focusedLink; }

    /// Link under the pointer (-1 = none)
    int getHoveredLink() const { return m_hoveredLink; }

    /// Plain text of the document (accessibility, tests)
    QString getPlainText() const;

    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A link has been activated (the view may be deleted by a slot)
    void linkActivated(const QString& target, pdf::PDFAppMarkdownLinkKind kind);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;
    virtual bool focusNextPrevChild(bool next) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    const PDFAppMarkdownLayout& getLayout(int width) const;
    void invalidateLayout();
    void onThemeChanged();
    int getLinkAt(const QPointF& point) const;
    void setHoveredLink(int link);
    void setFocusedLink(int link);
    void activateLink(int link);
    void ensureLinkVisible(int link);
    void updateFocusPolicy();

    PDFAppMarkdownDocument m_document;
    QString m_resourceBase = QStringLiteral(":/");
    bool m_isCardVisible = true;
    int m_hoveredLink = -1;
    int m_pressedLink = -1;
    int m_focusedLink = -1;

    /// Layouts of the last widths (heightForWidth asks for other widths)
    mutable std::vector<std::unique_ptr<PDFAppMarkdownLayout>> m_layouts;
};

}   // namespace pdf

#endif // PDFAPPMARKDOWNVIEW_H
