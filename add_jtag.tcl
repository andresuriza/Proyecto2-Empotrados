package require -exact qsys 16.0

load_system hps.qsys

add_instance jtag_master altera_jtag_avalon_master
set_instance_parameter_value jtag_master {USE_PLI} {0}
set_instance_parameter_value jtag_master {PLI_PORT} {50000}
set_instance_parameter_value jtag_master {FAST_VER} {0}
set_instance_parameter_value jtag_master {FIFO_DEPTHS} {2}

add_connection clk_0.clk jtag_master.clk clock
add_connection clk_0.clk_reset jtag_master.clk_reset reset
add_connection jtag_master.master mm_bridge_0.s0 avalon

save_system hps.qsys
