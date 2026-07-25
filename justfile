# StarHaven task shortcuts. Run `just` to list everything.
#
# Project conventions:
#   - the configured build directory is `buildDir/`
#   - `.clang-format` and `.clang-tidy` at the repo root are authoritative
#   - generated output is disposable; never hand-edit buildDir/

build_dir := "buildDir"

# Homebrew clang-tidy does not know the macOS SDK location, so it cannot find
# libc++ headers and fails with "'array' file not found". Pass the sysroot
# explicitly. Harmless (empty) on Linux, where xcrun does not exist.
sysroot_args := if os() == "macos" { "--extra-arg=-isysroot --extra-arg=$(xcrun --show-sdk-path)" } else { "" }

# Show available recipes.
default:
    @just --list --unsorted

# Configure the build directory (safe to re-run; reconfigures in place).
setup:
    @if [ -f {{build_dir}}/build.ninja ]; then \
        meson setup {{build_dir}} --reconfigure; \
    else \
        meson setup {{build_dir}}; \
    fi

# Compile everything.
build: setup
    meson compile -C {{build_dir}}

# Run the unit tests, printing logs for any failure.
test: build
    meson test -C {{build_dir}} --print-errorlogs

# Build and run the launcher.
run *args: build
    ./{{build_dir}}/starhaven {{args}}

# Rewrite all owned sources in place per .clang-format.
fmt:
    @find src tools tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
        | xargs -0 clang-format -i --style=file
    @echo "formatted."

# Fail if anything is misformatted. Use this in CI; it never edits files.
fmt-check:
    @find src tools tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
        | xargs -0 clang-format --dry-run --Werror --style=file

# Depends on build so compile_commands.json exists and is current.
# Static analysis over every owned translation unit.
tidy: build
    @find src tools tests -name '*.cpp' -print0 \
        | xargs -0 -n1 clang-tidy -p {{build_dir}} --quiet {{sysroot_args}}

# Static analysis for one file, e.g. `just tidy-file src/core/lod/lod_archive.cpp`.
tidy-file file: build
    clang-tidy -p {{build_dir}} --quiet {{sysroot_args}} {{file}}

# Review the diff afterwards — automated fixes are not always correct.
# Apply clang-tidy's suggested fixes in place.
tidy-fix: build
    @find src tools tests -name '*.cpp' -print0 \
        | xargs -0 -n1 clang-tidy -p {{build_dir}} --quiet --fix {{sysroot_args}}

# Formatting + analysis + tests. The pre-submit gate.
check: fmt-check tidy test

# Implements the engineer skill's "forced fallback" rule against wrap rot.
# Verify the committed wraps still build SDL3 and Catch2 from source.
check-wraps:
    meson setup /tmp/starhaven-wrap-check --force-fallback-for=sdl3,catch2
    meson compile -C /tmp/starhaven-wrap-check
    meson test -C /tmp/starhaven-wrap-check
    rm -rf /tmp/starhaven-wrap-check

# Remove the build directory.
clean:
    rm -rf {{build_dir}}
