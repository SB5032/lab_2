COMS 4840 final project-bubble bobble

Don't add executables, or most of the files resulting from C compliation here, just the source code!

Instruction:

Important code part:
vga_audio_integration/ : contains hardware files. steps:
make qsys
make quartus
make rbf
embedded_shell.sh
make dtb
Then copy soc_system.dtb and output_files/soc_system.rbf to the mmcblk0p1 partition of the board

kernel_modules/ : contains kernel drivers. steps:
in vga/: 
make
insmod vga_top.ko

in audio/:
make
insmod fpga_audio.ko

game/ : contains the software game logic. steps:
    make
    ./demo
    // to start the game
