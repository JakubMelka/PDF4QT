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

#ifndef PDFAPPTOOLGROUPEDPAGEMODEL_H
#define PDFAPPTOOLGROUPEDPAGEMODEL_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanels_thumbnails.h"

#include "pdfaccenthue.h"

#include <QString>

#include <vector>

namespace pdf
{

/// The pages of a grouped view (plan of step 3 of phase 2, 4.3, R1 b): the rows are pages (the
/// pictures of PDFAppThumbnailsModel), consecutive rows form the groups, which the view draws as
/// cards with their header - Merge (the runs of the inputs, 378:1242), Split (the files to
/// create, 383:3315), Insert Pages (the cards of the sources, 388:15487) and Manage Pages.
/// A group = {title, subtitle, Tag, dot, rows}.
///
/// Contract (a fixed interface of the skeleton of step 3a; only the coordinator changes it):
///  - The view (PDFAppToolMergePageView, package A) reads the groups only through this class;
///    a model of a tool implements it (Merge: PDFAppToolMergePageModel without a change of
///    behavior; Split: package A; the sources of Insert: package C through the view of A).
///  - The groups cover the rows in their order; a group may have no row (a pending input of
///    Merge, which shows its callout instead).
///  - groupsChanged is emitted after every change of the groups (their rows, texts or
///    callouts), last; a slot may delete the view. Ownership: the model is a child of the canvas
///    of the tool, the view holds it by a QPointer. Thread: GUI only.
///  - The marks of the tiles are the roles of PDFAppToolPageTileDelegate (HueRole, DimmedRole,
///    SlotScaleRole as Group::tileScale of the group of the row) and SubtitleRole (the caption,
///    Group::hasTileCaptions).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGroupedPageModel : public PDFAppThumbnailsModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppThumbnailsModel;

public:
    /// The callout of a group without rows
    enum class GroupCallout
    {
        None,       ///< The pages of the group
        Reading,    ///< callout-neutral 1390:34213: the reading of the file with its progress
        Locked      ///< callout-red 378:2613, 1390:34515: a password is needed
    };

    /// A button of a group below its callout (388:17789: Enter Password…, Remove File); the view
    /// says a click by actionTriggered of the view
    struct Action
    {
        QString id;                 ///< Id of the action ("password", "remove")
        QString text;               ///< "Enter Password…"
        QString iconName;           ///< "key", empty = none

        bool operator==(const Action&) const = default;
    };

    /// A group of the canvas (group-A · Contract.pdf 378:1242)
    struct Group
    {
        QString inputId;            ///< Id of the source of the group (an input of the list, a file of Split)
        int inputIndex = -1;        ///< Index of the source (its letter and hue), -1 = none
        int firstRow = 0;           ///< First row of the group
        int rowCount = 0;           ///< Rows of the group (0 for a group without pages)
        PDFAccentHue hue = PDFAccentHue::Neutral;   ///< Hue of the dot of the group
        QString iconName;           ///< Icon of the header 14 x 14 instead of the dot ("file", "image" of Insert 388:17789), empty = the dot
        QString title;              ///< "A · Contract.pdf", "Handbook_1.pdf"
        QString subtitle;           ///< "Pages 1–6", "Pages unknown until unlocked"
        QString tag;                ///< "Image", "Repaired", "Left out", "Duplicate name", empty = none
        GroupCallout callout = GroupCallout::None;
        QString calloutTitle;       ///< "Password required", "Reading the file – 50 %"
        QString calloutText;
        double progress = -1.0;     ///< Progress of the reading (0 - 1), -1 = none
        std::vector<Action> actions;///< Buttons below the callout (388:17789), empty = none
        double tileScale = 1.0;     ///< The slot of the tiles of the group is the slot of the view scaled (the blank pages of Insert 388:16811)
        bool hasTileCaptions = false;   ///< The tiles have the line of a caption below the label ("front 1" 385:13263, "blank · A4 portrait" 388:16811)
        bool isPlain = false;       ///< Only the tiles on the stage, no card and no header (the blank pages alone, 388:16811)

        bool operator==(const Group&) const = default;
    };

    explicit PDFAppToolGroupedPageModel(QObject* parent);
    virtual ~PDFAppToolGroupedPageModel() override;

    /// The groups in their order
    virtual const std::vector<Group>& getGroups() const = 0;

    /// Index of the group of the row (-1 for an invalid row)
    virtual int getGroupOfRow(int row) const = 0;

    /// The row is drawn as selected (the selection of the views)
    virtual bool isRowSelected(int row) const = 0;

Q_SIGNALS:
    /// The groups have changed (their rows, texts or callouts): the canvas is laid out again
    void groupsChanged();
};

}   // namespace pdf

#endif // PDFAPPTOOLGROUPEDPAGEMODEL_H
