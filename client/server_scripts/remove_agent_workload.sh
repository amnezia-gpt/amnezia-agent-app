set -eu

workload=@@WORKLOAD@@
case "$workload" in
    amgpt-device-gateway)
        container_name=amnezia-amgpt-device-gateway
        owned_volumes='amnezia-amgpt-device-gateway-state'
        image=amneziavpn/agent-workload-amgpt-auth-proxy:dev
        ;;
    openclaw-codex)
        container_name=amnezia-openclaw-codex
        owned_volumes='amnezia-agent-runtime-openclaw-state amnezia-agent-runtime-workspace amnezia-agent-runtime-codex-home amnezia-agent-runtime-codex-workspace'
        image=amneziavpn/agent-workload-openclaw-codex:dev
        ;;
    *) exit 70 ;;
esac

listed() {
    printf '%s\n' "$2" | grep -Fxq -- "$1"
}

container_ids() {
    docker container ls -aq --no-trunc --filter "name=^/${container_name}$"
}

ids=$(container_ids) || exit 71
count=$(printf '%s\n' "$ids" | awk 'NF { count++ } END { print count + 0 }')
[ "$count" -le 1 ] || exit 72
if [ -n "$ids" ]; then
    owner=$(docker container inspect --format '{{ index .Config.Labels "org.amnezia.amgpt.deployment.managed-by" }}' "$ids") || exit 73
    actual_workload=$(docker container inspect --format '{{ index .Config.Labels "org.amnezia.amgpt.deployment.workload" }}' "$ids") || exit 73
    [ "$owner" = amnezia-agent-app ] && [ "$actual_workload" = "$workload" ] || exit 74
    docker container rm --force "$ids" >/dev/null 2>&1 || exit 75
fi

remove_volume() {
    volumes=$(docker volume ls --format '{{.Name}}') || exit 76
    if listed "$1" "$volumes"; then
        # No force: another container's reference must block deletion.
        docker volume rm "$1" >/dev/null 2>&1 || exit 77
    fi
}

# Explicit uninstall purges private state even after a previous partial removal.
for volume in $owned_volumes; do
    remove_volume "$volume"
done

# Shared resources survive while any container, including a stopped one, uses them.
digest_volume=amnezia-agent-codex-app-socket
references=$(docker container ls -aq --filter "volume=$digest_volume") || exit 78
if [ -z "$references" ]; then
    remove_volume "$digest_volume"
fi
network=amnezia-agent-workloads
references=$(docker container ls -aq --filter "network=$network") || exit 78
if [ -z "$references" ]; then
    networks=$(docker network ls --format '{{.Name}}') || exit 79
    if listed "$network" "$networks"; then
        docker network rm "$network" >/dev/null 2>&1 || exit 80
    fi
fi

image_ids=$(docker image ls -q "$image") || exit 81
if [ -n "$image_ids" ]; then
    references=$(docker container ls -aq --filter "ancestor=$image") || exit 78
    if [ -z "$references" ]; then
        docker image rm "$image" >/dev/null 2>&1 || exit 82
        remaining=$(docker image ls -q "$image") || exit 81
        [ -z "$remaining" ] || exit 83
    fi
fi

remaining=$(container_ids) || exit 71
[ -z "$remaining" ] || exit 84
volumes=$(docker volume ls --format '{{.Name}}') || exit 76
for volume in $owned_volumes; do
    if listed "$volume" "$volumes"; then
        exit 85
    fi
done
printf 'AMNEZIA_AGENT_REMOVE_COMPLETE\n'
