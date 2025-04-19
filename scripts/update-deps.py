#!/usr/bin/python3
import hashlib
import json
import os
import re
import urllib.request


def get_latest(url, pattern):
    if type(pattern) is str:
        pattern = re.compile(pattern)
    res = urllib.request.urlopen(url)
    content = res.read()
    res.close()
    links = re.findall(r'<a href[^>]*>([^<]*)</a>', content.decode())
    matches = []
    for link in links:
        m = re.match(pattern, link)
        if m:
            matches.append(m)
    if not matches:
        raise ValueError
    matches.sort(key = lambda m: [int(v) for v in m.group(1).split('.')])
    return matches[-1].group(1), matches[-1].group(0)


def get_sha256(url):
    res = urllib.request.urlopen(url)
    size = int(res.getheader('Content-Length'))
    sha256 = hashlib.sha256()
    loaded = 0
    while True:
        buff = res.read(65536)
        if not buff:
            break
        sha256.update(buff)
        loaded += len(buff)
    if loaded != size:
        raise RuntimeError("Incomplete file: size %d != %d" % (loaded, size))
    return sha256.hexdigest()


def get_latest_vscode(name):
    url = 'https://update.code.visualstudio.com/api/update/' + name + '/stable/latest'
    res = urllib.request.urlopen(url)
    dat = json.load(res)
    res.close()
    return dat['version'], dat['url'], dat['sha256hash']


def get_latest_binutils_tar():
    url = 'https://ftp.gnu.org/gnu/binutils/'
    ver, tarname = get_latest(url, r'binutils-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + tarname


def get_latest_gcc_tar():
    url = 'https://ftp.gnu.org/gnu/gcc/'
    _, versiondir = get_latest(url, r'gcc-([0-9]+(:?\.[0-9]+)+)/')
    ver, tarname = get_latest(url + versiondir, r'gcc-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + versiondir + tarname


def get_latest_glibc_tar():
    url = 'https://ftp.gnu.org/gnu/glibc/'
    ver, tarname = get_latest(url, r'glibc-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + tarname


def get_latest_gmp_tar():
    url = 'https://ftp.gnu.org/gnu/gmp/'
    ver, tarname = get_latest(url, r'gmp-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + tarname


def get_latest_linux_tar():
    url = 'https://cdn.kernel.org/pub/linux/kernel/'
    _, versiondir = get_latest(url, r'v([0-9]+).x/')
    ver, tarname = get_latest(url + versiondir, r'linux-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + versiondir + tarname


def get_latest_mpc_tar():
    url = 'https://ftp.gnu.org/gnu/mpc/'
    ver, tarname = get_latest(url, r'mpc-([0-9]+(:?\.[0-9]+)+).tar.gz')
    return ver, url + tarname


def get_latest_mpfr_tar():
    url = 'https://ftp.gnu.org/gnu/mpfr/'
    ver, tarname = get_latest(url, r'mpfr-([0-9]+(:?\.[0-9]+)+).tar.xz')
    return ver, url + tarname


def update_latest_vscode_prod_ver():
    url = 'https://update.code.visualstudio.com/api/releases/stable'
    res = urllib.request.urlopen(url)
    dat = json.load(res)
    res.close()
    prod_ver = dat[0]
    version_txt_path = os.path.join(os.path.dirname(__file__), "../version.txt")
    fo = open(version_txt_path, "w")
    fo.write(str(prod_ver).strip() + "\n")
    fo.close()


def update_deps_info():
    verdict = {}
    latest = {}
    deps_path = os.path.join(os.path.dirname(__file__), "deps.sh")
    if os.path.exists(deps_path):
        fo = open(deps_path, "r")
        while True:
            line = fo.readline()
            if not line:
                break
            k, v = line.split("=", 1)
            k = k.strip()
            v = v.strip()
            verdict[k] = v
        fo.close()

    print('updating binutils...')
    latest['binutils_filename'] = "binutils-src.tar.xz"
    latest['binutils_version'], latest['binutils_url'] = get_latest_binutils_tar()
    latest['binutils_sha256'] = verdict['binutils_sha256'] if verdict.get('binutils_version') == latest['binutils_version'] else get_sha256(latest['binutils_url'])

    print('updating gcc...')
    latest['gcc_filename'] = "gcc-src.tar.xz"
    latest['gcc_version'], latest['gcc_url'] = get_latest_gcc_tar()
    latest['gcc_sha256'] = verdict['gcc_sha256'] if verdict.get('gcc_version') == latest['gcc_version'] else get_sha256(latest['gcc_url'])

    print('updating glibc...')
    latest['glibc_filename'] = "glibc-src.tar.xz"
    latest['glibc_version'], latest['glibc_url'] = get_latest_glibc_tar()
    latest['glibc_sha256'] = verdict['glibc_sha256'] if verdict.get('glibc_version') == latest['glibc_version'] else get_sha256(latest['glibc_url'])

    print('updating gmp...')
    latest['gmp_filename'] = "gmp-src.tar.xz"
    latest['gmp_version'], latest['gmp_url'] = get_latest_gmp_tar()
    latest['gmp_sha256'] = verdict['gmp_sha256'] if verdict.get('gmp_version') == latest['gmp_version'] else get_sha256(latest['gmp_url'])

    print('updating linux...')
    latest['linux_filename'] = "linux-src.tar.xz"
    latest['linux_version'], latest['linux_url'] = get_latest_linux_tar()
    latest['linux_sha256'] = verdict['linux_sha256'] if verdict.get('linux_version') == latest['linux_version'] else get_sha256(latest['linux_url'])

    print('updating mpc...')
    latest['mpc_filename'] = "mpc-src.tar.gz"
    latest['mpc_version'], latest['mpc_url'] = get_latest_mpc_tar()
    latest['mpc_sha256'] = verdict['mpc_sha256'] if verdict.get('mpc_version') == latest['mpc_version'] else get_sha256(latest['mpc_url'])

    print('updating mpfr...')
    latest['mpfr_filename'] = "mpfr-src.tar.xz"
    latest['mpfr_version'], latest['mpfr_url'] = get_latest_mpfr_tar()
    latest['mpfr_sha256'] = verdict['mpfr_sha256'] if verdict.get('mpfr_version') == latest['mpfr_version'] else get_sha256(latest['mpfr_url'])

    print('updating vscode_cli_arm64...')
    latest['vscode_cli_arm64_filename'] = "vscode-cli-arm64.tar.gz"
    latest['vscode_cli_arm64_version'], latest['vscode_cli_arm64_url'], latest['vscode_cli_arm64_sha256'] = get_latest_vscode('cli-linux-arm64')

    print('updating vscode_cli_armhf...')
    latest['vscode_cli_armhf_filename'] = "vscode-cli-armhf.tar.gz"
    latest['vscode_cli_armhf_version'], latest['vscode_cli_armhf_url'], latest['vscode_cli_armhf_sha256'] = get_latest_vscode('cli-linux-armhf')

    print('updating vscode_cli_x64...')
    latest['vscode_cli_x64_filename'] = "vscode-cli-x64.tar.gz"
    latest['vscode_cli_x64_version'], latest['vscode_cli_x64_url'], latest['vscode_cli_x64_sha256'] = get_latest_vscode('cli-linux-x64')

    print('updating vscode_server_arm64...')
    latest['vscode_server_arm64_filename'] = "vscode-srv-arm64.tar.gz"
    latest['vscode_server_arm64_version'], latest['vscode_server_arm64_url'], latest['vscode_server_arm64_sha256'] = get_latest_vscode('server-linux-arm64')

    print('updating vscode_server_armhf...')
    latest['vscode_server_armhf_filename'] = "vscode-srv-armhf.tar.gz"
    latest['vscode_server_armhf_version'], latest['vscode_server_armhf_url'], latest['vscode_server_armhf_sha256'] = get_latest_vscode('server-linux-armhf')

    print('updating vscode_server_x64...')
    latest['vscode_server_x64_filename'] = "vscode-srv-x64.tar.gz"
    latest['vscode_server_x64_version'], latest['vscode_server_x64_url'], latest['vscode_server_x64_sha256'] = get_latest_vscode('server-linux-x64')

    fo = open(deps_path, "w")
    for k in sorted(latest.keys()):
        fo.write("%s=%s\n" % (k, latest[k]))
    fo.close()

    print('updating vscode product version...')
    update_latest_vscode_prod_ver()


def main():
    update_deps_info()


if __name__ == "__main__":
    main()
