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

// Edit Content, stage E0, package P03 (session, undo, basic commands): the undo stack. It stores
// the executed commands and the position only; applying and reverting is done by the session.

#include "pdfcontentundostack.h"

#include <memory>
#include <optional>
#include <utility>

namespace pdf
{

/// The state of the undo stack.
///
/// Ownership: owned by its stack and destroyed with it; it co-owns the commands of its steps and
/// of the open macro (shared pointers), a command dropped from it is destroyed, unless somebody
/// else holds it.
class PDFContentUndoStack::Impl
{
public:
    Impl() = default;

    /// Drops the steps, which were undone. The clean point is lost, when it was among them.
    void dropUndoneSteps()
    {
        if (position < steps.size())
        {
            // What only the dropped commands name is remembered for the session (plain values:
            // the commands themselves are destroyed here, as before)
            for (size_t i = position; i < steps.size(); ++i)
            {
                for (const PDFContentCommandPointer& command : steps[i].commands)
                {
                    const std::vector<PDFObjectReference> objects = command->getNewObjects();
                    droppedNewObjects.insert(droppedNewObjects.end(), objects.cbegin(), objects.cend());
                }
            }

            steps.erase(std::next(steps.begin(), position), steps.end());

            if (cleanPosition && *cleanPosition > position)
            {
                cleanPosition = std::nullopt;
            }
        }
    }

    /// Adds a step at the position (the undone steps are dropped already) and keeps the limit
    void addStep(Step step)
    {
        steps.push_back(std::move(step));
        ++position;

        while (steps.size() > MAXIMUM_COMMAND_COUNT)
        {
            // The oldest step is dropped: the state before it cannot be reached any more
            steps.erase(steps.begin());
            --position;

            if (cleanPosition)
            {
                if (*cleanPosition == 0)
                {
                    cleanPosition = std::nullopt;
                }
                else
                {
                    --*cleanPosition;
                }
            }
        }
    }

    std::vector<Step> steps;
    size_t position = 0;                        ///< Steps before it are done
    std::optional<size_t> cleanPosition = 0;    ///< No value: the clean state cannot be reached any more
    int macroDepth = 0;                         ///< Number of the open macros
    Step macro;                                 ///< The commands of the outermost open macro
    std::vector<PDFObjectReference> droppedNewObjects;  ///< The new objects of the undone commands dropped since takeDroppedNewObjects
};

PDFContentUndoStack::PDFContentUndoStack() :
    m_impl(std::make_unique<Impl>())
{

}

PDFContentUndoStack::~PDFContentUndoStack() = default;

void PDFContentUndoStack::push(PDFContentCommandPointer command)
{
    if (!command)
    {
        return;
    }

    // The command changed the model after the current position: the undone steps can never be
    // applied again
    m_impl->dropUndoneSteps();

    if (m_impl->macroDepth > 0)
    {
        m_impl->macro.commands.push_back(std::move(command));
        return;
    }

    // Merging: only into a single command (a macro is a closed gesture), and never at the clean
    // point (the merged command would change the state the clean point stands for)
    const bool isAtCleanPoint = m_impl->cleanPosition && *m_impl->cleanPosition == m_impl->position;
    if (!m_impl->steps.empty() && !isAtCleanPoint)
    {
        Step& lastStep = m_impl->steps.back();
        if (lastStep.commands.size() == 1 && lastStep.commands.front()->mergeWith(*command))
        {
            lastStep.text = lastStep.commands.front()->getText();
            return;
        }
    }

    Step step;
    step.text = command->getText();
    step.commands.push_back(std::move(command));
    m_impl->addStep(std::move(step));
}

void PDFContentUndoStack::beginMacro(const QString& text)
{
    if (m_impl->macroDepth == 0)
    {
        m_impl->macro = Step();
        m_impl->macro.text = text;
    }

    ++m_impl->macroDepth;
}

void PDFContentUndoStack::endMacro()
{
    if (m_impl->macroDepth == 0)
    {
        return;
    }

    --m_impl->macroDepth;

    if (m_impl->macroDepth == 0)
    {
        Step macro = std::move(m_impl->macro);
        m_impl->macro = Step();

        if (!macro.commands.empty())
        {
            m_impl->addStep(std::move(macro));
        }
    }
}

bool PDFContentUndoStack::isMacroOpen() const
{
    return m_impl->macroDepth > 0;
}

bool PDFContentUndoStack::canUndo() const
{
    return m_impl->macroDepth == 0 && m_impl->position > 0;
}

bool PDFContentUndoStack::canRedo() const
{
    return m_impl->macroDepth == 0 && m_impl->position < m_impl->steps.size();
}

QString PDFContentUndoStack::getUndoText() const
{
    const Step* step = getUndoStep();
    return step ? step->text : QString();
}

QString PDFContentUndoStack::getRedoText() const
{
    const Step* step = getRedoStep();
    return step ? step->text : QString();
}

const PDFContentUndoStack::Step* PDFContentUndoStack::getUndoStep() const
{
    return canUndo() ? &m_impl->steps[m_impl->position - 1] : nullptr;
}

const PDFContentUndoStack::Step* PDFContentUndoStack::getRedoStep() const
{
    return canRedo() ? &m_impl->steps[m_impl->position] : nullptr;
}

void PDFContentUndoStack::moveToPreviousStep()
{
    if (canUndo())
    {
        --m_impl->position;
    }
}

void PDFContentUndoStack::moveToNextStep()
{
    if (canRedo())
    {
        ++m_impl->position;
    }
}

size_t PDFContentUndoStack::getCount() const
{
    return m_impl->steps.size();
}

size_t PDFContentUndoStack::getPosition() const
{
    return m_impl->position;
}

void PDFContentUndoStack::setClean()
{
    m_impl->cleanPosition = m_impl->position;
}

bool PDFContentUndoStack::isClean() const
{
    // The commands of an open macro are work, which is not a step yet
    return m_impl->cleanPosition && *m_impl->cleanPosition == m_impl->position && m_impl->macro.commands.empty();
}

void PDFContentUndoStack::clear()
{
    m_impl = std::make_unique<Impl>();
}

size_t PDFContentUndoStack::getStoredItemCount() const
{
    size_t count = 0;

    // The commands of an open macro are kept too
    for (const PDFContentCommandPointer& command : m_impl->macro.commands)
    {
        count += command->getStoredItemCount();
    }

    for (const Step& step : m_impl->steps)
    {
        for (const PDFContentCommandPointer& command : step.commands)
        {
            count += command->getStoredItemCount();
        }
    }

    return count;
}

std::vector<PDFObjectReference> PDFContentUndoStack::takeDroppedNewObjects()
{
    std::vector<PDFObjectReference> result = std::move(m_impl->droppedNewObjects);
    m_impl->droppedNewObjects.clear();
    return result;
}

}   // namespace pdf
