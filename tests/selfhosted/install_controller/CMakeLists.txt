set(SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET selfhosted_install_controller_test)

find_package(Qt6 REQUIRED COMPONENTS Core Core5Compat Concurrent Gui Network Test)
find_package(OpenSSL REQUIRED)
find_package(libssh REQUIRED)

set(_client_root "${CMAKE_CURRENT_LIST_DIR}/../../../client")
set(_common_root "${CMAKE_CURRENT_LIST_DIR}/../../../common")

add_executable(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET}
    install_controller_test.cpp
    ${_client_root}/core/repositories/secureAppSettingsRepository.cpp
    ${_client_root}/core/repositories/secureAppSettingsRepository.h
    ${_client_root}/secureQSettings.cpp
    ${_client_root}/secureQSettings.h
    ${_common_root}/crypto/cryptoUtils.cpp
    ${_client_root}/core/utils/containerEnum.h
    ${_client_root}/core/utils/routeModes.h
    ${_client_root}/core/utils/protocolEnum.h
    ${_client_root}/core/utils/errorCodes.h
    ${_client_root}/server_scripts/serverScripts.qrc
    ${_client_root}/core/controllers/selfhosted/installController.h
    ${_client_root}/core/controllers/selfhosted/installController.cpp

    ${_client_root}/core/installers/installerBase.cpp
    ${_client_root}/core/installers/awgInstaller.cpp
    ${_client_root}/core/installers/wireguardInstaller.cpp
    ${_client_root}/core/installers/openvpnInstaller.cpp
    ${_client_root}/core/installers/xrayInstaller.cpp
    ${_client_root}/core/installers/torInstaller.cpp
    ${_client_root}/core/installers/sftpInstaller.cpp
    ${_client_root}/core/installers/socks5Installer.cpp
    ${_client_root}/core/installers/mtProxyInstaller.cpp
    ${_client_root}/core/installers/telemtInstaller.cpp

    ${_client_root}/core/configurators/configuratorBase.cpp
    ${_client_root}/core/configurators/configuratorBase.h
    ${_client_root}/core/configurators/wireguardConfigurator.cpp
    ${_client_root}/core/configurators/xrayConfigurator.h
    ${_client_root}/core/configurators/xrayConfigurator.cpp
    ${_client_root}/core/utils/containers/containerUtils.cpp
    ${_client_root}/core/protocols/protocolUtils.cpp
    ${_client_root}/core/utils/networkUtilities.cpp
    ${_client_root}/core/utils/selfhosted/sshSession.cpp
    ${_client_root}/core/utils/selfhosted/sshSession.h
    ${_client_root}/core/utils/selfhosted/sshClient.cpp
    ${_client_root}/core/utils/selfhosted/scriptsRegistry.cpp
    ${_client_root}/core/utils/utilities.cpp
    ${_client_root}/core/models/containerConfig.cpp
    ${_client_root}/core/models/agentWorkloadApply.cpp
    ${_client_root}/core/models/agentWorkloadLogin.cpp
    ${_client_root}/core/models/selfhosted/selfHostedAdminServerConfig.cpp
    ${_client_root}/core/models/agentWorkloadDeploymentSpec.cpp
    ${_client_root}/core/models/agentWorkloadReconciliation.cpp
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
)

target_compile_features(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE cxx_std_17)
target_compile_definitions(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE
    DEV_AGW_ENDPOINT="https://gateway.example.invalid"
)

if(WIN32)
    target_compile_definitions(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE _WINSOCKAPI_)
endif()

target_include_directories(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE
    ${_client_root}
    ${_common_root}
    ${_common_root}/logger
    ${_common_root}/crypto
    ${PROJECT_BINARY_DIR}/client
    ${PROJECT_BINARY_DIR}/3rd/qtkeychain/qtkeychain
)

target_link_libraries(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE
    Qt6::Core
    Qt6::Core5Compat
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Network
    Qt6::Test
    OpenSSL::SSL
    OpenSSL::Crypto
    ssh::ssh
    qt6keychain
)

set_target_properties(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET} PRIVATE -Wl,--gc-sections)
    endif()
endif()

add_test(NAME selfhosted.install_controller COMMAND ${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET})
set_tests_properties(selfhosted.install_controller PROPERTIES
    LABELS "unit"
    TIMEOUT 30
)

if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests ${SELFHOSTED_INSTALL_CONTROLLER_TEST_TARGET})
endif()
