# Minimal System Console test script
# Usage: system-console.exe -tcl sc_test_master.tcl
puts "Starting minimal master test"
set masters [get_service_paths master]
if {[llength $masters] == 0} {
    puts "ERROR: No masters found"
    exit 1
}
set master [lindex $masters 0]
puts "Using master: $master"
if {[catch {open_service master $master} err]} {
    puts "open_service failed: $err"
    exit 2
}

if {[catch {master_write_32 $master 0x0 0xA5A5A5A5} werr]} { puts "WRITE_FAILED: $werr" } else { puts "WRITE_OK" }
if {[catch {master_read_32 $master 0x0 1} rerr]} { puts "READ_FAILED: $rerr" } else { puts "READ_OK" }

close_service master $master
puts "Finished"
