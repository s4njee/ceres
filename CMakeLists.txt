cmake_minimum_required(VERSION 3.21)
project(ceres VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5+ (loadFromModule / objectCreationFailed). Install via: brew install qt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# ceres_core: Qt-Core-only engine + models + controller.
# Deliberately free of Quick/QML deps so the parser and arg builder can be
# unit-tested headless (and so a future Windows/WSL engine can reuse it).
# ---------------------------------------------------------------------------
add_library(ceres_core STATIC
    src/core/SyncJob.h
    src/core/Peer.h
    src/core/ProfileStore.h          src/core/ProfileStore.cpp
    src/engine/RsyncEvents.h
    src/engine/SyncEngine.h
    src/engine/OutputParser.h        src/engine/OutputParser.cpp
    src/engine/ArgvBuilder.h         src/engine/ArgvBuilder.cpp
    src/engine/BinaryLocator.h       src/engine/BinaryLocator.cpp
    src/engine/RsyncProcessEngine.h  src/engine/RsyncProcessEngine.cpp
    src/net/Beacon.h                 src/net/Beacon.cpp
    src/net/DiscoveryService.h       src/net/DiscoveryService.cpp
    src/sched/Scheduler.h            src/sched/Scheduler.cpp
    src/models/ChangeListModel.h     src/models/ChangeListModel.cpp
    src/models/JobListModel.h        src/models/JobListModel.cpp
    src/models/PeerModel.h           src/models/PeerModel.cpp
    src/app/JobController.h          src/app/JobController.cpp
)
target_include_directories(ceres_core PUBLIC src)
target_link_libraries(ceres_core PUBLIC Qt6::Core Qt6::Network)

# ---------------------------------------------------------------------------
# ceres: the GUI app (QML/Quick shell over ceres_core).
# ---------------------------------------------------------------------------
qt_add_executable(ceres src/main.cpp)
# URI must not collide case-insensitively with the "ceres" binary: on macOS the
# generated module dir (named after the URI) would clash with the executable.
qt_add_qml_module(ceres
    URI CeresUi
    VERSION 1.0
    QML_FILES qml/Main.qml
)
target_link_libraries(ceres PRIVATE
    ceres_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

# Headless runner invoked by the OS scheduler (and usable from a terminal).
qt_add_executable(ceres-runner src/cli/runner_main.cpp)
target_link_libraries(ceres-runner PRIVATE ceres_core Qt6::Core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
