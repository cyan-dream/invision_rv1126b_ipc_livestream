#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CMD_PATH "/tmp/cmd"
#define UPDATE_PATH	"/usr/mmcdata/mmcblk2p8/"

int make_crc32_table(void);
int update_file_check(void);
void crc16_init_table(void);
uint16_t crc16_calc(uint8_t *data, int len);
int update_local_file(void);
int update_remote_file(void);


#ifdef __cplusplus
}
#endif
#endif