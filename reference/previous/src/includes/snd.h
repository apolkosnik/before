/*
  Previous - snd.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SND_H
#define PREV_SND_H

#define SND_BUFFER_SIZE  (4*1024)
#define SND_BUFFER_LIMIT (SND_BUFFER_SIZE>>1)

extern uint8_t snd_buffer[SND_BUFFER_SIZE];
extern int     snd_buffer_len;

extern void SND_Out_Handler(void);
extern void SND_In_Handler(void);
extern void SND_DSP_Handler(void);

extern void Sound_Reset(void);
extern void Sound_Pause(bool pause);

extern void snd_start_output(uint8_t mode);
extern void snd_stop_output(void);
extern void snd_start_input(void);
extern void snd_stop_input(void);
extern void snd_dsp_start(void);
extern void snd_dsp_stop(void);
extern bool snd_output_active(void);
extern bool snd_input_active(void);

extern void snd_send_sample(uint32_t data);
extern void snd_gpo_access(uint8_t data);
extern void snd_vol_access(uint8_t data);

#endif /* PREV_SND_H */
