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

add_executable(UnitTestsSignatureBuilder tst_signaturebuildertest.cpp)
target_link_libraries(UnitTestsSignatureBuilder PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsSignatureBuilder PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsSignatureBuilder "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsSignatureBuilder")

add_executable(UnitTestsTimestamp tst_timestamptest.cpp)
target_link_libraries(UnitTestsTimestamp PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test OpenSSL::Crypto)
set_target_properties(UnitTestsTimestamp PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsTimestamp "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsTimestamp")

add_executable(UnitTestsDocumentWriter tst_documentwritertest.cpp)
target_link_libraries(UnitTestsDocumentWriter PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(UnitTestsDocumentWriter PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
add_test(UnitTestsDocumentWriter "${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/UnitTestsDocumentWriter")

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
