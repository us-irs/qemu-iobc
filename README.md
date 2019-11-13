# QEMU for ISIS-OBC

QEMU with support for the ISIS On-Board Computer used in SOURCE.

See `README.orig.rst` for the original QEMU readme.


## Building QEMU

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
