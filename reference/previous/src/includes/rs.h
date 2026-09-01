/*
  Previous - rs.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_RS_H
#define PREV_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void rs_encode(uint8_t *sector);
extern int  rs_decode(uint8_t *sector);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_RS_H */
