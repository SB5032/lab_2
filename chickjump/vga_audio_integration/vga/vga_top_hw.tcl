# TCL File Generated by Component Editor 21.1
# Mon May 06 04:04:24 EDT 2024
# DO NOT MODIFY


# 
# vga_top "vga_top" v1.0
#  2024.05.06.04:04:24
# 
# 

# 
# request TCL package from ACDS 16.1
# 
package require -exact qsys 16.1


# 
# module vga_top
# 
set_module_property DESCRIPTION ""
set_module_property NAME vga_top
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR ""
set_module_property DISPLAY_NAME vga_top
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false


# 
# file sets
# 
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL vga_top
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file vga_top.sv SYSTEM_VERILOG PATH vga_top.sv TOP_LEVEL_FILE
add_fileset_file linebuffer.sv SYSTEM_VERILOG PATH linebuffer.sv
add_fileset_file linebuffer_ram.v VERILOG PATH linebuffer_ram.v
add_fileset_file tile_loader.sv SYSTEM_VERILOG PATH tile_loader.sv
add_fileset_file tile_rom.v VERILOG PATH tile_rom.v
add_fileset_file tile_array.v VERILOG PATH tile_array.v
add_fileset_file sprite_loader.sv SYSTEM_VERILOG PATH sprite_loader.sv
add_fileset_file sprite_draw.sv SYSTEM_VERILOG PATH sprite_draw.sv
add_fileset_file sprite_active.sv SYSTEM_VERILOG PATH sprite_active.sv
add_fileset_file sprite_rom.v VERILOG PATH sprite_rom.v
add_fileset_file combined_sprite.mif MIF PATH combined_sprite.mif
add_fileset_file combined_tile.mif MIF PATH combined_tile.mif


# 
# parameters
# 


# 
# module assignments
# 
set_module_assignment embeddedsw.dts.group vga
set_module_assignment embeddedsw.dts.name vga_top
set_module_assignment embeddedsw.dts.vendor csee4840


# 
# display items
# 


# 
# connection point clock
# 
add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
set_interface_property clock EXPORT_OF ""
set_interface_property clock PORT_NAME_MAP ""
set_interface_property clock CMSIS_SVD_VARIABLES ""
set_interface_property clock SVD_ADDRESS_GROUP ""

add_interface_port clock clk clk Input 1


# 
# connection point reset
# 
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
set_interface_property reset ENABLED true
set_interface_property reset EXPORT_OF ""
set_interface_property reset PORT_NAME_MAP ""
set_interface_property reset CMSIS_SVD_VARIABLES ""
set_interface_property reset SVD_ADDRESS_GROUP ""

add_interface_port reset reset reset Input 1


# 
# connection point avalon_slave_0
# 
add_interface avalon_slave_0 avalon end
set_interface_property avalon_slave_0 addressUnits WORDS
set_interface_property avalon_slave_0 associatedClock clock
set_interface_property avalon_slave_0 associatedReset reset
set_interface_property avalon_slave_0 bitsPerSymbol 8
set_interface_property avalon_slave_0 burstOnBurstBoundariesOnly false
set_interface_property avalon_slave_0 burstcountUnits WORDS
set_interface_property avalon_slave_0 explicitAddressSpan 0
set_interface_property avalon_slave_0 holdTime 0
set_interface_property avalon_slave_0 linewrapBursts false
set_interface_property avalon_slave_0 maximumPendingReadTransactions 0
set_interface_property avalon_slave_0 maximumPendingWriteTransactions 0
set_interface_property avalon_slave_0 readLatency 0
set_interface_property avalon_slave_0 readWaitTime 1
set_interface_property avalon_slave_0 setupTime 0
set_interface_property avalon_slave_0 timingUnits Cycles
set_interface_property avalon_slave_0 writeWaitTime 0
set_interface_property avalon_slave_0 ENABLED true
set_interface_property avalon_slave_0 EXPORT_OF ""
set_interface_property avalon_slave_0 PORT_NAME_MAP ""
set_interface_property avalon_slave_0 CMSIS_SVD_VARIABLES ""
set_interface_property avalon_slave_0 SVD_ADDRESS_GROUP ""

add_interface_port avalon_slave_0 writedata writedata Input 32
add_interface_port avalon_slave_0 write write Input 1
add_interface_port avalon_slave_0 chipselect chipselect Input 1
add_interface_port avalon_slave_0 address address Input 4
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isFlash 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isMemoryDevice 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isPrintableDevice 0


# 
# connection point vga
# 
add_interface vga conduit end
set_interface_property vga associatedClock clock
set_interface_property vga associatedReset ""
set_interface_property vga ENABLED true
set_interface_property vga EXPORT_OF ""
set_interface_property vga PORT_NAME_MAP ""
set_interface_property vga CMSIS_SVD_VARIABLES ""
set_interface_property vga SVD_ADDRESS_GROUP ""

add_interface_port vga VGA_B b Output 8
add_interface_port vga VGA_BLANK_n blank_n Output 1
add_interface_port vga VGA_CLK clk Output 1
add_interface_port vga VGA_G g Output 8
add_interface_port vga VGA_HS hs Output 1
add_interface_port vga VGA_R r Output 8
add_interface_port vga VGA_SYNC_n sync_n Output 1
add_interface_port vga VGA_VS vs Output 1

