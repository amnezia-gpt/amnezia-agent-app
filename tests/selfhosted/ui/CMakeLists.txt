set(SELFHOSTED_UI_TEST_TARGET selfhosted_ui_models_test)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

set(_client_root "${CMAKE_CURRENT_LIST_DIR}/../../../client")

add_executable(${SELFHOSTED_UI_TEST_TARGET}
    containers_model_test.cpp
    ${_client_root}/core/utils/containerEnum.h
    ${_client_root}/core/utils/protocolEnum.h
    ${_client_root}/ui/models/containersModel.cpp
    ${_client_root}/core/utils/containers/containerUtils.cpp
    ${_client_root}/core/protocols/protocolUtils.cpp
    ${_client_root}/core/models/containerConfig.cpp
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
)

target_compile_features(${SELFHOSTED_UI_TEST_TARGET} PRIVATE cxx_std_17)
target_include_directories(${SELFHOSTED_UI_TEST_TARGET} PRIVATE ${_client_root})
target_link_libraries(${SELFHOSTED_UI_TEST_TARGET} PRIVATE Qt6::Core Qt6::Test)
set_target_properties(${SELFHOSTED_UI_TEST_TARGET} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

add_test(NAME selfhosted.ui COMMAND ${SELFHOSTED_UI_TEST_TARGET})
set_tests_properties(selfhosted.ui PROPERTIES
    LABELS "unit"
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(TARGET amnezia-tests)
    add_dependencies(amnezia-tests ${SELFHOSTED_UI_TEST_TARGET})
endif()
