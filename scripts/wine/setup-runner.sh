#!/usr/bin/env bash
# Sets up a self-hosted GitHub Actions runner for AhShitRim's CI on a Linux machine that already
# builds with scripts/wine/setup-msvc-wine.sh (arcade). The runner runs as this user, as the
# systemd user service actions-runner.service, labelled msvc-wine, and keeps its checkout and
# build folders between jobs. See docs/DEV_SETUP.md, "CI".
#
# The registration token comes on stdin (it's short-lived and needs a repo admin), e.g. from the
# desktop:
#
#   gh api -X POST repos/rackmarten/AhShitRim/actions/runners/registration-token --jq .token \
#       | ssh arcade '~/AhShitRim-cold/scripts/wine/setup-runner.sh'
#
# Re-running it on a registered runner skips the registration and just reinstalls the service.
#
# Settings (environment):
#   RUNNER_DIR      where the runner goes (default ~/actions-runner)
#   RUNNER_VERSION  an actions/runner release without the v (default: the latest)
#   RUNNER_NAME     the runner's name on GitHub (default: this host's name)
set -euo pipefail

REPO_URL=https://github.com/rackmarten/AhShitRim
RUNNER_DIR="${RUNNER_DIR:-$HOME/actions-runner}"
RUNNER_NAME="${RUNNER_NAME:-$(hostname)}"
UNIT=actions-runner.service

step() { printf '\n== %s\n' "$*"; }

if [ ! -x "$RUNNER_DIR/run.sh" ]; then
    step "download the runner"
    release=$(curl -fsSL "https://api.github.com/repos/actions/runner/releases/${RUNNER_VERSION:+tags/v}${RUNNER_VERSION:-latest}")
    version=$(python3 -c 'import json, sys; print(json.load(sys.stdin)["tag_name"].lstrip("v"))' <<<"$release")
    # The release notes carry each package's SHA-256 between BEGIN/END SHA markers.
    sha=$(python3 -c 'import json, re, sys; print(re.search(r"BEGIN SHA linux-x64 -->([0-9a-f]{64})<", json.load(sys.stdin)["body"]).group(1))' <<<"$release")
    tarball="actions-runner-linux-x64-$version.tar.gz"
    mkdir -p "$RUNNER_DIR"
    curl -fsSL -o "$RUNNER_DIR/$tarball" "https://github.com/actions/runner/releases/download/v$version/$tarball"
    echo "$sha  $RUNNER_DIR/$tarball" | sha256sum -c -
    tar -xzf "$RUNNER_DIR/$tarball" -C "$RUNNER_DIR"
    rm "$RUNNER_DIR/$tarball"
fi

if [ ! -f "$RUNNER_DIR/.runner" ]; then
    step "register with $REPO_URL"
    read -r token || true
    [ -n "${token:-}" ] || { echo "No registration token on stdin (see the comment at the top)" >&2; exit 1; }
    (cd "$RUNNER_DIR" && ./config.sh --unattended --url "$REPO_URL" --token "$token" \
        --name "$RUNNER_NAME" --labels msvc-wine --work _work --replace)
fi

step "systemd user service"
mkdir -p "$HOME/.config/systemd/user"
cat >"$HOME/.config/systemd/user/$UNIT" <<EOF
# Installed by AhShitRim's scripts/wine/setup-runner.sh. arcade's cue-gpu stops it while someone
# streams or plays, and starts it again when nobody holds the machine.
[Unit]
Description=GitHub Actions runner for AhShitRim (msvc-wine)
After=network-online.target

[Service]
WorkingDirectory=$RUNNER_DIR
ExecStart=$RUNNER_DIR/run.sh
# The runner finishes its cleanup on SIGINT; a job in progress is cancelled.
KillSignal=SIGINT
TimeoutStopSec=60
Restart=on-failure
RestartSec=30

[Install]
WantedBy=default.target
EOF
# Linger starts user services at boot, before (or without) a desktop login.
loginctl show-user "$USER" -p Linger | grep -q yes || sudo loginctl enable-linger "$USER"
systemctl --user daemon-reload
systemctl --user enable "$UNIT"

holders="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/cue-gpu/holders"
if [ -d "$holders" ] && [ -n "$(ls -A "$holders")" ]; then
    echo "Someone holds the machine ($(ls -A "$holders" | tr '\n' ' ')): cue-gpu starts the runner when they're done."
else
    systemctl --user restart "$UNIT"
    systemctl --user --no-pager status "$UNIT" | head -5
fi
