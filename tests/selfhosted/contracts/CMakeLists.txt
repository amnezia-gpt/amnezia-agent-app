set(CONTRACT_TEST_TARGET selfhosted_contract_test)
find_package(Qt6 REQUIRED COMPONENTS Core Core5Compat Concurrent Gui Network Test)
find_package(OpenSSL REQUIRED)
find_package(libssh REQUIRED)

set(_client_root "${CMAKE_CURRENT_LIST_DIR}/../../../client")
set(_common_root "${CMAKE_CURRENT_LIST_DIR}/../../../common")

add_executable(${CONTRACT_TEST_TARGET}
    container_registry_contract_test.cpp
    ${_client_root}/core/utils/containerEnum.h
    ${_client_root}/core/utils/protocolEnum.h
    ${_client_root}/core/utils/containers/containerUtils.h
    ${_client_root}/core/utils/containers/containerUtils.cpp
    ${_client_root}/core/protocols/protocolUtils.h
    ${_client_root}/core/protocols/protocolUtils.cpp
    ${_client_root}/core/utils/networkUtilities.cpp
    ${_client_root}/core/utils/selfhosted/sshSession.cpp
    ${_client_root}/core/utils/selfhosted/sshClient.cpp
    ${_client_root}/core/utils/selfhosted/scriptsRegistry.cpp
    ${_client_root}/core/utils/utilities.cpp
    ${_client_root}/core/models/containerConfig.cpp
    ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
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

target_compile_features(${CONTRACT_TEST_TARGET} PRIVATE cxx_std_17)
target_include_directories(${CONTRACT_TEST_TARGET} PRIVATE
    ${_client_root}
    ${_common_root}
    ${_common_root}/logger
    ${_common_root}/crypto
    ${PROJECT_BINARY_DIR}/client
)
target_compile_definitions(${CONTRACT_TEST_TARGET} PRIVATE CONTRACT_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
if(WIN32)
    target_compile_definitions(${CONTRACT_TEST_TARGET} PRIVATE _WINSOCKAPI_)
endif()
target_link_libraries(${CONTRACT_TEST_TARGET} PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Test
    OpenSSL::SSL OpenSSL::Crypto ssh::ssh
)
set_target_properties(${CONTRACT_TEST_TARGET} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${CONTRACT_TEST_TARGET} PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(${CONTRACT_TEST_TARGET} PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(${CONTRACT_TEST_TARGET} PRIVATE -Wl,--gc-sections)
    endif()
endif()

add_test(NAME selfhosted.contracts COMMAND ${CONTRACT_TEST_TARGET})
set_tests_properties(selfhosted.contracts PROPERTIES
    LABELS "contract"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests ${CONTRACT_TEST_TARGET})
endif()

amnezia_add_qt_test(
    NAME selfhosted_agent_workload_deployment_spec_test
    SOURCES
        agent_workload_deployment_spec_test.cpp
        ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
        ${_client_root}/core/models/protocols/amgptAuthProxyProtocolConfig.cpp
        ${_client_root}/core/models/protocols/openClawCodexProtocolConfig.cpp
    LABELS contract
    INCLUDE_DIRECTORIES ${_client_root}
    TIMEOUT 30
)

amnezia_add_qt_test(
    NAME selfhosted_agent_workload_reconciliation_test
    SOURCES
        agent_workload_reconciliation_test.cpp
        ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
        ${_client_root}/core/models/agentWorkloadReconciliation.cpp
        ${_client_root}/core/models/protocols/amgptAuthProxyProtocolConfig.cpp
        ${_client_root}/core/models/protocols/openClawCodexProtocolConfig.cpp
    LABELS contract
    INCLUDE_DIRECTORIES ${_client_root}
    TIMEOUT 30
)

amnezia_add_qt_test(
    NAME selfhosted_agent_workload_login_test
    SOURCES
        agent_workload_login_test.cpp
        ${_client_root}/core/models/agentWorkloadLogin.cpp
        ${_client_root}/core/models/agentWorkloadReconciliation.cpp
        ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
        ${_client_root}/core/models/protocols/amgptAuthProxyProtocolConfig.cpp
        ${_client_root}/core/models/protocols/openClawCodexProtocolConfig.cpp
    LABELS contract
    INCLUDE_DIRECTORIES ${_client_root}
    TIMEOUT 30
)
