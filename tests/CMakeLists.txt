# Headless unit tests for the non-GUI pieces (no GUI, no real rsync needed).

qt_add_executable(tst_outputparser tst_outputparser.cpp)
target_link_libraries(tst_outputparser PRIVATE ceres_core Qt6::Test)
add_test(NAME OutputParser COMMAND tst_outputparser)

qt_add_executable(tst_argvbuilder tst_argvbuilder.cpp)
target_link_libraries(tst_argvbuilder PRIVATE ceres_core Qt6::Test)
add_test(NAME ArgvBuilder COMMAND tst_argvbuilder)

qt_add_executable(tst_endpoint tst_endpoint.cpp)
target_link_libraries(tst_endpoint PRIVATE ceres_core Qt6::Test)
add_test(NAME Endpoint COMMAND tst_endpoint)

qt_add_executable(tst_binarylocator tst_binarylocator.cpp)
target_link_libraries(tst_binarylocator PRIVATE ceres_core Qt6::Test)
add_test(NAME BinaryLocator COMMAND tst_binarylocator)

qt_add_executable(tst_profilestore tst_profilestore.cpp)
target_link_libraries(tst_profilestore PRIVATE ceres_core Qt6::Test)
add_test(NAME ProfileStore COMMAND tst_profilestore)

qt_add_executable(tst_secretstore tst_secretstore.cpp)
target_link_libraries(tst_secretstore PRIVATE ceres_core Qt6::Test)
add_test(NAME SecretStore COMMAND tst_secretstore)

qt_add_executable(tst_beacon tst_beacon.cpp)
target_link_libraries(tst_beacon PRIVATE ceres_core Qt6::Test)
add_test(NAME Beacon COMMAND tst_beacon)

qt_add_executable(tst_scheduler tst_scheduler.cpp)
target_link_libraries(tst_scheduler PRIVATE ceres_core Qt6::Test)
add_test(NAME Scheduler COMMAND tst_scheduler)

qt_add_executable(tst_pathcompleter tst_pathcompleter.cpp)
target_link_libraries(tst_pathcompleter PRIVATE ceres_core Qt6::Test)
add_test(NAME PathCompleter COMMAND tst_pathcompleter)

qt_add_executable(tst_jobcontroller tst_jobcontroller.cpp)
target_link_libraries(tst_jobcontroller PRIVATE ceres_core Qt6::Test)
add_test(NAME JobController COMMAND tst_jobcontroller)
