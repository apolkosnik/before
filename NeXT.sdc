derive_pll_clocks
derive_clock_uncertainty

# core specific constraints

# clk_sys (32 MHz, outclk_0) and clk_vid (100 MHz, outclk_1) exchange data
# only through the dual-clock VRAM scan port and a 2FF synchronizer on the
# vertical blank level, so they are timed as asynchronous groups.
set_clock_groups -asynchronous \
	-group [get_clocks {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}] \
	-group [get_clocks {emu|pll|pll_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk}]
