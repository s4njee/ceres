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
# Deliberately free of Quick/QML deps so the parser, scheduling, storage, and
# controller behavior can be unit-tested headless.
# ---------------------------------------------------------------------------
add_library(ceres_core STATIC
    src/core/SyncJob.h
    src/core/Peer.h
    src/core/Endpoint.h            src/core/Endpoint.cpp
    src/core/JobId.h
    src/core/ProfileStore.h          src/core/ProfileStore.cpp
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
    src/sched/Scheduler.h            src/sched/Scheduler.cpp
    src/models/ChangeListModel.h     src/models/ChangeListModel.cpp
    src/models/JobListModel.h        src/models/JobListModel.cpp
    src/models/PeerModel.h           src/models/PeerModel.cpp
    src/app/JobController.h          src/app/JobController.cpp
    src/app/PathCompleter.h          src/app/PathCompleter.cpp
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
)
target_link_libraries(ceres PRIVATE
    ceres_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

# Headless runner invoked by the OS scheduler (and usable from a terminal).
qt_add_executable(ceres-runner src/cli/runner_main.cpp)
target_link_libraries(ceres-runner PRIVATE ceres_core Qt6::Core)

if(WIN32)
    add_custom_target(stage-msys2-rsync
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage-msys2-rsync.ps1"
                -DestinationDir "$<TARGET_FILE_DIR:ceres>"
        DEPENDS ceres ceres-runner
        COMMENT "Staging MSYS2 rsync/OpenSSH runtime beside ceres.exe"
        VERBATIM
    )
    add_custom_target(stage-qt-runtime
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stage-qt-runtime.ps1"
                -DestinationDir "$<TARGET_FILE_DIR:ceres>"
        DEPENDS ceres ceres-runner
        COMMENT "Staging Qt runtime beside ceres.exe"
        VERBATIM
    )
    add_custom_target(stage-windows-runtime
        DEPENDS stage-msys2-rsync stage-qt-runtime
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
