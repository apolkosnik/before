derive_pll_clocks
derive_clock_uncertainty

# core specific constraints

# clk_sys (28 MHz, outclk_0) and clk_vid (100 MHz, outclk_1) exchange data
# only through the dual-clock VRAM scan port and a 2FF synchronizer on the
# vertical blank level, so they are timed as asynchronous groups.
set_clock_groups -asynchronous \
	-group [get_clocks {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}] \
	-group [get_clocks {emu|pll|pll_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk}]

# LTC2308 interface: core uses SCK_DIV=2 (7 MHz), long CONVST (2 us),
# and two clk_sys cycles from CONVST falling to the first SDO capture.
# SCK is gated and its start phase moves with the fractional sample divider;
# a continuously running divide-by-four generated clock would be incorrect.
# Instead bound the root-clock-referenced Tco and Tsu explicitly. TimeQuest
# includes launch clock insertion in output Tco and subtracts capture clock
# insertion from input Tsu; these are NOT bounds on data routing alone.
# The two sides use the same clk_sys reference, so their clock insertion
# terms combine into the actual round-trip skew. Each 71.429 ns half-period
# allows 18 ns output Tco + 2 ns board + 15 ns ADC enable/data-valid +
# 2 ns board + 18 ns input Tsu = 55 ns, leaving >16 ns margin.
# SDI changes on SCK falling; it has the same half-period setup budget and
# a further half-period hold before its next change (ADC hold requirement 2.5 ns).
# CONVST remains high for 2 us: even 18 ns output skew leaves >1.6 us for
# conversion. The 2 ns one-way board allowance covers the on-board interface.
# tb/check_adc_timing.tcl checks all corners, both budgets, and their common
# clock reference. No input/output delay is added on top of these exceptions.
set_max_delay 18.000 -from [get_ports ADC_SDO]
set_min_delay 0.000 -from [get_ports ADC_SDO]
set_max_delay 18.000 -to [get_ports {ADC_SCK ADC_SDI ADC_CONVST}]
set_min_delay 0.000 -to [get_ports {ADC_SCK ADC_SDI ADC_CONVST}]
