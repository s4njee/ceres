# Headless unit tests for the non-GUI pieces (no GUI, no real rsync needed).

function(ceres_add_test name target source)
    qt_add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ceres_core Qt6::Test)
    if(WIN32)
        add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:${target}>)
    else()
        add_test(NAME ${name} COMMAND ${target})
    endif()
endfunction()

ceres_add_test(OutputParser tst_outputparser tst_outputparser.cpp)

ceres_add_test(ArgvBuilder tst_argvbuilder tst_argvbuilder.cpp)

ceres_add_test(AdHocTransfer tst_adhoctransfer tst_adhoctransfer.cpp)

ceres_add_test(Endpoint tst_endpoint tst_endpoint.cpp)

ceres_add_test(BinaryLocator tst_binarylocator tst_binarylocator.cpp)

ceres_add_test(SecretStore tst_secretstore tst_secretstore.cpp)

ceres_add_test(SshKnownHosts tst_sshknownhosts tst_sshknownhosts.cpp)

ceres_add_test(SshConfigImport tst_sshconfigimport tst_sshconfigimport.cpp)

ceres_add_test(Snapshot tst_snapshot tst_snapshot.cpp)

ceres_add_test(Pairing tst_pairing tst_pairing.cpp)

ceres_add_test(Beacon tst_beacon tst_beacon.cpp)

ceres_add_test(PathCompleter tst_pathcompleter tst_pathcompleter.cpp)

ceres_add_test(JobController tst_jobcontroller tst_jobcontroller.cpp)

ceres_add_test(FileListModel tst_filelistmodel tst_filelistmodel.cpp)

ceres_add_test(RemoteFs tst_remotefs tst_remotefs.cpp)

ceres_add_test(TransferManager tst_transfermanager tst_transfermanager.cpp)
