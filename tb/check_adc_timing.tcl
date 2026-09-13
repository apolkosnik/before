# Run after a full Quartus fit, from the repository root:
# quartus_sta -t tb/check_adc_timing.tcl
# Fail closed if any ADC port has no timed path or violates its I/O budget.
package require ::quartus::project
load_package sta
project_open NeXT
create_timing_netlist
read_sdc
file mkdir tb/build/audio_fixes/adc_timing
set result [open tb/build/audio_fixes/adc_timing/results.txt w]
set failures 0
set checks 0
set core_clock {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}
foreach_in_collection corner [get_available_operating_conditions] {
    set label [get_operating_conditions_info -display_name $corner]
    set_operating_conditions $corner
    update_timing_netlist
    foreach port {ADC_SDO ADC_SCK ADC_SDI ADC_CONVST} {
        set pins [get_ports $port]
        if {[get_collection_size $pins] != 1} {error "Missing ADC port $port"}
        set direction [expr {$port eq "ADC_SDO" ? "-from" : "-to"}]
        foreach analysis {setup hold} {
            set paths [get_timing_paths -$analysis $direction $pins -npaths 0]
            if {[get_collection_size $paths] == 0} {
                error "No $analysis timing path for $port at $corner"
            }
            foreach_in_collection path $paths {
                set slack [get_path_info -slack $path]
                set delay [get_path_info -data_delay $path]
                set relationship [get_path_info -clock_relationship $path]
                set clock_option [expr {$port eq "ADC_SDO" ? "-to_clock" : "-from_clock"}]
                set clock_name [get_clock_info -name [get_path_info $clock_option $path]]
                if {![string is double -strict $slack]} {error "Invalid slack for $port: $slack"}
                if {$clock_name ne $core_clock} {error "Unexpected ADC clock: $clock_name"}
                set expected [expr {$analysis eq "setup" ? 18.0 : 0.0}]
                if {$relationship != $expected} {error "Unexpected ADC $analysis budget: $relationship"}
                puts $result "$label $port $analysis slack=$slack ns data_delay=$delay ns"
                incr checks
                if {$slack < 0} {incr failures}
                # Root-clock Tco/Tsu, including the clock insertion terms,
                # form the round-trip bound. Data delay alone does not.
                if {$analysis eq "setup"} {
                    puts $result "  effective_Tco_or_Tsu=[expr {$relationship-$slack}] ns (budget 18 ns)"
                }
            }
            report_timing -$analysis $direction $pins -npaths 1 -detail full_path \
                -file "tb/build/audio_fixes/adc_timing/${corner}_${port}_${analysis}.rpt"
            puts "ADC_TIMING checked $label $port $analysis"
        }
    }
}
puts $result "$checks ADC timing checks, $failures failures"
close $result
delete_timing_netlist
project_close
if {$checks == 0 || $failures != 0} {error "ADC interface timing failed: $failures / $checks"}
puts "ALL ADC TIMING CHECKS PASS ($checks checks)"
