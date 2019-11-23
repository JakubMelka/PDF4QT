//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFACTION_H
#define PDFACTION_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdffile.h"
#include "pdfmultimedia.h"

#include <QSharedPointer>

#include <set>
#include <variant>
#include <functional>

namespace pdf
{
class PDFAction;
class PDFDocument;

enum class ActionType
{
    GoTo,
    GoToR,
    GoToE,
    Launch,
    Thread,
    URI,
    Sound,
    Movie,
    Hide,
    Named,
    SetOCGState
};

enum class DestinationType
{
    Invalid,
    Named,
    XYZ,
    Fit,
    FitH,
    FitV,
    FitR,
    FitB,
    FitBH,
    FitBV
};

/// Destination to the specific location of the document. Destination can also be 'Named' type,
/// in this case, destination in name tree is found and used.
class PDFDestination
{
public:
    explicit inline PDFDestination() = default;

    DestinationType getDestinationType() const { return m_destinationType; }
    PDFReal getLeft() const { return m_left; }
    PDFReal getTop() const { return m_top; }
    PDFReal getRight() const { return m_right; }
    PDFReal getBottom() const { return m_bottom; }
    PDFReal getZoom() const { return m_zoom; }
    const QByteArray& getName() const { return m_name; }
    PDFObjectReference getPageReference() const { return m_pageReference; }
    PDFInteger getPageIndex() const { return m_pageIndex; }

    /// Parses the destination from the object. If object contains invalid destination,
    /// then exception is thrown. If object is empty, empty destination is returned.
    /// \param document Document
    /// \param object Destination object
    static PDFDestination parse(const PDFDocument* document, PDFObject object);

private:
    DestinationType m_destinationType = DestinationType::Invalid;
    PDFReal m_left = 0.0;
    PDFReal m_top = 0.0;
    PDFReal m_right = 0.0;
    PDFReal m_bottom = 0.0;
    PDFReal m_zoom = 0.0;
    QByteArray m_name;
    PDFObjectReference m_pageReference;
    PDFInteger m_pageIndex = 0;
};

using PDFActionPtr = QSharedPointer<PDFAction>;

/// Base class for action types.
class PDFAction
{
public:
    explicit PDFAction() = default;
    virtual ~PDFAction() = default;

    /// Returns type of the action.
    virtual ActionType getType() const = 0;

    /// Returns container with next actions
    const std::vector<PDFActionPtr>& getNextActions() const { return m_nextActions; }

    /// Tries to parse the action. If serious error occurs, then exception is thrown.
    /// If \p object is null object, then nullptr is returned.
    /// \param document Document
    /// \param object Object containing the action
    static PDFActionPtr parse(const PDFDocument* document, PDFObject object);

    /// Calls the lambda function with action as parameter, then following
    /// the 'Next' entry, as described by PDF 1.7 specification.
    void apply(const std::function<void(const PDFAction* action)>& callback);

private:
    static PDFActionPtr parseImpl(const PDFDocument* document, PDFObject object, std::set<PDFObjectReference>& usedReferences);

    std::vector<PDFActionPtr> m_nextActions;
};

class PDFActionGoTo : public PDFAction
{
public:
    explicit inline PDFActionGoTo(PDFDestination destination) : m_destination(qMove(destination)) { }

    virtual ActionType getType() const override { return ActionType::GoTo; }

    const PDFDestination& getDestination() const { return m_destination; }

private:
    PDFDestination m_destination;
};

class PDFActionGoToR : public PDFAction
{
public:
    explicit inline PDFActionGoToR(PDFDestination destination, PDFFileSpecification fileSpecification, bool newWindow) :
        m_destination(qMove(destination)),
        m_fileSpecification(qMove(fileSpecification)),
        m_newWindow(newWindow)
    {

    }

    virtual ActionType getType() const override { return ActionType::GoToR; }

    const PDFDestination& getDestination() const { return m_destination; }
    const PDFFileSpecification& getFileSpecification() const { return m_fileSpecification; }
    bool isNewWindow() const { return m_newWindow; }

private:
    PDFDestination m_destination;
    PDFFileSpecification m_fileSpecification;
    bool m_newWindow = false;
};

class PDFActionGoToE : public PDFAction
{
public:
    explicit inline PDFActionGoToE(PDFDestination destination, PDFFileSpecification fileSpecification, bool newWindow, const PDFObject& target) :
        m_destination(qMove(destination)),
        m_fileSpecification(qMove(fileSpecification)),
        m_newWindow(newWindow),
        m_target(target)
    {

    }

    virtual ActionType getType() const override { return ActionType::GoToE; }

    const PDFDestination& getDestination() const { return m_destination; }
    const PDFFileSpecification& getFileSpecification() const { return m_fileSpecification; }
    bool isNewWindow() const { return m_newWindow; }
    const PDFObject& getTarget() const { return m_target; }

private:
    PDFDestination m_destination;
    PDFFileSpecification m_fileSpecification;
    bool m_newWindow = false;
    PDFObject m_target;
};

class PDFActionLaunch : public PDFAction
{
public:

    /// Specification of launched application (if not file specification is attached)
    struct Win
    {
        QByteArray file;
        QByteArray directory;
        QByteArray operation;
        QByteArray parameters;
    };

    explicit inline PDFActionLaunch(PDFFileSpecification fileSpecification, bool newWindow, Win win) :
        m_fileSpecification(qMove(fileSpecification)),
        m_newWindow(newWindow),
        m_win(qMove(win))
    {

    }

    virtual ActionType getType() const override { return ActionType::Launch; }

    const PDFFileSpecification& getFileSpecification() const { return m_fileSpecification; }
    const Win& getWinSpecification() const { return m_win; }
    bool isNewWindow() const { return m_newWindow; }

private:
    PDFFileSpecification m_fileSpecification;
    bool m_newWindow = false;
    Win m_win;
};

class PDFActionThread : public PDFAction
{
public:
    using Thread = std::variant<typename std::monostate, PDFObjectReference, PDFInteger, QString>;
    using Bead = std::variant<typename std::monostate, PDFObjectReference, PDFInteger>;

    explicit inline PDFActionThread(PDFFileSpecification fileSpecification, Thread thread, Bead bead) :
        m_fileSpecification(qMove(fileSpecification)),
        m_thread(qMove(thread)),
        m_bead(qMove(bead))
    {

    }

    virtual ActionType getType() const override { return ActionType::Thread; }

    const PDFFileSpecification& getFileSpecification() const { return m_fileSpecification; }
    const Thread& getThread() const { return m_thread; }
    const Bead& getBead() const { return m_bead; }

private:
    PDFFileSpecification m_fileSpecification;
    Thread m_thread;
    Bead m_bead;
};

class PDFActionURI : public PDFAction
{
public:
    explicit inline PDFActionURI(QByteArray URI, bool isMap) :
        m_URI(qMove(URI)),
        m_isMap(isMap)
    {

    }

    virtual ActionType getType() const override { return ActionType::URI; }

    const QByteArray& getURI() const { return m_URI; }
    bool isMap() const { return m_isMap; }

private:
    QByteArray m_URI;
    bool m_isMap;
};

class PDFActionSound : public PDFAction
{
public:
    explicit inline PDFActionSound(PDFSound sound, PDFReal volume, bool isSynchronous, bool isRepeat, bool isMix) :
        m_sound(qMove(sound)),
        m_volume(qMove(volume)),
        m_isSynchronous(isSynchronous),
        m_isRepeat(isRepeat),
        m_isMix(isMix)
    {

    }

    virtual ActionType getType() const override { return ActionType::Sound; }

    const PDFSound* getSound() const { return &m_sound; }
    PDFReal getVolume() const { return m_volume; }
    bool isSynchronous() const { return m_isSynchronous; }
    bool isRepeat() const { return m_isRepeat; }
    bool isMix() const { return m_isMix; }

private:
    PDFSound m_sound;
    PDFReal m_volume;
    bool m_isSynchronous;
    bool m_isRepeat;
    bool m_isMix;
};

class PDFActionMovie : public PDFAction
{
public:
    enum class Operation
    {
        Play,
        Stop,
        Pause,
        Resume
    };

    explicit inline PDFActionMovie(PDFObjectReference annotation, QString title, Operation operation) :
        m_annotation(qMove(annotation)),
        m_title(qMove(title)),
        m_operation(operation)
    {

    }

    virtual ActionType getType() const override { return ActionType::Movie; }

    PDFObjectReference getAnnotation() const { return m_annotation; }
    const QString& getTitle() const { return m_title; }
    Operation getOperation() const { return m_operation; }

private:
    PDFObjectReference m_annotation;
    QString m_title;
    Operation m_operation;
};

class PDFActionHide : public PDFAction
{
public:
    explicit inline PDFActionHide(std::vector<PDFObjectReference>&& annotations, std::vector<QString>&& fieldNames, bool hide) :
        m_annotations(qMove(annotations)),
        m_fieldNames(qMove(fieldNames)),
        m_hide(hide)
    {

    }

    virtual ActionType getType() const override { return ActionType::Hide; }

    const std::vector<PDFObjectReference>& getAnnotations() const { return m_annotations; }
    const std::vector<QString>& getFieldNames() const { return m_fieldNames; }
    bool isHide() const { return m_hide; }

private:
    std::vector<PDFObjectReference> m_annotations;
    std::vector<QString> m_fieldNames;
    bool m_hide;
};

class PDFActionNamed : public PDFAction
{
public:
    enum class NamedActionType
    {
        Custom,
        NextPage,
        PrevPage,
        FirstPage,
        LastPage
    };

    explicit inline PDFActionNamed(NamedActionType namedActionType, QByteArray&& customNamedAction) :
        m_namedActionType(namedActionType),
        m_customNamedAction(qMove(customNamedAction))
    {

    }

    virtual ActionType getType() const override { return ActionType::Named; }

    NamedActionType getNamedActionType() const { return m_namedActionType; }
    const QByteArray& getCustomNamedAction() const { return m_customNamedAction; }

private:
    NamedActionType m_namedActionType;
    QByteArray m_customNamedAction;
};

class PDFActionSetOCGState : public PDFAction
{
public:

    enum class SwitchType
    {
        ON,
        OFF,
        Toggle
    };

    using StateChangeItem = std::pair<SwitchType, PDFObjectReference>;
    using StateChangeItems = std::vector<StateChangeItem>;

    explicit inline PDFActionSetOCGState(StateChangeItems&& stateChangeItems, bool isRadioButtonsPreserved) :
        m_items(qMove(stateChangeItems)),
        m_isRadioButtonsPreserved(isRadioButtonsPreserved)
    {

    }

    virtual ActionType getType() const override { return ActionType::SetOCGState; }

    const StateChangeItems& getStateChangeItems() const { return m_items; }
    bool isRadioButtonsPreserved() const { return m_isRadioButtonsPreserved; }

private:
    StateChangeItems m_items;
    bool m_isRadioButtonsPreserved;
};

}   // namespace pdf

#endif // PDFACTION_H
