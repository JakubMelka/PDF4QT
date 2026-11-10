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

#ifndef APPMCPPAGES_H
#define APPMCPPAGES_H

// The MCP actions of the tools of step 3a of phase 2 over HTTP (UnitTestsAppMcpPages, plan 7, 8):
// the schema, save_files, existingFiles, documentIds 2 to 2, imagePaths, the permissions, the
// cancellation. Each package registers the cases of its actions in its own file
// (appmcppages_<package>.cpp) by its registration function; the test runs every case as a row of
// the function "action" with its own fixture (AppMcpFixture: the window, the controller, the
// host and the client), so a case is run alone by "UnitTestsAppMcpPages action:<name>".

#include "appmcpfixture.h"

#include <QString>

#include <functional>
#include <vector>

/// A case of an MCP action: QVERIFY / QCOMPARE inside of \p run end the case (the row fails)
class AppMcpPagesCase
{
public:
    QString name;                                       ///< Name of the row ("split-save-files"), unique
    std::function<void(pdftest::AppMcpFixture&)> run;   ///< The case over a new fixture
};

/// The cases of the packages of step 3a: each registration function is defined in the file of its
/// package and appends its cases
class AppMcpPagesPackages
{
public:
    AppMcpPagesPackages() = delete;

    /// Every registered case in the order of the packages A, B, C, D
    static const std::vector<AppMcpPagesCase>& getCases();

    /// Package A (pages.split, pages.extract; save_files of every tool): appmcppages_split.cpp
    static void registerSplitCases(std::vector<AppMcpPagesCase>* cases);

    /// Package B (pages.crop, pages.resize, pages.boxes): appmcppages_geometry.cpp
    static void registerGeometryCases(std::vector<AppMcpPagesCase>* cases);

    /// Package C (pages.insert, pages.add-image): appmcppages_insert.cpp
    static void registerInsertCases(std::vector<AppMcpPagesCase>* cases);

    /// Package D (convert.images-to-pdf, pages.interleave): appmcppages_images.cpp
    static void registerImageCases(std::vector<AppMcpPagesCase>* cases);
};

#endif // APPMCPPAGES_H
