import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0
import PageEnum 1.0

import "../Controls2"
import "../Controls2/TextTypes"

PageType {
    id: root

    // AgentWorkloadReconciliationAction values from the typed core model.
    readonly property int actionCreate: 0
    readonly property int actionNoOp: 1
    readonly property int actionStart: 2
    readonly property int actionRecreate: 3
    readonly property int actionConflict: 4
    readonly property int actionUnknown: 5
    readonly property int loginAmgpt: 1
    readonly property int loginPreconditionNone: 0
    readonly property int loginStatePending: 1

    property int currentAction: actionUnknown
    property string stateMessage: qsTr("Inspecting workload state…")
    property int selectedLoginMode: loginAmgpt
    property int loginPrecondition: loginPreconditionNone
    property string loginMessage: ""
    property string verificationUrl: ""
    property string userCode: ""
    property bool operationInProgress: false
    property var savedConfig: ContainersModel.getContainerConfig(ServersUiController.processedContainerIndex)
    property bool isDeviceGateway: savedConfig.container === "amnezia-amgpt-device-gateway"
    property var deploymentConfig: savedConfig[isDeviceGateway ? "amgptdevicegateway" : "openclawcodex"] || ({})

    function actionText() {
        if (currentAction === actionCreate)
            return qsTr("Install workload")
        if (currentAction === actionStart)
            return qsTr("Start workload")
        if (currentAction === actionRecreate)
            return qsTr("Repair or update workload")
        return ""
    }

    function refreshState() {
        InstallController.refreshAgentWorkloadState(ServersUiController.processedServerId,
                                                     ServersUiController.processedContainerIndex)
    }

    function startLogin(mode) {
        selectedLoginMode = mode
        loginPrecondition = loginPreconditionNone
        loginMessage = ""
        verificationUrl = ""
        userCode = ""
        InstallController.startAgentWorkloadLogin(ServersUiController.processedServerId, mode)
    }

    function refreshLoginStatus(mode) {
        if (selectedLoginMode !== mode) {
            verificationUrl = ""
            userCode = ""
        }
        selectedLoginMode = mode
        loginPrecondition = loginPreconditionNone
        InstallController.refreshAgentWorkloadLoginStatus(ServersUiController.processedServerId, mode)
    }

    Connections {
        target: InstallController

        function onServerIsBusy(busy) {
            root.operationInProgress = busy
        }

        function onAgentWorkloadStateRefreshed(action, reason, message) {
            root.currentAction = action
            root.stateMessage = message
        }

        function onAgentWorkloadReconcileFinished(action, planReason, status, applyReason, transportError, message) {
            PageController.showNotificationMessage(message)
            root.refreshState()
        }

        function onAgentWorkloadLifecycleFinished(status, reason, transportError, message) {
            PageController.showNotificationMessage(message)
            root.refreshState()
        }

        function onAgentWorkloadLoginStarted(mode, precondition, operationError, url, code, expiresInSeconds, message) {
            root.selectedLoginMode = mode
            root.loginPrecondition = precondition
            root.verificationUrl = url
            root.userCode = code
            root.loginMessage = message
        }

        function onAgentWorkloadLoginStatusRefreshed(mode, state, authenticated, operationError, message) {
            root.selectedLoginMode = mode
            root.loginMessage = message
            if (state !== root.loginStatePending) {
                root.verificationUrl = ""
                root.userCode = ""
            }
        }
    }

    BackButtonType {
        id: backButton
        enabled: !root.operationInProgress
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + PageController.safeAreaTopMargin
    }

    ListViewType {
        enabled: !root.operationInProgress
        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        header: ColumnLayout {
            width: parent.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 24
                headerText: ContainersModel.getProcessedContainerName()
                descriptionText: qsTr("Independent self-hosted agent workload")
            }
        }

        model: 1

        delegate: ColumnLayout {
            width: ListView.view.width
            spacing: 0

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                text: root.stateMessage
                wrapMode: Text.WordWrap
            }

            LabelTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                visible: root.isDeviceGateway
                text: qsTr("Backend profile: %1").arg(root.deploymentConfig.backend_profile || qsTr("resolved on update"))
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                text: qsTr("Refresh state")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                clickedFunction: root.refreshState
            }

            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: root.currentAction === root.actionCreate
                         || root.currentAction === root.actionStart
                         || root.currentAction === root.actionRecreate
                text: root.actionText()
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                clickedFunction: function() {
                    InstallController.reconcileAgentWorkload(ServersUiController.processedServerId,
                                                             ServersUiController.processedContainerIndex)
                }
            }

            DividerType {
                visible: root.currentAction === root.actionCreate
                         || root.currentAction === root.actionStart
                         || root.currentAction === root.actionRecreate
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: root.currentAction === root.actionNoOp
                text: qsTr("Stop workload")
                clickedFunction: function() {
                    InstallController.stopAgentWorkload(ServersUiController.processedServerId,
                                                        ServersUiController.processedContainerIndex)
                }
            }

            DividerType { visible: root.currentAction === root.actionNoOp }

            LabelTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
                visible: root.isDeviceGateway && root.currentAction === root.actionNoOp
                text: qsTr("Device authorization")
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: root.isDeviceGateway && root.currentAction === root.actionNoOp
                text: qsTr("Sign in with Amnezia GPT")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                clickedFunction: function() { root.startLogin(root.loginAmgpt) }
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12
                visible: root.isDeviceGateway && root.loginMessage.length > 0
                text: root.loginMessage
                wrapMode: Text.WordWrap
            }

            LabelTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                visible: root.isDeviceGateway && root.userCode.length > 0
                text: qsTr("User code: %1").arg(root.userCode)
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: root.isDeviceGateway && root.verificationUrl.length > 0
                text: qsTr("Open authorization page")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                clickedFunction: function() {
                    if (!InstallController.openAgentWorkloadVerificationUrl(root.verificationUrl))
                        PageController.showNotificationMessage(qsTr("Unable to open the authorization page"))
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: root.isDeviceGateway && root.currentAction === root.actionNoOp
                text: qsTr("Refresh Amnezia GPT sign-in status")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                clickedFunction: function() { root.refreshLoginStatus(root.loginAmgpt) }
            }

            DividerType {
                visible: root.isDeviceGateway && root.currentAction === root.actionNoOp
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                text: qsTr("Remove workload")
                textColor: AmneziaStyle.color.vibrantRed
                clickedFunction: function() {
                    var yes = function() {
                        PageController.goToPage(PageEnum.PageDeinstalling)
                        InstallController.removeContainer(ServersUiController.processedServerId,
                                                          ServersUiController.processedContainerIndex)
                    }
                    showQuestionDrawer(
                        qsTr("Remove %1 from server?").arg(ContainersModel.getProcessedContainerName()),
                        qsTr("The workload container will be removed. Its persistent data and the other agent workload are preserved."),
                        qsTr("Continue"), qsTr("Cancel"), yes, function() {})
                }
            }
        }
    }

    Component.onCompleted: refreshState()
}
