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

#include "pdflockeddocumentpicture.h"
#include "pdfthememanager.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsEffect>
#include <QPainter>

namespace pdf
{

#if QT_CONFIG(accessibility)
/// Accessible interface of the illustration of a locked document: a graphic
class PDFLockedDocumentPictureAccessible : public QAccessibleWidget
{
public:
    explicit PDFLockedDocumentPictureAccessible(QWidget* widget) :
        QAccessibleWidget(widget, QAccessible::Graphic)
    {

    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        if (className != QLatin1String(PDFLockedDocumentPicture::staticMetaObject.className()))
        {
            return nullptr;
        }

        PDFLockedDocumentPicture* picture = qobject_cast<PDFLockedDocumentPicture*>(object);
        return picture ? new PDFLockedDocumentPictureAccessible(picture) : nullptr;
    }
};

/// Registration of the accessible interface (removed, when the library is
/// unloaded while the application still runs)
class PDFLockedDocumentPictureAccessibleRegistration
{
public:
    PDFLockedDocumentPictureAccessibleRegistration()
    {
        QAccessible::installFactory(&PDFLockedDocumentPictureAccessible::create);
    }

    ~PDFLockedDocumentPictureAccessibleRegistration()
    {
        if (QCoreApplication::instance())
        {
            QAccessible::removeFactory(&PDFLockedDocumentPictureAccessible::create);
        }
    }

    PDFLockedDocumentPictureAccessibleRegistration(const PDFLockedDocumentPictureAccessibleRegistration&) = delete;
    PDFLockedDocumentPictureAccessibleRegistration& operator=(const PDFLockedDocumentPictureAccessibleRegistration&) = delete;
};
#endif

/// Part of the shadow of the badge, which lies outside of the illustration: it
/// is drawn around the widget (into its parent), as the component does not clip
/// it, while the widget keeps the size of the component
class PDFLockedDocumentShadowEffect : public QGraphicsEffect
{
public:
    explicit PDFLockedDocumentShadowEffect(PDFLockedDocumentPicture* picture) :
        QGraphicsEffect(picture),
        m_picture(picture)
    {

    }

    virtual QRectF boundingRectFor(const QRectF& rect) const override
    {
        return rect.marginsAdded(QMarginsF(PDFPreviewPainter::getLockedDocumentOverflowMargins()));
    }

protected:
    virtual void draw(QPainter* painter) override
    {
        // The painter is in the logical coordinates of the widget. The effect
        // draws the whole illustration with the shadow outside of it directly
        // (the widget has no children): a pixmap of the widget kept by the
        // effect would keep the colors of the previous theme, while the hidden
        // widget is not repainted.
        const QRectF source = sourceBoundingRect(Qt::LogicalCoordinates);
        if (m_picture && !source.isEmpty())
        {
            PDFPreviewPainter::paintLockedDocument(painter, source, m_picture->getState(), true);
        }
    }

private:
    PDFLockedDocumentPicture* m_picture;
};

PDFLockedDocumentPicture::PDFLockedDocumentPicture(QWidget* parent) :
    PDFLockedDocumentPicture(State::Locked, parent)
{

}

PDFLockedDocumentPicture::PDFLockedDocumentPicture(State state, QWidget* parent) :
    BaseClass(parent),
    m_state(PDFPreviewPainter::normalized(state))
{
#if QT_CONFIG(accessibility)
    // Installed once, removed when the library is unloaded
    static PDFLockedDocumentPictureAccessibleRegistration registration;
    Q_UNUSED(registration);
#endif

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setGraphicsEffect(new PDFLockedDocumentShadowEffect(this));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    updateAccessibleName();
}

void PDFLockedDocumentPicture::setState(State state)
{
    state = PDFPreviewPainter::normalized(state);
    if (m_state != state)
    {
        m_state = state;
        updateAccessibleName();
        update();
    }
}

QString PDFLockedDocumentPicture::getStateText() const
{
    return m_state == State::Wrong ? tr("Wrong password") : tr("Locked document");
}

void PDFLockedDocumentPicture::updateAccessibleName()
{
    // A name set by the application is kept
    const QString previousText = m_stateText;
    m_stateText = getStateText();
    if (accessibleName().isEmpty() || accessibleName() == previousText)
    {
        setAccessibleName(m_stateText);
    }
}

QSize PDFLockedDocumentPicture::sizeHint() const
{
    // The size of the component, the shadow outside of it takes no space
    return PDFPreviewPainter::getLockedDocumentSize();
}

QSize PDFLockedDocumentPicture::minimumSizeHint() const
{
    return sizeHint();
}

void PDFLockedDocumentPicture::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFPreviewPainter::paintLockedDocument(&painter, QRectF(rect()), m_state);
}

void PDFLockedDocumentPicture::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateAccessibleName();
    }
}

}   // namespace pdf
