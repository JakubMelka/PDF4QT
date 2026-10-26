# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

add_executable(UnitTests
	tst_lexicalanalyzertest.cpp
)

target_link_libraries(UnitTests PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTests PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTests  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTests")

add_executable(UnitTestsImageScaling
	tst_imagescalingtest.cpp
)

target_link_libraries(UnitTestsImageScaling PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsImageScaling PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsImageScaling  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsImageScaling")

add_executable(UnitTestsImageOptimizer
	tst_imageoptimizertest.cpp
)

target_link_libraries(UnitTestsImageOptimizer PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsImageOptimizer PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsImageOptimizer  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsImageOptimizer")

add_executable(UnitTestsBitonalDocument
	tst_bitonaldocumenttest.cpp
)

target_link_libraries(UnitTestsBitonalDocument PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsBitonalDocument PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsBitonalDocument  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsBitonalDocument")

add_executable(UnitTestsJBIG2
	tst_jbig2test.cpp
)

target_link_libraries(UnitTestsJBIG2 PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsJBIG2 PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsJBIG2  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsJBIG2")

add_executable(UnitTestsJBIG2Files
	tst_jbig2filestest.cpp
)

target_link_libraries(UnitTestsJBIG2Files PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

# The directory with the JBIG2 test files is not a part of the repository, because the files
# are covered by a copyright of their authors. The test is skipped, when the directory does
# not exist, and the environment variable PDF4QT_JBIG2_TEST_DIRECTORY overrides the location.
target_compile_definitions(UnitTestsJBIG2Files PRIVATE PDF4QT_JBIG2_TEST_DIRECTORY="${CMAKE_SOURCE_DIR}/jbig2test")

set_target_properties(UnitTestsJBIG2Files PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsJBIG2Files  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsJBIG2Files")

add_executable(UnitTestsCCITTFax
	tst_ccittfaxtest.cpp
)

target_link_libraries(UnitTestsCCITTFax PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsCCITTFax PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsCCITTFax  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsCCITTFax")

add_executable(UnitTestsJBIG2Encoder
	tst_jbig2encodertest.cpp
)

target_link_libraries(UnitTestsJBIG2Encoder PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsJBIG2Encoder PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsJBIG2Encoder  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsJBIG2Encoder")

add_executable(UnitTestsJBIG2Segments
	tst_jbig2segmentstest.cpp
)

target_link_libraries(UnitTestsJBIG2Segments PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsJBIG2Segments PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsJBIG2Segments  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsJBIG2Segments")

add_executable(UnitTestsFontEncoding
	tst_fontencodingtest.cpp
)

target_link_libraries(UnitTestsFontEncoding PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsFontEncoding PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsFontEncoding  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsFontEncoding")

add_executable(UnitTestsAuthorSettings
	tst_authorsettingstest.cpp
)

target_link_libraries(UnitTestsAuthorSettings PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsAuthorSettings PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsAuthorSettings  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAuthorSettings")

add_executable(UnitTestsContentEditor
	tst_contenteditortest.cpp
)

target_link_libraries(UnitTestsContentEditor PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsContentEditor PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsContentEditor  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsContentEditor")

add_executable(UnitTestsMeasure
	tst_measuretest.cpp
)

target_link_libraries(UnitTestsMeasure PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsMeasure PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsMeasure  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsMeasure")

add_executable(UnitTestsPathBoolean
	tst_pathbooleantest.cpp
)

target_link_libraries(UnitTestsPathBoolean PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsPathBoolean PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsPathBoolean  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsPathBoolean")

add_executable(UnitTestsDimensions
	tst_dimensionstest.cpp
	../Pdf4QtEditorPlugins/DimensionsPlugin/dimensionsettings.cpp
	../Pdf4QtEditorPlugins/DimensionsPlugin/dimensionunits.cpp
)

target_include_directories(UnitTestsDimensions PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtEditorPlugins/DimensionsPlugin)

target_link_libraries(UnitTestsDimensions PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsDimensions PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsDimensions  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDimensions")

add_executable(UnitTestsFormFieldFormat
	tst_formfieldformattest.cpp
)

target_link_libraries(UnitTestsFormFieldFormat PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)

set_target_properties(UnitTestsFormFieldFormat PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsFormFieldFormat  "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsFormFieldFormat")

add_executable(UnitTestsTextLayout tst_textlayouttest.cpp)
target_link_libraries(UnitTestsTextLayout PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsTextLayout PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsTextLayout "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsTextLayout")

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsAnnotationTools tst_annotationtoolstest.cpp)
    target_link_libraries(UnitTestsAnnotationTools PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAnnotationTools PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAnnotationTools "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAnnotationTools")
    set_tests_properties(UnitTestsAnnotationTools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsPageContentEditorTools tst_pagecontenteditortoolstest.cpp)
    target_link_libraries(UnitTestsPageContentEditorTools PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsPageContentEditorTools PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsPageContentEditorTools "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsPageContentEditorTools")
    set_tests_properties(UnitTestsPageContentEditorTools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsDrawSpace tst_drawspacetest.cpp)
    target_link_libraries(UnitTestsDrawSpace PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsDrawSpace PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsDrawSpace "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDrawSpace")
    set_tests_properties(UnitTestsDrawSpace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(UnitTestsSignatureBuilder tst_signaturebuildertest.cpp)
target_link_libraries(UnitTestsSignatureBuilder PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsSignatureBuilder PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsSignatureBuilder "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsSignatureBuilder")

add_executable(UnitTestsTimestamp tst_timestamptest.cpp)
target_link_libraries(UnitTestsTimestamp PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Network Qt6::Test OpenSSL::Crypto)
set_target_properties(UnitTestsTimestamp PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsTimestamp "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsTimestamp")

add_executable(UnitTestsAnnotationManipulator tst_annotationmanipulatortest.cpp)
target_link_libraries(UnitTestsAnnotationManipulator PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsAnnotationManipulator PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsAnnotationManipulator "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAnnotationManipulator")
set_tests_properties(UnitTestsAnnotationManipulator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsAnnotationSelection tst_annotationselectiontest.cpp)
    target_link_libraries(UnitTestsAnnotationSelection PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAnnotationSelection PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAnnotationSelection "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAnnotationSelection")
    set_tests_properties(UnitTestsAnnotationSelection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(UnitTestsAnnotationIntegration tst_annotationintegrationtest.cpp)
    target_link_libraries(UnitTestsAnnotationIntegration PRIVATE Pdf4QtLibGui Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAnnotationIntegration PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAnnotationIntegration "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAnnotationIntegration")
    set_tests_properties(UnitTestsAnnotationIntegration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(UnitTestsFormFields tst_formfieldstest.cpp)
    target_link_libraries(UnitTestsFormFields PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsFormFields PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsFormFields "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsFormFields")
    set_tests_properties(UnitTestsFormFields PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(UnitTestsDocumentWriter tst_documentwritertest.cpp)
target_link_libraries(UnitTestsDocumentWriter PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsDocumentWriter PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsDocumentWriter "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDocumentWriter")

add_executable(UnitTestsPDFObject tst_pdfobjecttest.cpp)
target_link_libraries(UnitTestsPDFObject PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsPDFObject PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsPDFObject "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsPDFObject")

add_executable(UnitTestsScanPreparation tst_scanpreparationtest.cpp)
target_link_libraries(UnitTestsScanPreparation PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsScanPreparation PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsScanPreparation "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsScanPreparation")
set_tests_properties(UnitTestsScanPreparation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(UnitTestsOCR tst_ocrtest.cpp)
target_link_libraries(UnitTestsOCR PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
target_compile_definitions(UnitTestsOCR PRIVATE PDF4QT_OCR_SOURCE_DATA_DIRECTORY="${PDF4QT_OCR_BUILD_DATA_DIR}")
if(PDF4QT_ENABLE_OCR)
    target_link_libraries(UnitTestsOCR PRIVATE Pdf4QtOcrTesseract)
    target_compile_definitions(UnitTestsOCR PRIVATE PDF4QT_OCR_TESSERACT)
endif()
set_target_properties(UnitTestsOCR PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
if(PDF4QT_OCR_REQUIRED)
    target_compile_definitions(UnitTestsOCR PRIVATE PDF4QT_OCR_TESTS_REQUIRED)
endif()
add_test(UnitTestsOCR "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsOCR")
set_tests_properties(UnitTestsOCR PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PDF4QT_OCR_DATA_DIRECTORY=${PDF4QT_OCR_BUILD_DATA_DIR}")

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsOCRDialog tst_ocrdialogtest.cpp)
    target_link_libraries(UnitTestsOCRDialog PRIVATE Pdf4QtLibGui Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    if(PDF4QT_ENABLE_OCR)
        target_link_libraries(UnitTestsOCRDialog PRIVATE Pdf4QtOcrTesseract)
        target_compile_definitions(UnitTestsOCRDialog PRIVATE PDF4QT_OCR_TESSERACT)
    endif()
    set_target_properties(UnitTestsOCRDialog PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    if(PDF4QT_OCR_REQUIRED)
        target_compile_definitions(UnitTestsOCRDialog PRIVATE PDF4QT_OCR_TESTS_REQUIRED)
    endif()
    add_test(UnitTestsOCRDialog "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsOCRDialog")
    set_tests_properties(UnitTestsOCRDialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PDF4QT_OCR_DATA_DIRECTORY=${PDF4QT_OCR_BUILD_DATA_DIR}")
endif()

# Smoke test of the command 'PdfTool ocr' with the Tesseract engine and the built-in models
if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY AND PDF4QT_ENABLE_OCR)
    if(PDF4QT_OCR_REQUIRED)
        set(PDF4QT_OCR_SMOKE_REQUIRED ON)
    else()
        set(PDF4QT_OCR_SMOKE_REQUIRED OFF)
    endif()
    add_test(NAME PdfToolOcrSmoke
             COMMAND ${CMAKE_COMMAND} -DPDFTOOL=$<TARGET_FILE:PdfTool>
                                      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/PdfToolOcrSmoke
                                      -DOCR_REQUIRED=${PDF4QT_OCR_SMOKE_REQUIRED}
                                      -P ${CMAKE_CURRENT_SOURCE_DIR}/pdftoolocrsmoke.cmake)
    set_tests_properties(PdfToolOcrSmoke PROPERTIES
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PDF4QT_OCR_DATA_DIRECTORY=${PDF4QT_OCR_BUILD_DATA_DIR}"
                         SKIP_REGULAR_EXPRESSION "PDF4QT_OCR_SKIP")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsSignatureAppearance
        tst_signatureappearancetest.cpp
        ../Pdf4QtEditorPlugins/SignaturePlugin/signdialog.cpp
        ../Pdf4QtEditorPlugins/SignaturePlugin/signdialog.ui
    )
    target_include_directories(UnitTestsSignatureAppearance PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtEditorPlugins/SignaturePlugin)
    target_link_libraries(UnitTestsSignatureAppearance PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsSignatureAppearance PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsSignatureAppearance "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsSignatureAppearance")
    set_tests_properties(UnitTestsSignatureAppearance PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_executable(UnitTestsDiffOverlay
        tst_diffoverlaytest.cpp
        ../Pdf4QtDiff/mainwindow.cpp
        ../Pdf4QtDiff/mainwindow.ui
        ../Pdf4QtDiff/aboutdialog.cpp
        ../Pdf4QtDiff/aboutdialog.ui
        ../Pdf4QtDiff/differencesdockwidget.cpp
        ../Pdf4QtDiff/differencesdockwidget.ui
        ../Pdf4QtDiff/resources.qrc
        ../Pdf4QtDiff/utils.cpp
        ../Pdf4QtDiff/settingsdockwidget.cpp
        ../Pdf4QtDiff/settingsdockwidget.ui
    )
    target_include_directories(UnitTestsDiffOverlay PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtDiff)
    target_link_libraries(UnitTestsDiffOverlay PRIVATE Pdf4QtLibWidgets Pdf4QtLibCore Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsDiffOverlay PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsDiffOverlay "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDiffOverlay")
    set_tests_properties(UnitTestsDiffOverlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # The pages of the gallery are compiled into the test, which checks them for leaks of the palette
    add_executable(UnitTestsWidgetStyle
        tst_widgetstyletest.cpp
        ../Pdf4QtFluentGallery/gallerycardpages.cpp
        ../Pdf4QtFluentGallery/gallerycomponentpages.cpp
        ../Pdf4QtFluentGallery/gallerydelegatepages.cpp
        ../Pdf4QtFluentGallery/gallerypanelpages.cpp
        ../Pdf4QtFluentGallery/gallerypages.cpp
        ../Pdf4QtFluentGallery/gallerypages.h
    )
    target_include_directories(UnitTestsWidgetStyle PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtFluentGallery)
    target_link_libraries(UnitTestsWidgetStyle PRIVATE Pdf4QtLibFluent Qt6::Widgets Qt6::Test)
    # The platform theme of the test (colorschemetesttheme.h) needs the private headers of QtGui
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    if(TARGET Qt6::GuiPrivate)
        target_link_libraries(UnitTestsWidgetStyle PRIVATE Qt6::GuiPrivate)
        target_compile_definitions(UnitTestsWidgetStyle PRIVATE PDF4QT_TEST_PLATFORM_THEME)
    endif()
    set_target_properties(UnitTestsWidgetStyle PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsWidgetStyle "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsWidgetStyle")
    set_tests_properties(UnitTestsWidgetStyle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Components of the design: PDFComponentPainter, the bases of the rows and of
    # the cards and the widgets of the components; the pages of the gallery draw
    # every variant of the components
    add_executable(UnitTestsFluentComponents
        tst_fluentcomponentstest.cpp
        ../Pdf4QtFluentGallery/gallerycardpages.cpp
        ../Pdf4QtFluentGallery/gallerycomponentpages.cpp
        ../Pdf4QtFluentGallery/gallerydelegatepages.cpp
        ../Pdf4QtFluentGallery/gallerypanelpages.cpp
        ../Pdf4QtFluentGallery/gallerypages.cpp
        ../Pdf4QtFluentGallery/gallerypages.h
    )
    target_include_directories(UnitTestsFluentComponents PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtFluentGallery)
    target_link_libraries(UnitTestsFluentComponents PRIVATE Pdf4QtLibFluent Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsFluentComponents PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsFluentComponents "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsFluentComponents")
    set_tests_properties(UnitTestsFluentComponents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Destruction of every widget of every page of the gallery in every order and
    # situation (child first, parent first, from its own signal, with an open popup,
    # models, delegates and views of item views). Objects are tracked by the hooks
    # of QtCore (private API), so a double destruction, an event or a signal of
    # a destroyed object, a leaked object and a connection left on a long-lived
    # object are found also in a Release build. The test is built only, when the
    # private headers of QtCore are installed (Debian / Ubuntu: qt6-base-private-dev).
    find_package(Qt6 QUIET COMPONENTS CorePrivate)
    if(TARGET Qt6::CorePrivate)
        # List of the classes of Pdf4QtLibFluent with meta objects: the test checks,
        # that each of them has a sample on a page of the gallery
        file(GLOB PDF4QT_FLUENT_LIBRARY_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/Pdf4QtLibFluent/sources/*.h"
            "${CMAKE_SOURCE_DIR}/Pdf4QtLibFluent/sources/*.cpp")
        set(PDF4QT_FLUENT_CLASSES_HEADER "${CMAKE_CURRENT_BINARY_DIR}/fluentlifetime/fluentlifetimeclasses.h")
        add_custom_command(
            OUTPUT "${PDF4QT_FLUENT_CLASSES_HEADER}"
            COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}/Pdf4QtLibFluent/sources" "-DOUTPUT=${PDF4QT_FLUENT_CLASSES_HEADER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/fluentclasses.cmake"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fluentclasses.cmake" ${PDF4QT_FLUENT_LIBRARY_SOURCES}
            COMMENT "Listing the classes of Pdf4QtLibFluent"
            VERBATIM
        )

        add_executable(UnitTestsFluentLifetime
            tst_fluentlifetimetest.cpp
            "${PDF4QT_FLUENT_CLASSES_HEADER}"
            ../Pdf4QtFluentGallery/gallerycardpages.cpp
            ../Pdf4QtFluentGallery/gallerycomponentpages.cpp
            ../Pdf4QtFluentGallery/gallerydelegatepages.cpp
            ../Pdf4QtFluentGallery/gallerypanelpages.cpp
            ../Pdf4QtFluentGallery/gallerypages.cpp
            ../Pdf4QtFluentGallery/gallerypages.h
        )
        set_source_files_properties("${PDF4QT_FLUENT_CLASSES_HEADER}" PROPERTIES GENERATED ON SKIP_AUTOMOC ON)
        target_include_directories(UnitTestsFluentLifetime PRIVATE ${CMAKE_SOURCE_DIR}/Pdf4QtFluentGallery "${CMAKE_CURRENT_BINARY_DIR}/fluentlifetime")
        target_link_libraries(UnitTestsFluentLifetime PRIVATE Pdf4QtLibFluent Qt6::Widgets Qt6::Test Qt6::CorePrivate)
        set_target_properties(UnitTestsFluentLifetime PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsFluentLifetime "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsFluentLifetime")
        set_tests_properties(UnitTestsFluentLifetime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        message(STATUS "UnitTestsFluentLifetime is not built: the private headers of QtCore (Qt6::CorePrivate) are not installed")
    endif()
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # Storage of the new application (newapp_storage.md 6): SQLite in WAL, more
    # processes (the test starts itself), notifier, migrations, damaged / read-only /
    # newer files, portable memory database, settings. The test never links SQLite
    # itself - a second copy of SQLite in one process breaks the locks of the file.
    add_executable(UnitTestsAppStorage
        tst_appstoragetest.cpp
    )
    target_link_libraries(UnitTestsAppStorage PRIVATE Pdf4QtLibApp Qt6::Core Qt6::Test)
    set_target_properties(UnitTestsAppStorage PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppStorage "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppStorage")

    # Core of the new application: command line, forwarding to a running instance,
    # logs, command registry, application context and the executable PDF4QT
    add_executable(UnitTestsAppCore
        tst_appcoretest.cpp
    )
    target_link_libraries(UnitTestsAppCore PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
    add_dependencies(UnitTestsAppCore Pdf4QtApp)
    set_target_properties(UnitTestsAppCore PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppCore "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppCore")
    set_tests_properties(UnitTestsAppCore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Window shell of the new application: routes, route table, history, navigator,
    # Page Not Found, tabs, sidebar, banners, hit test of the frameless window
    add_executable(UnitTestsAppNavigation
        tst_appnavigationtest.cpp
    )
    target_link_libraries(UnitTestsAppNavigation PRIVATE Pdf4QtLibApp Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    if(TARGET Qt6::GuiPrivate)
        target_link_libraries(UnitTestsAppNavigation PRIVATE Qt6::GuiPrivate)
        target_compile_definitions(UnitTestsAppNavigation PRIVATE PDF4QT_TEST_PLATFORM_THEME)
    endif()
    set_target_properties(UnitTestsAppNavigation PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppNavigation "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppNavigation")
    set_tests_properties(UnitTestsAppNavigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Data and services of step 2 of the new application: static tool catalog of the
    # core, repositories of the recent files / folders / pinned locations / workspaces /
    # tools, thumbnailer and folder information (the test starts itself as a second
    # process; it never links SQLite itself)
    add_executable(UnitTestsAppData
        tst_appdatatest.cpp
    )
    target_link_libraries(UnitTestsAppData PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAppData PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppData "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppData")
    set_tests_properties(UnitTestsAppData PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # Pages About, Support and Sponsor of the new application (step 2): routes,
    # build information, third-party libraries, video catalog, For you, geometry
    # and pixels against Figma, deletion of the pages
    add_executable(UnitTestsAppInfoPages
        tst_appinfopagestest.cpp
    )
    target_link_libraries(UnitTestsAppInfoPages PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAppInfoPages PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppInfoPages "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppInfoPages")
    set_tests_properties(UnitTestsAppInfoPages PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # Content of the new application (step 2): What's New files, catalog and the
    # bundled files against RELEASES.txt, the update check against a loopback
    # HTTP server, What's New pages, the Markdown parser and view, the manual /help
    add_executable(UnitTestsAppContent
        tst_appcontenttest.cpp
    )
    target_link_libraries(UnitTestsAppContent PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
    target_compile_definitions(UnitTestsAppContent PRIVATE PDF4QT_SOURCE_DIRECTORY="${CMAKE_SOURCE_DIR}")
    set_target_properties(UnitTestsAppContent PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppContent "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppContent")
    set_tests_properties(UnitTestsAppContent PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # Global search of the new application (step 2): the matching of the core, the
    # engine and its providers, the page /search and the popup of the field
    add_executable(UnitTestsAppSearch
        tst_appsearchtest.cpp
    )
    target_link_libraries(UnitTestsAppSearch PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAppSearch PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppSearch "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppSearch")
    set_tests_properties(UnitTestsAppSearch PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    # Pages Home, Open and Recent of the new application (step 2): data of the
    # repositories, menus, dialogs of workspaces and of the history, drop of files,
    # thumbnails, notifications, geometry and pixels against Figma, lifetime
    add_executable(UnitTestsAppHome
        tst_apphometest.cpp
    )
    target_link_libraries(UnitTestsAppHome PRIVATE Pdf4QtLibApp Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    set_target_properties(UnitTestsAppHome PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_EXCLUDE_FROM_TRANSLATION ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
    )
    add_test(UnitTestsAppHome "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppHome")
    set_tests_properties(UnitTestsAppHome PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# The document tab (step 3 of the new application): one executable per package,
# all of them use the shared fixture appdocumentfixture (context with a memory
# database, the real window, the sample PDFs made by PDFDocumentBuilder)
if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    foreach(PDF4QT_DOCUMENT_TEST
            AppDocument
            AppDocumentView
            AppDocumentPanels
            AppDocumentSearch
            AppDocumentFile
            AppDocumentProperties)
        string(TOLOWER "${PDF4QT_DOCUMENT_TEST}" PDF4QT_DOCUMENT_TEST_FILE)
        add_executable(UnitTests${PDF4QT_DOCUMENT_TEST}
            tst_${PDF4QT_DOCUMENT_TEST_FILE}test.cpp
            appdocumentfixture.cpp
            appdocumentfixture.h
        )
        target_link_libraries(UnitTests${PDF4QT_DOCUMENT_TEST} PRIVATE Pdf4QtLibApp Pdf4QtLibWidgets Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        set_target_properties(UnitTests${PDF4QT_DOCUMENT_TEST} PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTests${PDF4QT_DOCUMENT_TEST} "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTests${PDF4QT_DOCUMENT_TEST}")
        set_tests_properties(UnitTests${PDF4QT_DOCUMENT_TEST} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()

    # Package E prints by QPrinter (printers of the test write PDF files)
    target_link_libraries(UnitTestsAppDocumentFile PRIVATE Qt6::PrintSupport)
endif()

# Annotations, Insert, Notes / Outline editing and form filling (step 4 of the new
# application): one executable per package of wave 1, all with the shared fixture
if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    foreach(PDF4QT_ANNOTATION_TEST
            AppAnnotate
            AppAnnotationEdit
            AppNotesOutline
            AppForms)
        string(TOLOWER "${PDF4QT_ANNOTATION_TEST}" PDF4QT_ANNOTATION_TEST_FILE)
        add_executable(UnitTests${PDF4QT_ANNOTATION_TEST}
            tst_${PDF4QT_ANNOTATION_TEST_FILE}test.cpp
            appdocumentfixture.cpp
            appdocumentfixture.h
        )
        target_link_libraries(UnitTests${PDF4QT_ANNOTATION_TEST} PRIVATE Pdf4QtLibApp Pdf4QtLibWidgets Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        set_target_properties(UnitTests${PDF4QT_ANNOTATION_TEST} PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTests${PDF4QT_ANNOTATION_TEST} "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTests${PDF4QT_ANNOTATION_TEST}")
        set_tests_properties(UnitTests${PDF4QT_ANNOTATION_TEST} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()

    # Generic lifetime test of step 4: the tab / window deleted from every signal of
    # the application in the scenarios of the user. It follows the emissions on the
    # stack by the signal spy callbacks of QtCore (private headers, as
    # UnitTestsFluentLifetime), so it is built only when they are installed.
    find_package(Qt6 QUIET COMPONENTS CorePrivate)
    if(TARGET Qt6::CorePrivate)
        add_executable(UnitTestsAppAnnotationLifetime
            tst_appannotationlifetimetest.cpp
            appdocumentfixture.cpp
            appdocumentfixture.h
        )
        target_link_libraries(UnitTestsAppAnnotationLifetime PRIVATE Pdf4QtLibApp Pdf4QtLibWidgets Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::CorePrivate)
        set_target_properties(UnitTestsAppAnnotationLifetime PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTestsAppAnnotationLifetime "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAppAnnotationLifetime")
        set_tests_properties(UnitTestsAppAnnotationLifetime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        message(STATUS "UnitTestsAppAnnotationLifetime is not built: the private headers of QtCore (Qt6::CorePrivate) are not installed")
    endif()
endif()

# Settings (step 5 of the new application): wave 0 and package A in
# UnitTestsAppSettings, one executable per package of wave 1, all with the fixture
# appsettingsfixture (the fixture of the document tab with the Settings helpers)
if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    foreach(PDF4QT_SETTINGS_TEST
            AppSettings
            AppSettingsRendering
            AppSettingsEditing
            AppSettingsShortcuts
            AppSettingsLifetime)
        string(TOLOWER "${PDF4QT_SETTINGS_TEST}" PDF4QT_SETTINGS_TEST_FILE)
        add_executable(UnitTests${PDF4QT_SETTINGS_TEST}
            tst_${PDF4QT_SETTINGS_TEST_FILE}test.cpp
            appdocumentfixture.cpp
            appdocumentfixture.h
            appsettingsfixture.cpp
            appsettingsfixture.h
        )
        target_link_libraries(UnitTests${PDF4QT_SETTINGS_TEST} PRIVATE Pdf4QtLibApp Pdf4QtLibWidgets Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        set_target_properties(UnitTests${PDF4QT_SETTINGS_TEST} PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTests${PDF4QT_SETTINGS_TEST} "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTests${PDF4QT_SETTINGS_TEST}")
        set_tests_properties(UnitTests${PDF4QT_SETTINGS_TEST} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

# The Tools catalog (step 6 of the new application): wave 0 and package A in
# UnitTestsAppTools, package B in UnitTestsAppToolCollections, package C in
# UnitTestsAppToolsPanel, all with the fixture apptoolsfixture (the fixture of the
# document tab with the helpers of the tools)
if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    foreach(PDF4QT_TOOLS_TEST
            AppTools
            AppToolCollections
            AppToolsPanel
            AppToolsLifetime)
        string(TOLOWER "${PDF4QT_TOOLS_TEST}" PDF4QT_TOOLS_TEST_FILE)
        add_executable(UnitTests${PDF4QT_TOOLS_TEST}
            tst_${PDF4QT_TOOLS_TEST_FILE}test.cpp
            appdocumentfixture.cpp
            appdocumentfixture.h
            apptoolsfixture.cpp
            apptoolsfixture.h
        )
        target_link_libraries(UnitTests${PDF4QT_TOOLS_TEST} PRIVATE Pdf4QtLibApp Pdf4QtLibWidgets Pdf4QtLibCore Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        set_target_properties(UnitTests${PDF4QT_TOOLS_TEST} PROPERTIES
            WIN32_EXECUTABLE OFF
            MACOSX_BUNDLE OFF
            QT_EXCLUDE_FROM_TRANSLATION ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
        )
        add_test(UnitTests${PDF4QT_TOOLS_TEST} "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTests${PDF4QT_TOOLS_TEST}")
        set_tests_properties(UnitTests${PDF4QT_TOOLS_TEST} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

# The core of the tools of phase 2 (Pdf4QtLibCore, step 6 package D): the extended
# data of the catalog, the suggestions of the next tools, the tool interface, its
# parameters, results and registry
add_executable(UnitTestsDocumentTool
    tst_documenttooltest.cpp
    testdocumenttool.h
)
target_link_libraries(UnitTestsDocumentTool PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsDocumentTool PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    QT_EXCLUDE_FROM_TRANSLATION ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsDocumentTool "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDocumentTool")
set_tests_properties(UnitTestsDocumentTool PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Properties of the annotations read and written by PDFAnnotationPropertyEditor
# (Pdf4QtLibCore, step 4 package B)
add_executable(UnitTestsAnnotationProperties
    tst_annotationpropertiestest.cpp
)
target_link_libraries(UnitTestsAnnotationProperties PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsAnnotationProperties PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    QT_EXCLUDE_FROM_TRANSLATION ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsAnnotationProperties "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsAnnotationProperties")
set_tests_properties(UnitTestsAnnotationProperties PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Text index of a whole document and the search in it, page labels (Pdf4QtLibCore, step 3)
add_executable(UnitTestsDocumentTextIndex
    tst_documenttextindextest.cpp
)
target_link_libraries(UnitTestsDocumentTextIndex PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(UnitTestsDocumentTextIndex PRIVATE PDF4QT_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/Pdf4QtLibFluent/resources/fonts")
set_target_properties(UnitTestsDocumentTextIndex PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    QT_EXCLUDE_FROM_TRANSLATION ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsDocumentTextIndex "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDocumentTextIndex")
set_tests_properties(UnitTestsDocumentTextIndex PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
