# Shared EON platform pin — the single source of truth for local builds and CI.
set(EON_PLATFORM_REF "v0.1.5")

FetchContent_Declare(eon-platform
    GIT_REPOSITORY https://github.com/eefsowa-source/eon-platform.git
    GIT_TAG        ${EON_PLATFORM_REF}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(eon-platform)
