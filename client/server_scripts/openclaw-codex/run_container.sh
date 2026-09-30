set -eu

action=@@ACTION@@
container_name=@@CONTAINER_NAME@@
workload=@@WORKLOAD@@
managed_by=@@MANAGED_BY@@
image=@@IMAGE@@
platform=@@PLATFORM@@
network_name=@@NETWORK_NAME@@

on_exit() {
    status=$?
    if [ "$status" -ne 0 ]; then
        printf 'AMNEZIA_AGENT_APPLY_FAILED:%s\n' "$status"
    fi
}
trap on_exit EXIT HUP INT TERM

container_ids() {
    docker container ls -aq --no-trunc --filter "name=^/${container_name}$"
}

guard_owner() {
    ids=$(container_ids)
    count=$(printf '%s\n' "$ids" | awk 'NF { count++ } END { print count + 0 }')
    [ "$count" -eq 1 ] || return 70
    actual_owner=$(docker container inspect --format '{{ index .Config.Labels "org.amnezia.amgpt.deployment.managed-by" }}' "$ids")
    actual_workload=$(docker container inspect --format '{{ index .Config.Labels "org.amnezia.amgpt.deployment.workload" }}' "$ids")
    [ "$actual_owner" = "$managed_by" ] || return 71
    [ "$actual_workload" = "$workload" ] || return 72
}

wait_healthy() {
    attempts=@@HEALTH_ATTEMPTS@@
    while [ "$attempts" -gt 0 ]; do
        health=$(docker container inspect --format '{{if .State.Running}}{{with .State.Health}}{{.Status}}{{else}}healthy{{end}}{{else}}stopped{{end}}' "$container_name")
        [ "$health" = healthy ] && return 0
        [ "$health" = stopped ] && return 74
        attempts=$((attempts - 1))
        sleep 1
    done
    printf 'AMNEZIA_AGENT_APPLY_HEALTH_TIMEOUT\n'
    return 75
}

if [ "$action" = start ]; then
    guard_owner
    docker container start "$container_name" >/dev/null 2>&1
    wait_healthy
else
    docker network inspect "$network_name" >/dev/null 2>&1 || docker network create --driver bridge "$network_name" >/dev/null 2>&1
    for volume in @@VOLUME_NAMES@@; do
        docker volume inspect "$volume" >/dev/null 2>&1 || docker volume create "$volume" >/dev/null 2>&1
    done
    docker image pull --platform "$platform" "$image" >/dev/null 2>&1

    if [ "$action" = recreate ]; then
        guard_owner
        docker container rm --force "$container_name" >/dev/null 2>&1
    else
        [ -z "$(container_ids)" ] || exit 76
    fi

    docker container run --detach --pull=never \
        --name "$container_name" \
        --platform "$platform" \
        --network "$network_name" \
@@NETWORK_ALIAS_ARG@@
@@PUBLISH_ARG@@
        --restart @@RESTART_POLICY@@ \
        --stop-timeout @@STOP_TIMEOUT@@ \
@@ENV_ARGS@@
@@LABEL_ARGS@@
@@VOLUME_ARGS@@
@@TMPFS_ARGS@@
@@CAPABILITY_ARGS@@
@@SECURITY_ARGS@@
        --health-interval @@HEALTH_INTERVAL@@ \
        --health-timeout @@HEALTH_TIMEOUT@@ \
        --health-retries @@HEALTH_RETRIES@@ \
        --health-start-period @@HEALTH_START_PERIOD@@ \
        "$image" >/dev/null 2>&1
    wait_healthy
fi

trap - EXIT HUP INT TERM
printf 'AMNEZIA_AGENT_APPLY_APPLIED\n'
