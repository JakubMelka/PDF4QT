# Gallery of the widgets drawn by PDFWidgetStyle. It is a development tool:
# each tab shows one section of the page "Widgets" of the design, and the
# samples can be exported to images, which are compared with the design.
add_executable(Pdf4QtFluentGallery
    gallerypages.cpp
    gallerypages.h
    gallerywindow.cpp
    gallerywindow.h
    main.cpp
)

target_link_libraries(Pdf4QtFluentGallery PRIVATE Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(Pdf4QtFluentGallery PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    QT_EXCLUDE_FROM_TRANSLATION ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
