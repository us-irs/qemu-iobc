# QEMU for ISIS-OBC

QEMU with support for the ISIS On-Board Computer used in SOURCE.

See `README.orig.rst` for the original QEMU readme.


## Building QEMU

For a general overview of how to build QEMU on Linux and which dependencies are required, see https://wiki.qemu.org/Hosts/Linux#Building_QEMU_for_Linux.
In the following is a short overview.
Note that you do not need to fully build QEMU, as we only need ARM system emulation (i.e. all other machines can be skipped).
Install basic build tools if not already done so
```sh
sudo apt-get install build-essential cmake
``` 

Install required libraries
```sh
sudo apt-get install git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev
```

Update the submodules
```sh
git submodule init
git submodule update
```

It is recommended to build QEMU in a separate build directory.
In the following, we assume that this directory is `./build/` in the source directory.
If you are building QEMU for the first time, you can create this directory via
```sh
mkdir build && cd build
```

From that, you then need to configure the QEMU build system.
For this, it is sufficient to only specify `arm-softmmu` as emulation target.
```sh
../configure --target-list=arm-softmmu
```

Finally build QEMU via
```sh
make -j`nproc`
```
Note that you only need to re-run the latest step (i.e. `make` from inside the build directory) when you make changes to the source-code and want to rebuild.

## Setting up QEMU for eclipse

To set up QEMU for eclipse, follow the steps above and make sure this repostiory was cloned in the same directory the OBSW was cloned.
After that, the shell script inside the OBSW folder should work to start the QEMU emulation for the iOBC.
To test whether QEMU will run with the script, run the second command from the next section.

## Running QEMU for ISIS-OBC

From the root directory, run
```sh
./iobc-loader                                    \
    -f <file-addr> ./path/to/bin -s <start-addr> \
    -- -monitor stdio
```
Make sure the path to the binary is set up correctly and use the appropriate address to load the file to (`<file-addr>`) and start address, i.e. initial program counter (`<start-addr>`).
Debug-loading the `sdram` binary works, for example with
```sh
./iobc-loader                       \
    -f sdram ./path/to/sdram-bin -s sdram -o pmc-mclk \
    -- -monitor stdio
```
while loading the full `nofrlash` binary works with 
```sh
./iobc-loader                       \
    -f norflash ./path/to/norflash-bin -s norflash \
    -- -monitor stdio
```
See `./iobc-loader -h` for more information.
The `iobc-loader` script will load and initialize the IOBC and load the specified files accordingly (more than one file can be specified at the same time).
Options after the `--` are directly forwarded to the underlying `qemu-system-arm`.

The QEMU options to pipe the serial output to the console directly are:
```sh
-serial stdio -monitor none
```
To have access to the QMP protocoll, have a look at the sections below.

### Support for SD-Cards

The iOBC supports up to two SD-Cards.
In QEMU, SD-Cards can be added by telling it to use a specified image file containing the contents of the SD-Card, meaning that all data will be read from and written to the file.
This can be done by adding the
```
-drive if=sd,index=0,format=raw,file=sd0.img
```
options to the `qemu-system-arm` call.
- The `-drive` option adds a new drive (i.e. block device).
- The `if=sd` parameter specifies the type as being an SD-Card.
- The `index=0` parameter specifies the slot of the SD-Card.
  This can be changed to `index=1` to populate the second SD-Card slot.
  For multiple slots, the `-drive` option can be repeated with parameters changed accordingly.
- The `format=raw` parameter specifies that the image file is in `raw` format.
  Multiple other formats are supported (for details consult the official QEMU documentation), however, `raw` images can be easily mounted in Linux (more details below) or copied directly from/to a physical SD-Card (e.g. via the `dd` tool).
  More on the creation of image files below.
- The `file=sd0.img` specifies that the file to be loaded is called `sd0.img` in the current working directory.
  Change `sd0.img` to whatever is appropriate for you.

As an example, a full command to run the iOBC on QEMU with two SD-Cards, provided as `sd0.img` and `sd1.img`, is
```
./iobc-lodaer                                              \
    -f sdram ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
    -s sdram -o pmc-mclk                                   \
    -- -monitor stdio                                      \
    -drive if=sd,index=0,format=raw,file=sd0.img           \
    -drive if=sd,index=1,format=raw,file=sd1.img
```

#### Creating Image Files

To manipulate and create image files, QEMU provides the `qemu-img` tool.
A `raw`-type image can be created via the command
```
./build/qemu-img create -f raw sd0.img 4G
```
The `-f raw` option specifies the type as being `raw`, `sd0.img` is the path to the image file, and `4G` specifies that the image should have a size of four gigabytes.
Change the parameters according to your needs.
For more information and other format types refer to the actual QEMU documentation for this command.

#### Mounting a raw Image on Linux

On Linux systems, a `raw`-type image can be easily mounted via the command
```
mount -o loop sd0.img /mnt
```
Again, `sd0.img` is the path to the image file.
The `/mnt` path is the path to the directory where the image should be mounted.
This will not work as easily with other image formats, however, the `qemu-img` tool provides a sub-command to convert between different types of images, which can be used to convert other types to `raw` before mounting them with the above command.

### Controlling QEMU via QMP

QEMU can be controlled via the QEMU Machine Protocol (QMP).
This is can be useful for fully automated testing, as it, for example, allows the QEMU machine and simulation framework to be initialized and all device emulators to connect before sending a start command via QMP to actually start the simulated processor.
This way it can be ensured that no initial communication between machine and devices is lost.

For this, you will need to add the `-qmp` and `-S` flags, e.g. like this:
```sh
./iobc-loader                                              \
    -f sdram ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
    -s sdram -o pmc-mclk                                   \
    -- -monitor stdio                                      \
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
  ```sh
  ./iobc-loader                                              \
      -f sdram ./path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
      -s sdram -o pmc-mclk -- -monitor stdio -s -S
  ```

- run GDB:
  ```
  arm-none-eabi-gdb \
      -ex 'target remote localhost:1234' \
      -ex 'symbol-file ./path/to/sourceobsw-at91sam9g20_ek-sdram.elf'
  ```
in your terminal.
IDEs with GDB support (Eclipse, VS-Code) can be configured accordingly.

## Examples for External Peripheral Simulation

Example scripts for simulation of external peripherals can be found in `./scripts/iobc-examples`.
