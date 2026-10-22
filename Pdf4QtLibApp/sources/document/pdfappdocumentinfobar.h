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

#ifndef PDFAPPDOCUMENTINFOBAR_H
#define PDFAPPDOCUMENTINFOBAR_H

#include "pdfappglobal.h"
#include "pdfappdocumentview.h"

#include <QPointer>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QProgressBar;
class QPushButton;

namespace pdf
{

class PDFIconBadge;
class PDFIconButton;
class PDFLabel;

/// Notification bar over the canvas (Figma 1028:24277 link blocked, 1030:25657
/// printing): a card bg/surface with the border border/default, the radius 14 and
/// the shadow 0 / 4 / 16, 48 px high. Inside (padding 5 / 10 / 5 / 12, gap 8): the
/// icon 14 in a circle of 24 in the colors of the tone, the title (Semi Bold 13
/// text/primary) with the optional second line (Regular 11 text/muted), the
/// progress line 160 x 4 of the tone Progress, a divider, the buttons (Secondary),
/// a divider and the close button (Tab close button). The widget holds the margins
/// of the shadow around the card; the view places the card itself.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInfoBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Height of the card and its radius
    static constexpr int CARD_HEIGHT = 48;
    static constexpr int CARD_RADIUS = 14;

    /// Width of the progress line
    static constexpr int PROGRESS_WIDTH = 160;

    /// Margins of the shadow 0 / 4 / 16 around the card
    static QMargins getShadowMargins();

    explicit PDFAppInfoBar(const PDFAppInfoBarRequest& request, QWidget* parent = nullptr);
    virtual ~PDFAppInfoBar() override;

    const PDFAppInfoBarRequest& getRequest() const { return m_request; }

    /// Replaces the content of the bar (the same id)
    void setRequest(const PDFAppInfoBarRequest& request);

    /// Rectangle of the card in the coordinates of the bar
    QRect getCardRect() const;

    /// Buttons of the actions in the order of the request, the close button (nullptr
    /// for a bar, which cannot be closed) and the progress line (nullptr without it)
    std::vector<QPushButton*> getActionButtons() const;
    PDFIconButton* getCloseButton() const;
    QProgressBar* getProgressBar() const;

    /// Icon of the tone (alert-triangle of a warning, ...)
    static QString getToneIconName(PDFAppInfoBarTone tone);

Q_SIGNALS:
    /// A button of the bar has been clicked (sent from the event loop, the slot
    /// may delete the bar together with its tab)
    void actionTriggered(const QString& infoBarId, const QString& actionId);

    /// The close button has been clicked (sent from the event loop)
    void closeRequested(const QString& infoBarId);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;

private:
    void rebuild();
    void onActionClicked();
    void onActionQueued(const QString& actionId);
    void onCloseClicked();
    void onCloseQueued();

    PDFAppInfoBarRequest m_request;
    QPointer<QHBoxLayout> m_layout;
    QPointer<PDFIconBadge> m_badge;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_text;
    QPointer<QProgressBar> m_progressBar;
    QPointer<PDFIconButton> m_closeButton;
    std::vector<QPointer<QWidget>> m_dynamicWidgets;
    std::vector<QPointer<QPushButton>> m_actionButtons;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTINFOBAR_H
