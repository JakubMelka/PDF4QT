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

#include "pdfappdocumentstatusbar.h"

#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QToolButton>

namespace pdf
{

PDFAppDocumentStatusBar::PDFAppDocumentStatusBar(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("documentStatusBar"));
    setFixedHeight(HEIGHT);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(14, 0, 8, 0);
    m_layout->setSpacing(12);

    m_message = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_message->setObjectName(QStringLiteral("statusMessage"));
    m_message->setMinimumWidth(0);
    m_message->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_layout->addWidget(m_message);

    // The page item (Status bar item 190:658, 22 px high, icon 14) - the first leading item
    m_pageItem = new QToolButton(this);
    m_pageItem->setObjectName(QStringLiteral("statusPage"));
    m_pageItem->setIcon(PDFThemeIcons::getIcon(QStringLiteral("pages")));
    m_pageItem->setIconSize(QSize(14, 14));
    m_pageItem->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pageItem->setFocusPolicy(Qt::TabFocus);
    PDFWidgetStyleProperties::set(m_pageItem, PDFWidgetStyleProperties::STATUS_ITEM);
    m_pageItem->hide();
    connect(m_pageItem.data(), &QToolButton::clicked, this, &PDFAppDocumentStatusBar::pageItemClicked);
    m_layout->addWidget(m_pageItem);
    m_layout->addStretch(1);

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, &PDFAppDocumentStatusBar::onMessageTimeout);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentStatusBar::update));
}

PDFAppDocumentStatusBar::~PDFAppDocumentStatusBar() = default;

void PDFAppDocumentStatusBar::addItem(QWidget* item, Area area)
{
    if (!item || !m_layout)
    {
        return;
    }

    item->setParent(this);
    if (area == Area::Leading)
    {
        // After the message and the previous leading items, before the stretch (the
        // items may have been deleted by their packages, so the stretch is searched)
        int stretchIndex = m_layout->count();
        for (int i = 0; i < m_layout->count(); ++i)
        {
            if (m_layout->itemAt(i) && m_layout->itemAt(i)->spacerItem())
            {
                stretchIndex = i;
                break;
            }
        }
        m_layout->insertWidget(stretchIndex, item);
    }
    else
    {
        m_layout->addWidget(item);
    }
    m_items.emplace_back(item);
}

std::vector<QWidget*> PDFAppDocumentStatusBar::getItems() const
{
    std::vector<QWidget*> items;
    for (const QPointer<QWidget>& item : m_items)
    {
        if (item)
        {
            items.push_back(item.data());
        }
    }
    return items;
}

void PDFAppDocumentStatusBar::showMessage(const QString& text, int timeout)
{
    if (!m_message)
    {
        return;
    }

    if (timeout <= 0)
    {
        m_permanentMessage = text;
        if (m_messageTimer)
        {
            m_messageTimer->stop();
        }
    }
    else if (m_messageTimer)
    {
        m_messageTimer->start(timeout);
    }
    m_message->setText(text);
}

QString PDFAppDocumentStatusBar::getMessage() const
{
    return m_message ? m_message->text() : QString();
}

void PDFAppDocumentStatusBar::setPageText(const QString& text)
{
    if (m_pageItem)
    {
        m_pageItem->setText(text);
        m_pageItem->setToolTip(text.isEmpty() ? QString() : tr("Go to page…"));
        m_pageItem->setAccessibleName(text);
        m_pageItem->setVisible(!text.isEmpty());
    }
}

QString PDFAppDocumentStatusBar::getPageText() const
{
    return m_pageItem ? m_pageItem->text() : QString();
}

QToolButton* PDFAppDocumentStatusBar::getPageItem() const
{
    return m_pageItem.data();
}

void PDFAppDocumentStatusBar::onMessageTimeout()
{
    if (m_message)
    {
        m_message->setText(m_permanentMessage);
    }
}

void PDFAppDocumentStatusBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/sidebar with the border border/default at the top (status-bar 193:1319)
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSidebar));
    painter.fillRect(QRect(0, 0, width(), 1), manager->getColor(PDFThemeColors::BorderDefault));
}

}   // namespace pdf
