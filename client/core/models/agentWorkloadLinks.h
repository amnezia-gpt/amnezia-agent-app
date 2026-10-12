#ifndef AGENTWORKLOADLINKS_H
#define AGENTWORKLOADLINKS_H

#include <QByteArray>
#include <QString>
#include <optional>

#include "core/utils/errorCodes.h"

namespace amnezia
{
    inline constexpr qsizetype AgentWorkloadLinksMaxBytes = 8192;

    enum class AgentWorkloadLinksError {
        None,
        InvalidTarget,
        CommandFailed,
        Unsupported,
        OutputTooLarge,
        InvalidResponse,
    };

    struct AgentWorkloadLinksResult
    {
        AgentWorkloadLinksError error = AgentWorkloadLinksError::InvalidTarget;
        ErrorCode transportError = ErrorCode::NoError;
        std::optional<QString> webUi;
    };

    bool isSafeAgentWorkloadWebUiHref(const QString &href);
    AgentWorkloadLinksResult parseAgentWorkloadLinks(const QByteArray &payload, int exitCode);
}

#endif
