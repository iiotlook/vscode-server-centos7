#!/bin/bash
set -euo pipefail

if [ "$#" != "2" ]; then
    echo "Usage: $0 <version> <tarball>" >&2
    exit 1
fi

VERSION=$1
TARBALL=$(realpath "$2")
CONTAINERS=""
STEP="Initialization"
CODE_CMD='code --disable-gpu --no-sandbox'

cd "$(dirname "$0")"

INFO() {
    echo '' >&2
    echo '============================' >&2
    echo "$@" >&2
    echo '============================' >&2
}

ERROR() {
    echo '' >&2
    echo '============================' >&2
    echo 'ERROR:' "$@" >&2
    echo '============================' >&2
}

STEP_PASS() {
    echo '' >&2
    echo '============================' >&2
    echo "$STEP: PASS" >&2
    echo '============================' >&2
}

STEP_FAIL() {
    echo '' >&2
    echo '============================' >&2
    echo "$STEP: FAIL" >&2
    echo '============================' >&2
}

SRV_ROOT() {
    local ret=0
    echo $'\n'"[SRV_ROOT]$(printf ' %q' "$@")" >&2
    docker exec --user root --workdir /root -i vscode-srv "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [SRV_ROOT]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

SRV_USER() {
    local ret=0
    echo $'\n'"[SRV_USER]$(printf ' %q' "$@")" >&2
    docker exec --user vscode --workdir /home/vscode -i vscode-srv "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [SRV_USER]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

CLI_ROOT() {
    local ret=0
    echo $'\n'"[CLI_ROOT]$(printf ' %q' "$@")" >&2
    docker exec --user root --workdir /root -i vscode-cli "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [CLI_ROOT]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

CLI_ROOT_BG() {
    local ret=0
    echo $'\n'"[CLI_ROOT_BG]$(printf ' %q' "$@")" >&2
    docker exec --user root --workdir /root -d vscode-cli "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [CLI_ROOT_BG]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

CLI_USER() {
    local ret=0
    echo $'\n'"[CLI_USER]$(printf ' %q' "$@")" >&2
    docker exec --user vscode --workdir /home/vscode -i vscode-cli "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [CLI_USER]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

CLI_USER_BG() {
    local ret=0
    echo $'\n'"[CLI_USER_BG]$(printf ' %q' "$@")" >&2
    docker exec --user vscode --workdir /home/vscode -d vscode-cli "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"  [CLI_USER_BG]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

LOCAL() {
    local ret=0
    echo $'\n'"[LOCAL]$(printf ' %q' "$@")" >&2
    "$@" || {
        ret=$?
        ERROR "failed command: "$'\n'"[LOCAL]$(printf ' %q' "$@")"
        STEP_FAIL
    }
    return $ret
}

onexit() {
    local ret=$?

    STEP="Clean up"

    if [ -n "$CONTAINERS" ]; then
        LOCAL docker stop -t0 -- $CONTAINERS
        LOCAL docker rm -- $CONTAINERS
    fi

    exit $ret
}

trap onexit EXIT

if [ -e ./logs ]; then
    ERROR 'Old ./logs exists. Please remove it before running tests.'
    exit 1
fi

LOCAL docker create --name vscode-srv \
    -e 'DONT_PROMPT_WSL_INSTALL=1' \
    -v './logs/server:/home/vscode/.vscode-server/data/logs' \
    -it centos:7
CONTAINERS="vscode-srv"

LOCAL docker create --name vscode-cli \
    -e 'DONT_PROMPT_WSL_INSTALL=1' \
    -e 'DISPLAY=:0' \
    -v './logs/client:/home/vscode/.config/Code/logs' \
    -it almalinux:9
CONTAINERS="vscode-srv vscode-cli"

LOCAL docker cp "$TARBALL" vscode-srv:/tmp/vscode-server_x64.tar.gz

LOCAL docker start vscode-srv
LOCAL docker start vscode-cli

SRV_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' vscode-srv)
INFO "Server IP: $SRV_IP"

# ============================
STEP="Server Installation"

SRV_ROOT find /etc/yum.repos.d/ -name 'CentOS-*.repo' -exec \
    sed -i -e 's|^mirrorlist=|#mirrorlist=|g' \
    -e 's|^#baseurl=http://mirror.centos.org/centos/$releasever|baseurl=http://vault.centos.org/7.9.2009|g' \
    -e 's|^#baseurl=http://mirror.centos.org/$contentdir/$releasever|baseurl=http://vault.centos.org/7.9.2009|g' \
    '{}' +
SRV_ROOT yum makecache
SRV_ROOT yum install -y procps-ng openssh-server
SRV_ROOT useradd -M vscode
SRV_ROOT chown -R vscode:vscode /home/vscode
SRV_ROOT ssh-keygen -A
SRV_ROOT /usr/sbin/sshd

SRV_USER mkdir -p /home/vscode/.ssh /home/vscode/.vscode-server
SRV_USER tar xzf /tmp/vscode-server_x64.tar.gz -C /home/vscode/.vscode-server --strip-components 1
SRV_USER /home/vscode/.vscode-server/code-latest --patch-now
SRV_USER /home/vscode/.vscode-server/code-latest --version
SRV_USER sh -c "printf '[ $(date) ]\n\n> Hello from CentOS 7!\n\n(version: $VERSION)\n' > /home/vscode/hello.txt"

STEP_PASS

# ============================
STEP="Client Installation"

CLI_ROOT sh -c "echo '$SRV_IP' CentOS_7 >> /etc/hosts"
CLI_ROOT curl -fLo '/root/code.x86_64.rpm' "https://update.code.visualstudio.com/${VERSION}/linux-rpm-x64/stable"
CLI_ROOT sh -c 'echo fastestmirror=1 >> /etc/dnf/dnf.conf'
CLI_ROOT yum makecache
CLI_ROOT yum install -y procps-ng openssh-clients xorg-x11-server-Xvfb gnome-screenshot /root/code.x86_64.rpm
CLI_ROOT useradd -M vscode
CLI_ROOT chown -R vscode:vscode /home/vscode
CLI_ROOT_BG Xvfb -screen 0 1200x800x24 -nolisten tcp
LOCAL sleep 3

CLI_USER $CODE_CMD --install-extension 'ms-vscode-remote.remote-ssh'
CLI_USER sh -c 'echo '\''{"security.workspace.trust.enabled": false, "telemetry.telemetryLevel": "off", "update.mode": "none"}'\'' > /home/vscode/.config/Code/User/settings.json'
CLI_USER ssh-keygen -t rsa -b 4096 -P '' -f /home/vscode/.ssh/id_rsa
CLI_USER cat /home/vscode/.ssh/id_rsa.pub | SRV_USER tee /home/vscode/.ssh/authorized_keys
CLI_USER sh -c 'ssh-keyscan -t rsa CentOS_7 > /home/vscode/.ssh/known_hosts'
CLI_USER ssh CentOS_7 echo 'SSH OK'

STEP_PASS

# ============================
STEP="Client Test"

CLI_USER_BG $CODE_CMD --remote 'ssh-remote+CentOS_7' /home/vscode/
LOCAL sleep 30
CLI_USER env GNOME_SCREENSHOT_FORCE_FALLBACK=1 gnome-screenshot --file=/home/vscode/.config/Code/logs/screen_00.png
CLI_USER $CODE_CMD --reuse-window --remote "ssh-remote+CentOS_7" /home/vscode/hello.txt
CLI_USER env GNOME_SCREENSHOT_FORCE_FALLBACK=1 gnome-screenshot --file=/home/vscode/.config/Code/logs/screen_01.png
CLI_USER pgrep -x code
CLI_USER grep -Frl 'Server setup complete' /home/vscode/.config/Code/logs | LOCAL grep 'Remote - SSH[0-9.]*\.log$'
CLI_USER grep -Frl 'Extension host agent started.' /home/vscode/.config/Code/logs | LOCAL grep 'Remote - SSH[0-9.]*\.log$'
CLI_USER grep -Frl 'New connection established.' /home/vscode/.config/Code/logs | LOCAL grep 'Remote - SSH[0-9.]*\.log$'

STEP_PASS

# ============================
STEP="Server Test"

SRV_USER pgrep -f '^/home/vscode/.vscode-server/cli/servers/Stable-[0-9a-f]*/server/node '
SRV_USER pgrep -f '^/home/vscode/.vscode-server/code-[0-9a-f]* command-shell '
SRV_USER grep -F 'command-shell' /home/vscode/.vscode-server/patch.log
SRV_USER grep -Frl 'Extension host agent started.' /home/vscode/.vscode-server/data/logs | LOCAL grep '/remoteagent[0-9.]*\.log$'
SRV_USER grep -Frl 'New connection established.' /home/vscode/.vscode-server/data/logs | LOCAL grep '/remoteagent[0-9.]*\.log$'

STEP_PASS

# ============================
INFO "All Tests Passed."

exit 0

