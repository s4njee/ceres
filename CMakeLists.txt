cmake_minimum_required(VERSION 3.21)
project(ceres VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5+ (loadFromModule / objectCreationFailed). Install via: brew install qt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
qt_standard_project_setup(REQUIRES 6.5)
if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# ---------------------------------------------------------------------------
# ceres_core: non-GUI engine, app services, models, and controller.
# Deliberately free of Quick/QML deps so the parser, storage, and controller
# behavior can be unit-tested headless.
# ---------------------------------------------------------------------------
add_library(ceres_core STATIC
    src/cli/AdHocTransfer.h        src/cli/AdHocTransfer.cpp
    src/core/SyncJob.h
    src/core/Peer.h
    src/core/FileEntry.h
    src/core/Format.h
    src/core/AppSettings.h
    src/core/Endpoint.h            src/core/Endpoint.cpp
    src/core/SshHost.h
    src/core/SshKnownHosts.h        src/core/SshKnownHosts.cpp
    src/core/SshHostStore.h          src/core/SshHostStore.cpp
    src/core/SshConfigImport.h       src/core/SshConfigImport.cpp
    src/core/SecretStore.h           src/core/SecretStore.cpp
    src/engine/RsyncEvents.h
    src/engine/SyncEngine.h
    src/engine/OutputParser.h        src/engine/OutputParser.cpp
    src/engine/ArgvBuilder.h         src/engine/ArgvBuilder.cpp
    src/engine/BinaryLocator.h       src/engine/BinaryLocator.cpp
    src/engine/RsyncProcessEngine.h  src/engine/RsyncProcessEngine.cpp
    src/net/Beacon.h                 src/net/Beacon.cpp
    src/net/DiscoveryService.h       src/net/DiscoveryService.cpp
    src/net/NetworkUtils.h           src/net/NetworkUtils.cpp
    src/models/ChangeListModel.h     src/models/ChangeListModel.cpp
    src/models/SshHostListModel.h     src/models/SshHostListModel.cpp
    src/models/FileListModel.h        src/models/FileListModel.cpp
    src/models/TransfersModel.h       src/models/TransfersModel.cpp
    src/models/PeerModel.h           src/models/PeerModel.cpp
    src/app/JobController.h          src/app/JobController.cpp
    src/app/PathCompleter.h          src/app/PathCompleter.cpp
    src/app/SshCommand.h             src/app/SshCommand.cpp
    src/app/RemoteFs.h               src/app/RemoteFs.cpp
    src/app/TransferManager.h        src/app/TransferManager.cpp
    src/app/BrowseController.h       src/app/BrowseController.cpp
    src/app/BrowseSessions.h         src/app/BrowseSessions.cpp
    src/app/Notifier.h               src/app/Notifier.cpp
)
target_include_directories(ceres_core PUBLIC src)
target_link_libraries(ceres_core PUBLIC Qt6::Core Qt6::Network)
if(APPLE)
    target_link_libraries(ceres_core PUBLIC "-framework Security" "-framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(ceres_core PUBLIC advapi32)  # Credential Manager (CredWrite/Read/Delete)
endif()

# ---------------------------------------------------------------------------
# ceres: the GUI app (QML/Quick shell over ceres_core).
# ---------------------------------------------------------------------------
qt_add_executable(ceres src/main.cpp)
# URI must not collide case-insensitively with the "ceres" binary: on macOS the
# generated module dir (named after the URI) would clash with the executable.
qt_add_qml_module(ceres
    URI CeresUi
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Chip.qml
        qml/FlatButton.qml
        qml/Field.qml
        qml/DeleteConfirmDialog.qml
        qml/SshPasswordDialog.qml
        qml/KnownHostChangedDialog.qml
        qml/AddSshHostDialog.qml
        qml/NewHostDialog.qml
        qml/BrowseTab.qml
        qml/TransfersDialog.qml
        qml/FileBrowserPane.qml
)
target_link_libraries(ceres PRIVATE
    ceres_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
target_compile_definitions(ceres PRIVATE CERES_VERSION="${PROJECT_VERSION}")

# The window/dock icon, embedded so it's available at runtime on every platform.
qt_add_resources(ceres "ceres_appicon"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES icons/ceres-512.png
)

if(WIN32)
    add_custom_target(stage-msys2-rsync
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage-msys2-rsync.ps1"
                -DestinationDir "$<TARGET_FILE_DIR:ceres>"
        DEPENDS ceres
        COMMENT "Staging MSYS2 rsync/OpenSSH runtime beside ceres.exe"
        VERBATIM
    )
    add_custom_target(stage-qt-runtime
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage-qt-runtime.ps1"
                -DestinationDir "$<TARGET_FILE_DIR:ceres>"
        DEPENDS ceres
        COMMENT "Staging Qt runtime beside ceres.exe"
        VERBATIM
    )
    add_custom_target(stage-windows-runtime
        DEPENDS stage-msys2-rsync stage-qt-runtime
    )
endif()

# ---------------------------------------------------------------------------
# Packaging: install rules + CPack (macOS .dmg, Linux .tar.gz/.deb, Windows .zip).
#   cmake --build build --target ceres
#   cmake --install build --prefix dist     # staged install tree
#   cd build && cpack                        # produces a distributable archive
# ---------------------------------------------------------------------------
if(APPLE)
    set_target_properties(ceres PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Ceres"
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.ceres.Ceres"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "ceres.icns"
    )
    # Copy the .icns into the bundle's Resources folder.
    target_sources(ceres PRIVATE icons/ceres.icns)
    set_source_files_properties(icons/ceres.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# Bundle the Qt runtime (frameworks/DLLs + QML plugins) at install time.
qt_generate_deploy_qml_app_script(
    TARGET ceres
    OUTPUT_SCRIPT ceres_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${ceres_deploy_script})

if(APPLE)
    install(TARGETS ceres BUNDLE DESTINATION .)
else()
    install(TARGETS ceres RUNTIME DESTINATION bin)
endif()

if(UNIX AND NOT APPLE)
    # Desktop integration: launcher entry + themed icons.
    install(FILES packaging/ceres.desktop DESTINATION share/applications)
    install(FILES icons/ceres-256.png
            DESTINATION share/icons/hicolor/256x256/apps RENAME ceres.png)
    install(FILES icons/ceres-512.png
            DESTINATION share/icons/hicolor/512x512/apps RENAME ceres.png)
    install(FILES icons/ceres.svg
            DESTINATION share/icons/hicolor/scalable/apps RENAME ceres.svg)
endif()

set(CPACK_PACKAGE_NAME "Ceres")
set(CPACK_PACKAGE_VENDOR "Ceres")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "rsync-based file transfer and folder sync over SSH")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/s4njee/ceres")
set(CPACK_PACKAGE_EXECUTABLES "ceres" "Ceres")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ceres")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "rsync, openssh-client")
endif()
include(CPack)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
