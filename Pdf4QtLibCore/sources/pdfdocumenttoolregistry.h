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

#ifndef PDFDOCUMENTTOOLREGISTRY_H
#define PDFDOCUMENTTOOLREGISTRY_H

#include "pdfglobal.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <memory>

namespace pdf
{

class PDFDocumentTool;

/// Result of a registration of a tool
enum class PDFDocumentToolRegistration
{
    Registered,
    NullTool,           ///< No tool
    UnknownTool,        ///< The id is not a tool of PDFDocumentToolCatalog
    Duplicate,          ///< A tool of the id is registered already
    InvalidParameters   ///< The definitions of the parameters are not valid (PDFDocumentToolParameters::isValid)
};

/// A listener of the changes of the registry (PDFDocumentToolRegistry::addListener): MCP builds
/// its tool catalog again when the registry changes, without asking it in intervals
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRegistryListener
{
public:
    virtual ~PDFDocumentToolRegistryListener();

    /// The registry has changed (its generation has grown). Called in the thread, which has
    /// changed the registry (any thread), under the lock of the listeners, after the change:
    /// the listener only hands the news over (a queued invocation into its own thread), never
    /// calls the registry nor adds or removes a listener here.
    virtual void onToolRegistryChanged() = 0;
};

/// Process wide registry of the implementations of the tools of the catalog
/// (newapp_tools.md 2, Katalog a registr): a tool of the catalog without an
/// implementation is shown in the catalog, its page is Page Not Found and MCP
/// does not offer it. Only ids of PDFDocumentToolCatalog are accepted, one
/// implementation per id. Every function is thread safe (a mutex); a tool got
/// from the registry stays alive while it is held, also when it is unregistered
/// meanwhile. The applications register their tools at the start; nothing is
/// registered in phase 1 (step 6) - the tests register the test tool.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRegistry
{
public:
    PDFDocumentToolRegistry() = delete;

    /// Registers the tool under its id (PDFDocumentTool::getId)
    static PDFDocumentToolRegistration registerTool(std::shared_ptr<const PDFDocumentTool> tool);

    /// Removes the tool of the id; false = none was registered
    static bool unregisterTool(QStringView id);

    /// Tool of the id, nullptr = no implementation (also for a hostile id)
    static std::shared_ptr<const PDFDocumentTool> getTool(QStringView id);

    /// The tool of the id has an implementation
    static bool isImplemented(QStringView id);

    /// Ids of the implemented tools in the order of the catalog
    static QStringList getImplementedTools();

    /// Removes every tool (tests only)
    static void clearForTests();

    /// Generation of the registry: a counter, which grows with every successful
    /// registration and removal (MCP builds its tool catalog again when it changes,
    /// plan of step 7, 3.10). Thread safe; starts at 0.
    static quint64 getGeneration();

    /// Adds a listener of the changes (thread safe; not owned). Every successful registration,
    /// removal and clearForTests calls PDFDocumentToolRegistryListener::onToolRegistryChanged.
    static void addListener(PDFDocumentToolRegistryListener* listener);

    /// Removes the listener (thread safe): after the return it is never called again, also from
    /// a change of another thread - a dying listener removes itself first
    static void removeListener(PDFDocumentToolRegistryListener* listener);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLREGISTRY_H
