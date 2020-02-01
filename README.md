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
./arm-softmmu/qemu-system-arm -M isis-obc -monitor stdio \
    -bios ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin
```
Due to the current board configuration, only the `sdram` image is supported.

### Controlling QEMU via QMP

QEMU can be controlled via the QEMU Machine Protocol (QMP).
This is can be useful for fully automated testing, as it, for example, allows the QEMU machine and simulation framework to be initialized and all device emulators to connect before sending a start command via QMP to actually start the simulated processor.
This way it can be ensured that no initial communication between machine and devices is lost.

For this, you will need to add the `-qmp` and `-S` flags, e.g. like this:
```sh
./arm-softmmu/qemu-system-arm -M isis-obc -monitor stdio \
    -bios ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
    -qmp unix:/tmp/qemu,server -S
```
This opens a Unix domain socket at `/tmp/qemu`.
Commands can be automatically sent to this socket, e.g. from the simulation and testing framework.
Specifying the `-S` option causes QEMU to initially pause the emulation, otherwise it would start immediately.
Once the simulation framework is fully connected, it can then send a QMP `cont` command to continue emulation.

### Running without Graphics

By default, QEMU tries to launch a window which requires some graphics system (X11/Wayland) to be present.
If this is not available, e.g. in a virtual machine or container, one can add the
```
-serial stdio -monitor none
```
options.
The first option redirects the output of the emulated OBSW/the At91 to the standard I/O stream.
The second option disables the monitor interface, which can be used to pause or exit the emulator or control it in any other way.
For automated testing, this level of control is usually not required, and if so it is available via QMP.
If the monitor interface is needed nontheless (e.g. in an interactive session), one could open a telnet server, e.g. via `-monitor telnet:127.0.0.1:55555,server` and then connect to it via `telnet 127.0.0.1 55555`.
Please refer to the [QEMU documentation](https://qemu.weilnetz.de/doc/qemu-doc.html) for more details.

### Debugging AT91 with QEMU

Debugging a program running in QEMU is fairly easy:
Run QEMU with your preferred options (as detailed above) and add `-s -S`.
Option `-s` enables debugging (by default at port `1234`), option `-S` pauses initial execution until it is explicitly continued via a command in the debugger.
To then debug the running QEMU instance, simply start GDB and connect to `localhost:1234` (`target remote localhost:1234`).
To obtain debug information, load the symbols from the `.elf` file generated when compiling the binary (same directory, `symbol-file _bin/sam9g20/devel/sourceobsw-at91sam9g20_ek-sdram.elf`).

For example, the following commands can be used to
- run QEMU:
  ```
  ./arm-softmmu/qemu-system-arm -M isis-obc -monitor stdio \
      -bios ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin -s -S
  ```

- run GDB:
  ```
  arm-none-eabi-gdb \
      -ex 'target remote localhost:1234' \
      -ex 'symbol-file _bin/sam9g20/devel/sourceobsw-at91sam9g20_ek-sdram.elf'
  ```
in your terminal.
IDEs with GDB support (Eclipse, VS-Code) can be configured accordingly.
