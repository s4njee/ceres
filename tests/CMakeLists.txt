# Headless unit tests for the pure engine pieces (no GUI, no real rsync needed).

qt_add_executable(tst_outputparser tst_outputparser.cpp)
target_link_libraries(tst_outputparser PRIVATE ceres_core Qt6::Test)
add_test(NAME OutputParser COMMAND tst_outputparser)

qt_add_executable(tst_argvbuilder tst_argvbuilder.cpp)
target_link_libraries(tst_argvbuilder PRIVATE ceres_core Qt6::Test)
add_test(NAME ArgvBuilder COMMAND tst_argvbuilder)

qt_add_executable(tst_profilestore tst_profilestore.cpp)
target_link_libraries(tst_profilestore PRIVATE ceres_core Qt6::Test)
add_test(NAME ProfileStore COMMAND tst_profilestore)
