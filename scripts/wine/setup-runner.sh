#!/usr/bin/env bash
# Sets up a self-hosted GitHub Actions runner for AhShitRim's CI on a Linux machine that already
# builds with scripts/wine/setup-msvc-wine.sh (arcade). The runner runs as this user, as the
# systemd user service actions-runner.service, labelled msvc-wine, and keeps its checkout and
# build folders between jobs. See docs/DEV_SETUP.md, "CI".
#
# The registration token comes on stdin (it's short-lived and needs a repo admin). Run it from
# a checkout of this repo on that machine (the one setup-msvc-wine.sh ran from), e.g. from the
# desktop:
#
#   gh api -X POST repos/rackmarten/AhShitRim/actions/runners/registration-token --jq .token \
#       | ssh arcade '<checkout>/scripts/wine/setup-runner.sh'
#
# Re-running it on a registered runner skips the registration and just reinstalls the service
# and the healthcheck (from whichever checkout runs it, so use one on main; the runner's own,
# ~/actions-runner/_work/AhShitRim/AhShitRim, is on whatever its last job built).
#
# It also installs the healthcheck (runner-healthcheck.sh) as actions-runner-healthcheck.timer,
# every 3 minutes, and gh for it. gh has to be logged in once, by hand, with a read-only token
# (docs/DEV_SETUP.md, "CI"); until then the check only logs a warning.
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

step "healthcheck"
command -v gh >/dev/null || sudo apt-get install -y -q gh
install -D -m755 "$(dirname "$0")/runner-healthcheck.sh" "$HOME/.local/bin/actions-runner-healthcheck"
cat >"$HOME/.config/systemd/user/actions-runner-healthcheck.service" <<EOF
# Installed by AhShitRim's scripts/wine/setup-runner.sh: see runner-healthcheck.sh there.
[Unit]
Description=Start or restart the AhShitRim runner if it's down or stuck

[Service]
Type=oneshot
ExecStart=%h/.local/bin/actions-runner-healthcheck
EOF
cat >"$HOME/.config/systemd/user/actions-runner-healthcheck.timer" <<EOF
# Installed by AhShitRim's scripts/wine/setup-runner.sh.
[Unit]
Description=Check the AhShitRim runner every 3 minutes

[Timer]
OnBootSec=5min
OnUnitActiveSec=3min

[Install]
WantedBy=timers.target
EOF
systemctl --user daemon-reload
systemctl --user enable --now actions-runner-healthcheck.timer
gh auth status >/dev/null 2>&1 || echo "gh isn't logged in yet: the healthcheck can't see GitHub until it is (docs/DEV_SETUP.md, \"CI\")."
