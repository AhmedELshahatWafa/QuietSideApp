cmake_minimum_required(VERSION 3.16)
project(CoworkingSpaceManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Sql
    Network
    Multimedia
)

# Enable high DPI scaling
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x060000)

# Source files
set(SOURCES
    src/main.cpp

    src/models/Client.cpp
    src/models/Room.cpp
    src/models/Session.cpp
    src/models/Transaction.cpp

    src/database/DatabaseManager.cpp

    src/services/ClientManager.cpp
    src/services/SessionTimerService.cpp

    src/ui/MainWindow.cpp
    src/ui/LobbyView.cpp
    src/ui/ClientListView.cpp
    src/ui/ClientFormDialog.cpp
    src/ui/AlertsPanel.cpp
)

set(HEADERS
    src/models/Client.h
    src/models/Room.h
    src/models/Session.h
    src/models/Transaction.h

    src/database/DatabaseManager.h

    src/services/ClientManager.h
    src/services/SessionTimerService.h

    src/ui/MainWindow.h
    src/ui/LobbyView.h
    src/ui/ClientListView.h
    src/ui/ClientFormDialog.h
    src/ui/AlertsPanel.h
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    resources/resources.qrc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Multimedia
)

# Windows: produce a single .exe (no console window in release)
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt windeployqt6
        HINTS "${QT_INSTALL_PREFIX}/bin"
    )
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt was not found in the Qt installation.")
    endif()

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --compiler-runtime
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt runtime dependencies"
        VERBATIM
    )
endif()

# Install rules (optional)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
