## A 64-bit (aarch64) bridge for LG webOS

A custom bridge that allows aarch64 binaries to run on existing LG webOS TVs, while
still making use of hardware acceleration from the GPU through shared memory.

### Features:

Implementation of a shared-memory IPC bridge which allows a **64-bit (aarch64)** binary to communicate with
the existing **32-bit (armv7a)** EGL (`libEGL.so`) and GLES libraries (`libGLESv2.so`) on webOS.

In addition, it also forwards surface creation and associated wayland events. This is only necessary because
the surface created by wayland cannot be shared between two seperate processes.

(if someone comes up with a workaround for this, PRs will be welcome and we can ditch all the wayland
code!)

The bridge is bundled as an IPK which bundles all required lib64 libraries needed to run wayland/GLES apps.

This IPK is a prerequisite for other aarch64 apps on webOS. Further libraries required will be considered via PR.

### Minimum supported webOS versions:

It should be roughly compatible with 2021 (webOS 6) and newer devices. This has been tested on webOS 10 and 11.

### QuickStart:

Download the latest IPK from https://github.com/webosbrew/webos-bridge-64to32/releases and install it.

Run the IPK from the menu, it should produce a rotating cube if successful.

Now install your aarch64 IPK e.g. RetroArch from https://github.com/webosbrew/RetroArch/releases

---

## Building it

### Prerequisites

In order to build this project you need two things 1) a armv7a compiler and 2) a aarch64 compiler.

These are available from:
https://github.com/cscd98/buildroot-nc4/releases/

Pass the Makefile CC_32_PATH, CC_64_PATH,
SYSROOT_64 and SYSROOT_32 that your toolchain uses if required.

Then run:

```
make
```

This will produce the following 64-bit libs:

```
out/aarch64/libEGL.so.1
out/aarch64/libGLESv2.so.2
out/aarch64/libgles_bridge_core.so
out/aarch64/libwayland-client.so.0
out/aarch64/libwayland-egl.so.1
```

As well as the following 32-bit binary (deploy to install path in IPK or /media/developer/temp/ on the TV
for debugging or as specified in Makefile's PROXY_INSTALL_PATH):
```
out/armv7a/gles_proxy
```

### Using the bridge with your application:

You will need to use patchelf to change the LD intepreter location
(ld-linux-aarch64.so.1) as well as the rpath of your binary, so it finds the IPK's loader and libs:

```
patchelf --set-interpreter /media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32/lib/ld-linux-aarch64.so.1 ./yourbinary
patchelf --add-rpath /media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32/lib ./yourbinary
```

Then after you have created the IPK - run the script in this repo:

```
./scripts/patch-aarch64-in-ipk.sh <name of IPK>
```

Or alternatively if using: https://github.com/webosbrew/ares-cli-rs

```
ares-package -A arm <folder> -o .
```

This will generate a IPK that can be installed via webOS dev manager and appear on the menu.

### Libraries Provided in IPK

Inspired by webOS 11 this IPK supplies the following libraries normally found in webOS 11 - lib64:

```
ld-linux-aarch64.so.1
libBrokenLocale.so.1
libanl.so.1
libc.so.6
libdl.so.2
libipset.so.13
libipset.so.13.4.0
libm.so.6
libmnl.so.0
libmnl.so.0.2.0
libmvec.so.1
libnsl.so.1
libnss_compat.so.2
libnss_dns.so.2
libnss_files.so.2
libpthread.so.0
libresolv.so.2
librt.so.1
libutil.so.1
```
However, they are not available from a jailed environment so we also package these, except for:

```
libmnl.so.0 libmnl.so.0.2.0 libipset.so.13.4.0
```

It also includes several additional libraries required for running actual aarch64 apps. More can
suggested / submitted via PR.
