set(SSH_TEST_TARGET selfhosted_ssh_test)
find_package(Qt6 REQUIRED COMPONENTS Core Core5Compat Concurrent Gui Network Test)
find_package(OpenSSL REQUIRED)
find_package(libssh REQUIRED)

set(_client_root "${CMAKE_CURRENT_LIST_DIR}/../../../client")
set(_common_root "${CMAKE_CURRENT_LIST_DIR}/../../../common")

# SshSession is intentionally linked to the production implementation. The
# small support set below supplies the transitive symbols used by its command
# construction and variable generation; no SSH connection is made by tests.
add_executable(${SSH_TEST_TARGET}
    ssh_session_test.cpp
    ${_client_root}/core/utils/containerEnum.h
    ${_client_root}/core/utils/errorCodes.h
    ${_client_root}/core/utils/protocolEnum.h
    ${_client_root}/core/utils/containers/containerUtils.cpp
    ${_client_root}/core/protocols/protocolUtils.cpp
    ${_client_root}/core/utils/networkUtilities.cpp
    ${_client_root}/core/utils/selfhosted/sshSession.cpp
    ${_client_root}/core/utils/selfhosted/sshClient.cpp
    ${_client_root}/core/utils/selfhosted/scriptsRegistry.cpp
    ${_client_root}/core/utils/utilities.cpp
    ${_client_root}/core/models/containerConfig.cpp
    ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
    ${_client_root}/core/models/agentWorkloadApply.cpp
    ${_client_root}/core/models/agentWorkloadReconciliation.cpp
    ${_client_root}/core/models/agentWorkloadLogin.cpp
    ${_client_root}/core/models/protocolConfig.cpp
    ${_client_root}/core/models/protocols/awgProtocolConfig.cpp
    ${_client_root}/core/models/protocols/amgptAuthProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/dnsProtocolConfig.cpp
    ${_client_root}/core/models/protocols/ikev2ProtocolConfig.cpp
    ${_client_root}/core/models/protocols/mtProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/openVpnProtocolConfig.cpp
    ${_client_root}/core/models/protocols/openClawCodexProtocolConfig.cpp
    ${_client_root}/core/models/protocols/sftpProtocolConfig.cpp
    ${_client_root}/core/models/protocols/socks5ProxyProtocolConfig.cpp
    ${_client_root}/core/models/protocols/telemtProtocolConfig.cpp
    ${_client_root}/core/models/protocols/torProtocolConfig.cpp
    ${_client_root}/core/models/protocols/wireGuardProtocolConfig.cpp
    ${_client_root}/core/models/protocols/xrayProtocolConfig.cpp
    ${_common_root}/logger/logger.cpp
    ${_client_root}/server_scripts/serverScripts.qrc
)

target_compile_features(${SSH_TEST_TARGET} PRIVATE cxx_std_17)
if(WIN32)
    target_compile_definitions(${SSH_TEST_TARGET} PRIVATE _WINSOCKAPI_)
endif()
target_include_directories(${SSH_TEST_TARGET} PRIVATE
    ${_client_root}
    ${_common_root}
    ${_common_root}/logger
    ${_common_root}/crypto
    ${PROJECT_BINARY_DIR}/client
)
target_link_libraries(${SSH_TEST_TARGET} PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Test
    OpenSSL::SSL OpenSSL::Crypto ssh::ssh
)
set_target_properties(${SSH_TEST_TARGET} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC ON
)

add_test(NAME selfhosted.ssh COMMAND ${SSH_TEST_TARGET})
set_tests_properties(selfhosted.ssh PROPERTIES
    LABELS "unit"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests ${SSH_TEST_TARGET})
endif()
