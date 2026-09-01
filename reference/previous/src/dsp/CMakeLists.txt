
if(ENABLE_DSP_EMU)
	set(DSP_SOURCES dsp_core.c dsp_cpu.c dsp_disasm.c)
endif(ENABLE_DSP_EMU)

add_library(DSP dsp.c ${DSP_SOURCES})
