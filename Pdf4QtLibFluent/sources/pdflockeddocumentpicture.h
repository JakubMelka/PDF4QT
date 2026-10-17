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

#ifndef PDFLOCKEDDOCUMENTPICTURE_H
#define PDFLOCKEDDOCUMENTPICTURE_H

#include "pdffluentglobal.h"
#include "pdfpreviewpainter.h"

#include <QWidget>

namespace pdf
{

/// Illustration of the screen of a locked document (component Locked document
/// 225:906): a document with a lock badge, blue while the document asks for
/// its password and red after a wrong password. A picture without interaction,
/// 160 x 160; the accessible name says the state ("Locked document",
/// "Wrong password").
///
/// The shadow of the badge reaches beyond the 160 x 160 of the component (the
/// component does not clip it, \p PDFPreviewPainter::getLockedDocumentOverflowMargins).
/// The widget keeps the size of the component and its graphics effect draws
/// that part of the shadow into the parent (as the shadow of PDFPageThumbnail),
/// so keep the margins free of other widgets. Replacing the graphics effect
/// removes the part of the shadow outside of the widget.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLockedDocumentPicture : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using State = PDFPreviewPainter::LockedState;

    explicit PDFLockedDocumentPicture(QWidget* parent = nullptr);
    explicit PDFLockedDocumentPicture(State state, QWidget* parent = nullptr);

    State getState() const { return m_state; }

    /// Sets the state (an invalid value is Locked)
    void setState(State state);

    /// Returns the text of the state for assistive technologies ("Locked document" or "Wrong password", translated)
    QString getStateText() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateAccessibleName();

    State m_state = State::Locked;
    QString m_stateText;
};

}   // namespace pdf

#endif // PDFLOCKEDDOCUMENTPICTURE_H
