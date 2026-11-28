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

#ifndef PDFAPPEDITCONTENTCARDS_H
#define PDFAPPEDITCONTENTCARDS_H

#include "pdfappeditcontentcontext.h"
#include "pdfappglobal.h"
#include "pdfcontentdisplay.h"
#include "pdfdeferreddeletion.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QCoreApplication>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>
#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFAppEditContentCardEntry;
class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFExpanderCard;
class PDFLabel;

// Edit Content, stage E1, package P13 (cards). Plan: zadani_gui.md 4.19 (right panel), zadani.md
// 9.3. This file: the base of the cards, the stack and the cards Selection, Locked object,
// Annotations and fields, Preview; every other card is in a file pdfappeditcontentcards_<area>.*
// of its package with its entry of the registry. PDFExpanderCard: Pdf4QtLibFluent
// (pdfcomponentsections.h, agent S5).

/// The base of the cards of the right panel.
///
/// - A card is one or more sections (PDFExpanderCard of Pdf4QtLibFluent: heading, badge, the
///   link Expand / Collapse, the content shown always and the content shown only when
///   expanded). addSection creates a section with the expanded state of
///   PDFAppEditContentContext::isSectionExpanded and keeps the context told about its changes,
///   so the state survives the rebuild of the stack and of the panel.
/// - A card reads the selection, the model and the tool from the context when updateCard is
///   called, and changes the model only by commands (PDFAppEditContentContext::execute). A
///   command is never executed inside of another command: a button, whose click may come inside
///   of a change, uses requestExecute.
/// - A value typed into a field and not confirmed yet (zadani_gui.md 4.19): the card commits it
///   as its own command in commitPendingEdit and forgets it in discardPendingEdit (the base
///   connects both to the signals of the context). A numeric field confirms by Enter or by the
///   loss of the focus (one undo step); the wheel and the arrows of a field merge
///   (PDFContentCommand::mergeWith).
/// - A value, which differs between the selected objects, is shown by the "mixed" state of the
///   Fluent fields; the card writes a value only after the user has entered one.
/// - Numeric fields (PDFSpinBox / PDFDoubleSpinBox; arch/fluent-style.md "numeric fields of a
///   panel"): the card writes the value in a slot of valueEntered(value) - the one signal for
///   every commit by the user (Enter, leaving the field after typing, a step by the arrows, the
///   wheel or the buttons), emitted also for the held value and when the field was mixed, never
///   for setValue / setMixed of the program and never when the user leaves a field without
///   typing; never valueChanged or editingFinished. The slot may rebuild or delete the field.
///   commitPendingEdit calls commitEntry() of the field for a text the user typed and has not
///   confirmed. The value is set first, then setMixed(true) (a later setValue leaves the
///   state); updateCard skips a field, which has the focus (it would overwrite the entry).
/// - The buttons of a card are Secondary and keep their place between the states; in the fields
///   the letters of the tools switch nothing (the keys of the canvas act only with the focus on
///   the canvas).
/// - The card may be retired right after any command it executes (the stack is rebuilt from the
///   event loop, the tool may close): after execute it touches itself only when
///   !PDFDeferredDeletion::isRetired(this).
///
/// Threads and ownership: a part - a child of the stack, which retires it by
/// PDFDeferredDeletion::retireLater when its entry is shown no more, and builds every card again
/// after a change of the language. It keeps no state the user would lose. GUI thread. The
/// context is held by a QPointer and checked. The hook retire() discards the pending value
/// itself: the card is retired with the GUI at the end of the tool, its connections to the
/// context are cut before the hooks run, no discard signal reaches it then (R3-9).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCard : public QWidget, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap of the sections of a card and of the cards of the stack (the right panel of
    /// edit-content-v2: 14)
    static constexpr int SECTION_GAP = 14;

    /// \param cardId The identifier of the card (PDFAppEditContentCardIds)
    explicit PDFAppEditContentCard(PDFAppEditContentContext* context, const QString& cardId, QWidget* parent);
    virtual ~PDFAppEditContentCard() override;

    const QString& getCardId() const { return m_cardId; }
    PDFAppEditContentContext* getContext() const { return m_context.data(); }

    /// The state the card shows changed (the selection, the model, the editor state, the active
    /// tool, an option): the card reads it again. Called by the stack from the event loop, also
    /// right after the creation; never inside of a command. zadani.md 9.3 setSelection.
    virtual void updateCard() = 0;

    /// Commits / forgets the value of a field, which is not confirmed yet (see the class). The
    /// defaults do nothing (a card without fields).
    virtual void commitPendingEdit();
    virtual void discardPendingEdit();

    /// The sections of the card from the top (tests)
    const std::vector<QPointer<PDFExpanderCard>>& getSections() const { return m_sections; }

protected:
    /// Adds a section below the sections before. \p sectionName is empty for the one section of a
    /// card, or the name of the section of a card of more sections ("actions"): the expanded state
    /// is kept under "<card id>" or "<card id>/<section name>".
    PDFExpanderCard* addSection(const QString& title, const QString& sectionName = QString());

    virtual void retire() override;

private:
    void onSectionExpandedChanged(bool isExpanded);

    /// The signals of the context (PDFAppEditContentContext::commitPendingEdits / discardPendingEdits)
    void onPendingEditsCommitRequested();
    void onPendingEditsDiscardRequested();

    QPointer<PDFAppEditContentContext> m_context;
    QString m_cardId;
    QVBoxLayout* m_layout = nullptr;
    std::vector<QPointer<PDFExpanderCard>> m_sections;
};

/// The content of the right panel (PDFDocumentToolGUI::createRightPanel): the cards of the
/// entries of the registry, which are shown for the state of now, in their fixed order
/// (zadani_gui.md 4.19).
///
/// - **The stack is rebuilt only from the event loop**: a change of the selection, of the model,
///   of the tool, of the preview or of the requested cards only schedules the rebuild (a button
///   of a card executes a command, the selection changes and a rebuild inside of the click would
///   delete the sender). The rebuild keeps the cards, whose entries are still shown (they get
///   updateCard), creates the new ones and retires the others by retireLater.
/// - A card, which changes the model (PDFAppEditContentCardEntry::isChangingModel), is disabled
///   while PDFAppEditContentContext::isEditingAllowed is false (Preview, a run, a read-only page);
///   a card of the display, of the selection or of the state of the session (Selection, Preview,
///   Annotations and fields, Locked object, Keyboard) stays usable then (R7-8).
/// - The stack follows a new session of the context (PDFAppEditContentContext::sessionReplaced:
///   it connects to the signals of the new session and rebuilds).
///
/// Threads and ownership: a part - created by the GUI of the tool for the right panel, built
/// again after a change of the language, deleted by the shell or with the view of the document.
/// GUI thread. It owns its cards (children). The hook retire() drops the scheduled rebuild.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCardStack : public QWidget, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppEditContentCardStack(PDFAppEditContentContext* context, QWidget* parent);
    virtual ~PDFAppEditContentCardStack() override;

    /// The identifiers of the cards shown, from the top (tests: the fixed order)
    QStringList getCardIds() const;

    /// The card of the identifier, nullptr = it is not shown
    PDFAppEditContentCard* getCard(const QString& cardId) const;

    /// A rebuild is scheduled and has not run yet (tests: "the rebuild comes from the event loop")
    bool isRebuildScheduled() const { return m_isRebuildScheduled; }

    /// The entries of this file for the registry (the cards Selection, Locked object,
    /// Annotations and fields, Preview)
    static void registerParts(PDFAppEditContentRegistry* registry);

protected:
    virtual void retire() override;

private:
    void scheduleRebuild();
    void onRebuild();

    /// The signal of a change of the session, of the selection, of an option: scheduleRebuild
    void onStateChanged();

    /// PDFAppEditContentContext::sessionReplaced: the connections to the new session, a rebuild
    void onSessionReplaced();

    /// The connections to the signals of the session of the context
    void connectSession();

    QPointer<PDFAppEditContentContext> m_context;
    QVBoxLayout* m_layout = nullptr;
    std::vector<QPointer<PDFAppEditContentCard>> m_cards;  ///< The cards shown, from the top
    bool m_isRebuildScheduled = false;
};

/// A value of the selected objects for a card (zadani_gui.md 4.19 "Mixed"): the one value of all
/// of them, or "mixed", when two of them differ. Values are compared by the comparison given to
/// add (operator== by default).
template<typename T>
class PDFAppEditContentMixedValue
{
public:
    /// Adds the value of the next object
    void add(const T& value)
    {
        if (!m_value)
        {
            m_value = value;
        }
        else if (!(*m_value == value))
        {
            m_isMixed = true;
        }
    }

    /// Adds the value; two values are the same, when \p isSame says so
    template<typename Same>
    void add(const T& value, Same isSame)
    {
        if (!m_value)
        {
            m_value = value;
        }
        else if (!isSame(*m_value, value))
        {
            m_isMixed = true;
        }
    }

    /// The objects differ (the field shows Mixed)
    bool isMixed() const { return m_isMixed; }

    /// No object has given a value
    bool isEmpty() const { return !m_value.has_value(); }

    /// The value of the first object (also when mixed: the value the field holds under "Mixed")
    const std::optional<T>& getValue() const { return m_value; }

private:
    std::optional<T> m_value;
    bool m_isMixed = false;
};

/// Helpers of the cards of package P13 (pdfappeditcontentcards*.cpp and the test of the cards; no
/// other package calls them). Threads: GUI thread (the functions read the context).
///
/// What a card reads and what its command changes are two lists (R7-1, R7-4):
///  - a command takes the nodes of getCommandNodes - the whole changeable selection of the context
///    (PDFAppEditContentContext::getChangeableSelection: a locked object is left alone, every
///    other selected object is changed, never a part of the selection);
///  - the values a card shows are read from getInspectedNodes - the same nodes, at most
///    MAXIMUM_INSPECTED_NODES of them (or the selected nodes read only, when none may be changed).
///
/// Bounds (hostile input): a card reads at most MAXIMUM_INSPECTED_NODES nodes and visits at most
/// MAXIMUM_INSPECTED_NODES nodes below them (a walk over a tree, one visit per node, not deeper
/// than PDFContentContainer::MAXIMUM_DEPTH + 2 levels); what is beyond counts as "mixed" for the
/// values (isTruncated), so the values of a selection of 100 000 objects cost a bounded time per
/// update. A command walks the whole changeable selection once (one visited set, the same depth
/// limit; bounded by the nodes of the container).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCardTools
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentCardTools)

public:
    PDFAppEditContentCardTools() = delete;

    static constexpr size_t MAXIMUM_INSPECTED_NODES = 10000;

    /// Width of the label of a field row (the rows "Line spacing", "Fill opacity" of the frames:
    /// the label up to 151 px, the field at the trailing edge)
    static constexpr int FIELD_WIDTH = 100;

    /// A selected node, which exists in the container of the context
    struct SelectedNode
    {
        PDFContentNodeReference reference;
        PDFContentNodePointer node;
    };

    /// The selected nodes, which exist in the edited container, in the order of the selection; at
    /// most MAXIMUM_INSPECTED_NODES (\p isTruncated: there were more). The card Selection names
    /// them (locked ones too); no command takes its nodes from here.
    static std::vector<SelectedNode> getSelectedNodes(const PDFAppEditContentContext* context, bool* isTruncated = nullptr);

    /// The nodes, whose values a card shows: the nodes of getCommandNodes (in the order of the
    /// selection), or - when no selected node may be changed (only locked objects are selected) -
    /// the selected nodes of getSelectedNodes, which the card shows read only (\p isChangeable
    /// false). At most MAXIMUM_INSPECTED_NODES (\p isTruncated: there were more).
    static std::vector<SelectedNode> getInspectedNodes(const PDFAppEditContentContext* context, bool* isTruncated = nullptr, bool* isChangeable = nullptr);

    /// The nodes a command of a card acts on: the whole changeable selection of the context
    /// (PDFAppEditContentContext::getChangeableSelection), with the node pointers of the edited
    /// container; the order of the selection (the key object, when it may be changed, is in it)
    static std::vector<SelectedNode> getCommandNodes(const PDFAppEditContentContext* context);

    /// The references of the nodes
    static std::vector<PDFContentNodeReference> getReferences(const std::vector<SelectedNode>& nodes);

    /// The leaves of the selected nodes: a selected leaf itself, the leaves below a selected Group,
    /// MarkedContent or TextObject (each once, in the order of the content), only of the kinds
    /// \p isKind accepts; at most \p maximumVisits visits (\p isTruncated)
    static std::vector<PDFContentNodePointer> getLeaves(const PDFContentContainer& container,
                                                        const std::vector<SelectedNode>& nodes,
                                                        bool (*isKind)(const PDFContentNode& node),
                                                        bool* isTruncated,
                                                        size_t maximumVisits = MAXIMUM_INSPECTED_NODES);

    /// The objects, whose appearance a command of the core sets one by one (R-1: a Path, which is
    /// no clipping path only, and a TextObject as a whole - a part of a text cannot be restyled):
    /// the nodes themselves or the ones below a selected Group / MarkedContent, each once, in the
    /// order of the content; the whole list (a command acts on it), bounded by the container
    static std::vector<SelectedNode> getAppearanceObjects(const PDFContentContainer& container, const std::vector<SelectedNode>& nodes);

    /// A leaf, which can paint a fill or a stroke (a Path, which is no clipping path only - also a
    /// path, whose painting is switched off, "n" without a clip; a TextShow)
    static bool isPaintedLeaf(const PDFContentNode& node);

    /// A leaf, which draws something (Path, TextShow, Image, FormUse, Shading)
    static bool isDrawingLeaf(const PDFContentNode& node);

    /// The context of the display queries of the core (the document of the session)
    static PDFContentDisplayContext getDisplayContext(const PDFAppEditContentContext* context);

    /// The translated name of the kind of the node: the kind of its row of the tab Objects
    /// (PDFAppEditContentObjectsModel::getKindText, R7-10: one source of truth), "Page content" for
    /// the root, "Text" for a show, "Graphics state" for a state run; \p document may be nullptr
    /// (a form use is "Form XObject" then, never "Transparency group"). The view of the canvas
    /// announces the selection with it too.
    static QString getKindTitle(const PDFContentNode& node, const PDFDocument* document);

    /// The cards of the selection are shown: Preview is off and the card Keyboard is not requested
    /// (the frames -preview, -keyboard)
    static bool isSelectionStateShown(const PDFAppEditContentContext* context);

    /// The box of the shown page (its crop box, the paper of the canvas) in the default space of the
    /// page; empty without a page
    static QRectF getPageBox(const PDFAppEditContentContext* context);

    /// The matrix of the default space of the shown page to the space the user sees (R5-6:
    /// PDFContentArrange::getDisplayedPageMatrix of the /Rotate of the page; the identity without a
    /// page). Left, right, up and down of the cards (X, Y, W, H, Rotation, the flips, Arrange) are
    /// the directions on the screen.
    static QTransform getDisplayedPageMatrix(const PDFAppEditContentContext* context);

    /// The matrix of the default space of the edited container to the space the user sees:
    /// PDFAppEditContentGeometryHelper::getContainerToPageMatrix x getDisplayedPageMatrix; no value
    /// without a loaded container
    static std::optional<QTransform> getContainerToDisplayed(const PDFAppEditContentContext* context);

    /// The matrix of the leaf to the space of its container, as it draws its content: the matrix of
    /// its display state (the override and the CTM), for a TextShow also its text matrix Tm before
    /// the first glyph (ISO 32000-2, 9.4.2: a rotated text is mostly rotated by Tm, R7-5); no value
    /// for a node without a display state
    static std::optional<QTransform> getLeafMatrix(const PDFContentContainer& container, const PDFContentNode& leaf, const PDFContentDisplayContext& displayContext);

    /// The number of points of one unit of the option PDFAppEditContentOptions::UNITS (pt 1, mm
    /// 72 / 25.4, in 72) and its translated suffix ("pt", "mm", "in")
    static double getUnitFactor(const PDFAppEditContentContext* context);
    static QString getUnitSuffix(const PDFAppEditContentContext* context);

    /// A real number in the locale of the user interface (PDFAppLocale) with at most \p decimals
    /// decimals, without trailing zeros of the fraction ("63.4", "100")
    static QString formatReal(double value, int decimals);

    /// A label of a card (PDFLabel of the role and the color)
    static PDFLabel* createLabel(const QString& text, PDFThemeFonts::Role role, PDFThemeColors::Color color, QWidget* parent);

    /// A note of a card: a wrapped text Regular 11, text/tertiary
    static PDFLabel* createNote(const QString& text, QWidget* parent);

    /// A row "label ... field" of a card (the field at the trailing edge, FIELD_WIDTH wide unless
    /// \p fieldWidth says otherwise)
    static QWidget* createFieldRow(const QString& label, QWidget* field, QWidget* parent, int fieldWidth = FIELD_WIDTH);

    /// A widget of a column of widgets, 8 apart (the content of a section)
    static QWidget* createColumn(QWidget* parent, QVBoxLayout** layout);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTCARDS_H
