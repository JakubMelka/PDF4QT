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

#ifndef PDFAPPTOOLPARAMETERMODEL_H
#define PDFAPPTOOLPARAMETERMODEL_H

#include "pdfapptoolundotarget.h"
#include "pdfappvaluestore.h"
#include "pdfdatabase.h"
#include "pdfsettingdefinition.h"

#include "pdfdocumenttoolparameter.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;

/// The only model of the values of the parameters of an open tool (plan of the first
/// batch, chapter 2.1): the widgets of the panels, the binder PDFAppSettingControl and
/// the runner all use it.
///  - The values are canonical (PDFDocumentToolParameters): an invalid value of a
///    widget is refused with its translated reason and nothing changes.
///  - The start values are the last values of the tool (PDFToolParameterRepository):
///    a hostile stored value gives the default. A Secret and a parameter with
///    setStored(false) are never read nor written.
///  - Undo and Redo are snapshots of all values; each accepted change is one step and
///    Undo is disabled until the first change (K6). They are what Undo and Redo of the
///    tool act on while the tool is open, never the document (owner, 2026-11-01).
///  - The last values are written by \p storeLastValues, when the result of the tool
///    is accepted - not by a change, which is canceled later.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolParameterModel : public PDFAppToolUndoTarget
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolUndoTarget;

public:
    /// Most undo steps
    static constexpr int MAXIMUM_HISTORY = 100;

    /// \param context Context of the application (the repository of the last values), may be null
    /// \param toolId Id of the tool of the catalog
    /// \param definitions Definitions of the parameters of the tool
    /// \param parent Parent object
    explicit PDFAppToolParameterModel(PDFAppContext* context, QString toolId, PDFDocumentToolParameterDefinitions definitions, QObject* parent);
    virtual ~PDFAppToolParameterModel() override;

    const QString& getToolId() const { return m_toolId; }
    const PDFDocumentToolParameterDefinitions& getDefinitions() const { return m_definitions; }

    /// The definitions of the tool asked again after a change of the language: only
    /// their translated texts change, the values stay
    void updateDefinitionTexts(PDFDocumentToolParameterDefinitions definitions);

    /// Canonical values of all parameters (a required parameter without a value is
    /// left out); what the runner gets
    const QVariantMap& getValues() const { return m_values; }

    /// Canonical value of the parameter, invalid for an unknown name or a missing value
    QVariant getValue(const QString& name) const { return m_values.value(name); }

    /// Sets a value of the user. A value equal to the current one does nothing.
    /// \returns the translated reason of a refusal, empty on success
    QString setValue(const QString& name, const QVariant& value);

    /// Sets more values as one undo step; one invalid value refuses all of them.
    /// \returns the translated reason of a refusal, empty on success
    QString setValues(const QVariantMap& values);

    /// Sets a value, which follows the view, not the user (the pages of the modes Current
    /// and Selected of a page selection): no undo step (K6: Undo waits for the first change
    /// of the user), the steps keep it. \returns the translated reason of a refusal.
    QString setFollowingValue(const QString& name, const QVariant& value);

    /// Errors of the current values (required parameters without a value)
    std::vector<PDFDocumentToolParameterError> getErrors() const;

    virtual bool canUndo() const override { return !m_undoStack.empty(); }
    virtual bool canRedo() const override { return !m_redoStack.empty(); }
    virtual void undo() override;
    virtual void redo() override;

    /// Stores the current values as the last values of the tool (the result has been
    /// accepted). A Secret and a parameter with setStored(false) are left out.
    PDFDatabaseError storeLastValues();

    /// Definition of the binder of a parameter (Enum: Option, Integer, Real, Bool,
    /// Color: a text with the color control, a path or a text: a text). The key is
    /// "tool.<name>"; std::nullopt for a type the binder has no control of (page
    /// selection, a Secret) and for a name the settings do not accept.
    std::optional<PDFSettingDefinition> toSettingDefinition(const QString& name) const;

    /// Store of the binder: PDFAppSettingControl writes into this model. The store may
    /// outlive the model (a binder deleted later), then it refuses nothing and does nothing.
    std::shared_ptr<PDFAppValueStore> createValueStore();

Q_SIGNALS:
    /// A value has changed (once per a changed parameter; also by Undo and Redo)
    void changed(const QString& name);

private:
    /// Replaces the values, emits \p changed for what differs and \p undoStateChanged
    void applyValues(const QVariantMap& values);

    void loadLastValues();

    QPointer<PDFAppContext> m_context;
    QString m_toolId;
    PDFDocumentToolParameterDefinitions m_definitions;
    QVariantMap m_values;
    std::vector<QVariantMap> m_undoStack;
    std::vector<QVariantMap> m_redoStack;
};

}   // namespace pdf

#endif // PDFAPPTOOLPARAMETERMODEL_H
