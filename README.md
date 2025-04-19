# vscode-server-centos7

A specially patched VS Code Server that runs on RHEL/CentOS 7.

## Quick Start

1. Download a tarball from the [Releases](https://github.com/MikeWang000000/vscode-server-centos7/releases) page to your server.

2. Execute the following commands on your server:

    ```bash
    mkdir -p ~/.vscode-server
    tar xzf vscode-server_*.tar.gz -C ~/.vscode-server --strip-components 1
    ~/.vscode-server/code-latest --patch-now
    ```

3. Enjoy!


## Build from Source

1. Install YUM dependencies:

    ```bash
    sudo scripts/yum-install.sh
    ```

2. Download additional dependencies:

    ```bash
    scripts/download-deps.sh
    ```

3. Start the build process:

    ```bash
    make
    ```

    You can specify the value of `ARCH` to build for different architectures:

    ```bash
    make ARCH=x64
    ```

    ```bash
    make ARCH=arm64
    ```

    ```bash
    make ARCH=armhf
    ```

A full build process may take a long time since it involves compiling the glibc and GCC toolchains.


## License

**Licenses for this repository:**  
[GNU General Public License v3.0](./LICENSE.txt)

Microsoft Visual Studio Code product license:  
https://code.visualstudio.com/license

Visual Studio Code - Open Source:  
https://github.com/microsoft/vscode/blob/main/LICENSE.txt

libfastjson:  
https://github.com/rsyslog/libfastjson/blob/master/COPYING

PatchELF:  
https://github.com/NixOS/patchelf/blob/master/COPYING

The GNU C Library:  
https://www.gnu.org/software/libc/manual/html_node/Copying.html

The GNU C++ Library:  
https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html
