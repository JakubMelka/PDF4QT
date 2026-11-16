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

#ifndef PDFAPPTOOLGUI_SIGNBYHAND_H
#define PDFAPPTOOLGUI_SIGNBYHAND_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptooldrawingpart.h"

#include <QPointer>

namespace pdf
{

class PDFAppToolCallout;
class PDFAppToolPanelHeader;
class PDFMessageBox;

/// The GUI of Sign by Hand (plan of step 6 of phase 2, 2.3, 4.5, K6.2, K6.3, K6.10, R6-24; package P4
/// implements it; Figma 435:8619 ... 435:10812): the drawing of PDFAppToolDrawingPart over the pages of
/// the document (the toolbar Text label, Freehand curve, SVG image…, Accept mark, Reject mark, Dot, the
/// menus Shapes and Arrange, Clear All; the list of the elements by pages, Style), Undo of the drawing,
/// the question at the close (435:10500 "Discard the drawing?") and the result dialog of a changed
/// document (Keep in this tab preselected, Then continue with). The confirmation is Apply to Pages.
///
/// The drawing is the value of the parameter drawing (PDFAppToolDrawingPart::bindParameter): Undo and
/// Redo of the tool take its steps back, the model never stores it (sensitive, R6-19).
///
/// Ownership: the shell owns this GUI; the drawing part is its child object (created with it), the
/// widgets belong to the panels of the shell and are held by QPointer; the questions are new + QPointer
/// and delete themselves (closed with the GUI).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISignByHand : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUISignByHand(QObject* parent = nullptr);
    virtual ~PDFAppToolGUISignByHand() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): PDFDocumentToolSignByHand
    /// and this GUI
    static void registerSignByHandTools();

    /// The question 435:10500 "Discard the drawing?" (Keep Drawing Primary, Discard Drawing Secondary
    /// with the trash): Discard Drawing closes the tool without its drawing (PDFDocumentToolShell::
    /// closeAfterQuestion). Nothing without a drawing (the tool closes at once). \returns true, when the
    /// question is open (or was open already). The shell asks it by askClose at Cancel, Esc and Discard
    /// result.
    bool askDiscard();

    /// PDFDocumentToolGUI: askDiscard
    virtual bool askClose() override;

    /// The question of Clear All (the style of 435:10500): Keep Drawing / Clear All
    void askClearAll();

    /// Parts (tests; nullptr = none)
    PDFAppToolDrawingPart* getDrawingPart() const { return m_drawingPart.data(); }
    PDFAppToolPanelHeader* getLeftHeader() const { return m_leftHeader.data(); }
    PDFAppToolPanelHeader* getRightHeader() const { return m_rightHeader.data(); }
    QWidget* getEmptyCard() const { return m_emptyCard.data(); }
    PDFAppToolCallout* getNotDigitalCallout() const { return m_notDigitalCallout.data(); }
    PDFAppToolCallout* getSignedCallout() const { return m_signedCallout.data(); }
    PDFMessageBox* getDiscardDialog() const { return m_discardDialog.data(); }
    PDFMessageBox* getClearDialog() const { return m_clearDialog.data(); }

    /// "4 elements on pages 3 and 308" (the status, 435:8619), "4 elements on 2 pages" (the left header)
    QString getPagesText() const;

    /// "4 elements on 2 pages", "1 element on 1 page" (435:8763; also the header of Sign 434:5206)
    static QString getCountText(int elementCount, int pageCount);

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual void setContentEnabled(bool isEnabled) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;
    virtual void retranslate() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void ensureDrawingPart();
    void updateParts();

    /// The document of the tab has signatures (the callout of 2.6: the drawing is a change after them)
    bool isSigned() const;

    void onDrawingChanged();
    void onSelectionChanged();
    void onClearRequested();
    void onDiscardClicked();
    void onClearConfirmed();

    QPointer<PDFAppToolDrawingPart> m_drawingPart;     ///< Child object of this GUI
    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<PDFAppToolPanelHeader> m_rightHeader;
    QPointer<QWidget> m_emptyCard;
    QPointer<QWidget> m_elementList;
    QPointer<PDFAppParagraph> m_listHint;
    QPointer<PDFAppToolCallout> m_notDigitalCallout;
    QPointer<PDFAppToolCallout> m_signedCallout;
    QPointer<PDFMessageBox> m_discardDialog;
    QPointer<PDFMessageBox> m_clearDialog;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_SIGNBYHAND_H
