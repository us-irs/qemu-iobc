# QEMU for ISIS-OBC

QEMU with support for the ISIS On-Board Computer used in SOURCE.

See `README.orig.rst` for the original QEMU readme.


## Building QEMU

For a general overview of how to build QEMU on Linux and which dependencies are required, see https://wiki.qemu.org/Hosts/Linux#Building_QEMU_for_Linux.
In the following is a short overview.
Note that you do not need to fully build QEMU, as we only need ARM system emulation (i.e. all other machines can be skipped).

It is recommended to build QEMU in a separate build directory.
In the following, we assume that this directory is `./build/` in the source directory.
If you are building QEMU for the first time, you can create this directory via
```
mkdir build && cd build
```
From that, you then need to configure the QEMU build system.
For this, it is sufficient to only specify `arm-softmmu` as emulation target.
```
../configure --target-list=arm-softmmu
```
Finally build QEMU via
```
make -j `nproc`
```
Note that you only need to re-run the latest step (i.e. `make` from inside the build directory) when you make changes to the source-code and want to rebuild.


## Running QEMU for ISIS-OBC

From the build directory, run
```sh
./arm-softmmu/qemu-system-arm -m isis-obc -monitor stdio \
    -bios ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin
```
Due to the current board configuration, only the `sdram` image is supported.

### Controlling QEMU via QMP

QEMU can be controlled via the QEMU Machine Protocol (QMP).
This is can be useful for fully automated testing, as it, for example, allows the QEMU machine and simulation framework to be initialized and all device emulators to connect before sending a start command via QMP to actually start the simulated processor.
This way it can be ensured that no initial communication between machine and devices is lost.

For this, you will need to add the `-qmp` and `-S` flags, e.g. like this:
```sh
./arm-softmmu/qemu-system-arm -m isis-obc -monitor stdio \
    -bios ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
    -qmp unix:/tmp/qemu,server -S
```
This opens a Unix domain socket at `/tmp/qemu`.
Commands can be automatically sent to this socket, e.g. from the simulation and testing framework.
Specifying the `-S` option causes QEMU to initially pause the emulation, otherwise it would start immediately.
Once the simulation framework is fully connected, it can then send a QMP `cont` command to continue emulation.
