#!/usr/bin/env bash
# Keeps AhShitRim's self-hosted runner (actions-runner.service, see setup-runner.sh) running when
# it should be, and restarts it when it's stuck. setup-runner.sh installs it as
# ~/.local/bin/actions-runner-healthcheck, run every 3 minutes by actions-runner-healthcheck.timer.
# DRY_RUN=1 only reports.
#
# - While someone holds the machine (a file in cue-gpu's holders directory: a stream, or a game
#   at the monitor), the runner is meant to be stopped, so nothing is checked.
# - Otherwise it must be running: a stopped or failed service is started.
# - It must be online on GitHub: one that has been up for a few minutes and is still offline is
#   restarted.
# - The frozen case, as on sandbox (its runner-healthcheck): the runner's own log goes quiet for
#   long stretches while it's healthy and idle, so it's no signal. GitHub's view is: the runner
#   says it's idle (busy=false) while a job for it has been queued far longer than a pickup takes
#   (seconds). Then it's restarted.
#
# Not covered: a runner that hangs in the middle of a job reports busy=true; the workflow's step
# and job timeouts end that.
#
# Needs gh logged in on this machine with read access to the repo's runners and Actions
# (docs/DEV_SETUP.md, "CI").
set -uo pipefail

REPO=rackmarten/AhShitRim
RUNNER_NAME="${RUNNER_NAME:-$(hostname)}"
LABEL=msvc-wine
UNIT=actions-runner.service
STALE_SECONDS=180
HOLDER_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/cue-gpu/holders"

log() { printf '%s\n' "$*"; }

restart() {
    if [ -n "${DRY_RUN:-}" ]; then
        log "DRY_RUN set, not touching $UNIT."
    elif systemctl --user restart "$UNIT"; then
        log "Restarted $UNIT."
    else
        log "ERROR: restarting $UNIT failed."
    fi
}

if [ -d "$HOLDER_DIR" ] && [ -n "$(ls -A "$HOLDER_DIR")" ]; then
    log "OK: paused, the machine is held by $(ls -A "$HOLDER_DIR" | tr '\n' ' ')"
    exit 0
fi

if ! systemctl --user is-enabled --quiet "$UNIT"; then
    log "OK: $UNIT is disabled, so it's meant to be off"
    exit 0
fi

if ! systemctl --user is-active --quiet "$UNIT"; then
    log "DOWN: $UNIT isn't running and nobody holds the machine. Starting it."
    restart
    exit 0
fi

# Seconds since the service (re)started: a fresh one gets time to connect before it's judged.
started=$(systemctl --user show "$UNIT" -p ActiveEnterTimestampMonotonic --value)
uptime_s=$(( $(cut -d' ' -f1 /proc/uptime | cut -d. -f1) - started / 1000000 ))

runner=$(gh api "repos/$REPO/actions/runners" \
    --jq ".runners[] | select(.name == \"$RUNNER_NAME\") | \"\\(.status) \\(.busy)\"" 2>&1)
if [ $? -ne 0 ] || [ -z "$runner" ]; then
    log "WARN: couldn't read the runner's status from GitHub: ${runner:-not listed}"
    exit 0
fi
read -r status busy <<<"$runner"

if [ "$status" != online ]; then
    if [ "$uptime_s" -gt "$STALE_SECONDS" ]; then
        log "OFFLINE: $UNIT has been up ${uptime_s}s but GitHub shows the runner $status. Restarting it."
        restart
    else
        log "OK: $UNIT started ${uptime_s}s ago, still connecting"
    fi
    exit 0
fi

if [ "$busy" = true ]; then
    log "OK: running a job"
    exit 0
fi

# The oldest job that's waiting for this runner's label.
now=$(date -u +%s)
oldest=0
for run in $(gh api "repos/$REPO/actions/runs?status=queued&per_page=100" --jq '.workflow_runs[].id' 2>/dev/null); do
    created=$(gh api "repos/$REPO/actions/runs/$run/jobs?filter=latest&per_page=100" \
        --jq "[.jobs[] | select(.status == \"queued\" and (.labels | index(\"$LABEL\"))) | .created_at] | sort | first // empty" 2>/dev/null)
    [ -n "$created" ] || continue
    age=$(( now - $(date -u -d "$created" +%s) ))
    [ "$age" -gt "$oldest" ] && oldest=$age
done

if [ "$oldest" -gt "$STALE_SECONDS" ] && [ "$uptime_s" -gt "$STALE_SECONDS" ]; then
    log "FROZEN: the runner says it's idle, but a job for it has been queued ${oldest}s. Restarting it."
    restart
else
    log "OK: idle, oldest queued job ${oldest}s"
fi
