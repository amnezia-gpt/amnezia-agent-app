set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

root := justfile_directory()
preset := env_var_or_default("AMNEZIA_CMAKE_PRESET", "local")
client_preset := env_var_or_default("AMNEZIA_CMAKE_CLIENT_PRESET", "local-client")
local_jobs := env_var_or_default("AMNEZIA_BUILD_JOBS", "2")

check:
    # Configure the graph and run the repository/resource/script contract suite.
    # The contract target is first-party only and never contacts a remote host.
    git -C "{{root}}" diff --check
    cd "{{root}}" && cmake --preset {{preset}} --fresh
    cd "{{root}}" && cmake --build --preset {{preset}} --parallel "{{local_jobs}}" --target selfhosted_contract_test
    cd "{{root}}" && ctest --preset {{preset}} --output-on-failure --no-tests=error -L contract

test:
    git -C "{{root}}" diff --check
    cd "{{root}}" && cmake --preset {{preset}} --fresh
    cd "{{root}}" && cmake --build --preset {{preset}} --parallel "{{local_jobs}}" --target amnezia-tests
    cd "{{root}}" && ctest --preset {{preset}} --output-on-failure --no-tests=error

client-build:
    git -C "{{root}}" diff --check
    cd "{{root}}" && cmake --preset {{client_preset}} --fresh
    cd "{{root}}" && cmake --build --preset {{client_preset}} --parallel "{{local_jobs}}" --target AmneziaVPN

sanitizer-test:
    git -C "{{root}}" diff --check
    cd "{{root}}" && cmake --preset ci-sanitizers --fresh
    cd "{{root}}" && cmake --build --preset ci-sanitizers --parallel "{{local_jobs}}" --target amnezia-tests
    cd "{{root}}" && ctest --preset ci-sanitizers --output-on-failure --no-tests=error
