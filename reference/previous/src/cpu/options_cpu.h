/*
  Hatari - options_cpu.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef UAE_OPTIONS_H
#define UAE_OPTIONS_H

#include "uae/types.h"


struct uae_prefs {
	int cachesize;
	bool cpu_cycle_exact;
	int cpu_clock_multiplier;
	int cpu_frequency;
	bool cpu_memory_cycle_exact;

	int m68k_speed;
	int cpu_model;
	int mmu_model;
	bool mmu_ec;
	int cpu060_revision;
	int fpu_model;
	int fpu_revision;
	bool cpu_compatible;
	bool int_no_unimplemented;
	bool fpu_no_unimplemented;
	bool address_space_24;
	bool cpu_data_cache;
};


extern struct uae_prefs currprefs, changed_prefs;
extern void fixup_cpu (struct uae_prefs *prefs);
extern void check_prefs_changed_cpu (void);
extern void error_log (const TCHAR*, ...);

#endif /* UAE_OPTIONS_H */
