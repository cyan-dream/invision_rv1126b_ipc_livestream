#include "update.h"
#include <stdio.h>
#include <dirent.h>
#include "uart.h"
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
static uint32_t crc32_table[256];
#if 0
static uint16_t crc16_table[256];

void crc16_init_table(void)
{
    for (int i = 0; i < 256; i++) {
        uint16_t crc = i << 8; 
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
        crc16_table[i] = crc;
    }
}

uint16_t crc16_calc(uint8_t *data, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }   
    return crc;
}
#endif

static unsigned short update_crc_ccitt( unsigned short crc, unsigned char c );
unsigned short crc16_calc(unsigned char* data_p, int length)
{
	unsigned short crc;

	crc = 0;//0xffff;

	if (length == 0)
	  //return (~crc);
	  	return crc;
	int j;
	for(j = 0; j < length; j++) {
		crc = update_crc_ccitt(crc, data_p[j]);
	}

	//myprintf(DBG_INFO, "crc = %x, %x\n", crc, crc^0xffff);

	return crc;
}

#define                 P_CCITT     0x1021
static unsigned short   crc_tabccitt[256];
/*******************************************************************\
 *                                                                   *
 *   static void init_crcccitt_tab( void );                          *
 *                                                                   *
 *   The function init_crcccitt_tab() is used to fill the  array     *
 *   for calculation of the CRC-CCITT with values.                   *
 *                                                                   *
 \*******************************************************************/

void crc16_init_table( void )
{

	int i, j;
	unsigned short crc, c;

	for (i = 0; i < 256; i++) {

		crc = 0;
		c   = ((unsigned short) i) << 8;

		for (j=0; j<8; j++) {

			if ( (crc ^ c) & 0x8000 ) crc = ( crc << 1 ) ^ P_CCITT;
			else                      crc =   crc << 1;

			c = c << 1;
		}

		crc_tabccitt[i] = crc;
	}
	return;
}  /* init_crcccitt_tab */

static unsigned short update_crc_ccitt( unsigned short crc, unsigned char c )
{

	unsigned short tmp, short_c;

	short_c  = 0x00ff & (unsigned short) c;

	tmp = (crc >> 8) ^ short_c;
	crc = (crc << 8) ^ crc_tabccitt[tmp];

	return crc;

}  /* update_crc_ccitt */

int make_crc32_table(void)
{
	uint32_t c;
	int i = 0;
	int bit = 0;

	for(i = 0; i < 256; i++)
	{
		c  = (uint32_t)i;

		for(bit = 0; bit < 8; bit++)
		{
			if(c&1)
			{
				c = (c >> 1)^(0xEDB88320);
			}
			else
			{
				c =  c >> 1;
			}

		}
		crc32_table[i] = c;
	}
	return 0;
}

static uint32_t make_crc(uint32_t crc, unsigned char *string, uint32_t size)
{
	while(size--)
	  crc = (crc >> 8)^(crc32_table[(crc ^ *string++)&0xff]);

	return crc;
}
extern struct update_info update_info;

static int IsDirectory(char *pszName)  
{  
	struct stat s_stat;  

	//取得文件状态  
	if (lstat(pszName, &s_stat) < 0) return 1;  // skip

	if (S_ISDIR(s_stat.st_mode)) return 1;  // skip

	return 0; // File type  
} 

int update_local_file(void)
{
	if (update_info.update_fin == NULL) return 0;

	fseek(update_info.update_fin, sizeof(update_info.ft), SEEK_SET);

	system("mkdir -p " CMD_PATH "/update");

	FILE * fout = fopen(CMD_PATH"/update/tmp.tar", "w");
	char buffer[1024];
	long long fsize = update_info.ft.size;
	while(fsize > 0) {
		int len = fread(buffer, 1, sizeof(buffer), update_info.update_fin);
		if (len > 0) {
			if (fsize < len) len = fsize;
			int nbytes = fwrite(buffer, 1, len, fout);
			if (nbytes != len) {
				fclose(update_info.update_fin);
				fclose(fout);
				update_info.update_fin = NULL;
				return -1;
			}
			fsize -= len;
		}
		if (feof(update_info.update_fin)) break;
	}

	if (fsize > 0) {
		fclose(update_info.update_fin);
		update_info.update_fin = NULL;
		fclose(fout);
		return -1;
	}

	fflush(fout);
	printf("write out file %d\n", ftell(fout));
	fclose(update_info.update_fin);
	update_info.update_fin = NULL;
	update_info.update_index++;
	fclose(fout);

	if (update_info.ft.type == 0) {
		system("tar -C " CMD_PATH "/update -xf " CMD_PATH "/update/tmp.tar");
		system("rm -f " CMD_PATH "/update/tmp.tar");
		system("cp -rf " CMD_PATH "/update/* /");
	} else if (update_info.ft.type == 2) {
		printf("update kernel\n");
		system("dd if=" CMD_PATH "/update/tmp.tar of=/dev/mmcblk2p1");
	}
	system("rm -rf " CMD_PATH "/update");
	sprintf(buffer, "rm -f %s/%s", UPDATE_PATH, update_info.update_file);
	system(buffer);
	system("sync");
	free(update_info.update_file);
	update_info.update_file = NULL;

	return 1;
}

int update_remote_file(void)
{
	if (update_info.update_fin == NULL) return 0;

	 if (update_info.remote_ack_timeout) {
	 	fseek(update_info.update_fin, -update_info.remote_data_num, SEEK_CUR);
	 	update_info.update_pos -= update_info.remote_data_num;
	 	update_info.remote_ack_timeout = 0;
	 	printf("resend update data pos = %d\n", update_info.update_pos);
	 }

	char buffer[512];
	char temp[512];
	int max_len = 384;
	buffer[0] = 0;
	buffer[1] = RM_UPDATE_DATA;
	char* ptr = buffer+2; // alloc update cmd
	int num = 0;
	int fsize = update_info.ft.size - update_info.update_pos;
	do {
		int bl = sizeof(buffer)-num-2;
		if (fsize < bl-1) bl = fsize+1;
		if (fgets(temp, bl, update_info.update_fin) != NULL) {
			//if (fsize > xui.ft.size - 512 || fsize < 512) 
			//			myprintf(-1, "%s", temp);
			int len = strlen(temp);
			if (len > max_len) return -1; // file format error
			num += len;
			if (num > max_len) {
				num -= len;
				fseek(update_info.update_fin, -len, SEEK_CUR);
				break;
			} else {
				fsize -= len;
				int xn = 1;
				do {
					if (temp[len-xn] == '\n' || temp[len-xn] == '\r') temp[len-xn] = 0;
					else break;
					xn++;
				} while(1);
				len = strlen(temp);
				if (len > 0) {
					strcpy(ptr, temp);
					ptr += len;
				}
			}
		} else {
			break;
		}
	} while(fsize != 0);

	update_info.remote_data_num = num;
	update_info.update_pos += num;

	if (num > 0) {
		printf("send update pos = %d, fsize = %d\n", update_info.update_pos, fsize);
		uart_send_update_data(COM, buffer, ptr-buffer);
	} else {
		//myprintf(-1, "file pkt num = %d\n", num);
	}

	return num;
}
int update_file_check(void)
{
	printf("update_file_check %s\n", UPDATE_PATH);
	DIR *pDir;  
	struct dirent *pDirent;  
	pDir = opendir(UPDATE_PATH);  
	if (pDir == NULL) return 0;  
	int num = 0;
	while ((pDirent = readdir(pDir)) != NULL)  
	{  
		// if ((pDirent->d_name == '.') || pDirent->d_name[0] == '..')  
		printf("check update file %s\n", pDirent->d_name);
		if (strcmp(pDirent->d_name, ".") == 0 || strcmp(pDirent->d_name, "..") == 0)  
		{  
			continue;  
		}  
		char buff[1024] = {0};  
		sprintf(buff, "%s/%s", UPDATE_PATH, pDirent->d_name);  

		if (IsDirectory(buff)) continue;

		FILE* fin = fopen(buff, "r");
		if (fin == NULL) continue;
		update_file_t ft;
		if (fread(&ft, 1, sizeof(ft), fin) < sizeof(ft)) {
			fclose(fin);
			continue;
		}
		printf("code = %x, size = %lld\n", ft.code, ft.size);
		if (ft.code != 0x5a4723a5 || (ft.type != 0 && ft.type != 1 && ft.type != 2)) {
			fclose(fin);
			continue;
		}
		int tcrc;
		if (ft.crc32 != (tcrc = make_crc(0xffffffff, (unsigned char*)&ft, sizeof(ft)-sizeof(ft.crc32)))) {
			printf("ft head len = %d, crc error %x != %x\n", sizeof(ft)-sizeof(ft.crc32), ft.crc32, tcrc);
			fclose(fin);
			continue;
		}

		fseek(fin, 0, SEEK_END);
		if (ft.size != ftell(fin)-sizeof(ft)-sizeof(uint32_t)) {
			printf("file size %d != %d\n", ft.size, (int)(ftell(fin)-sizeof(ft)-sizeof(uint32_t)));
			fclose(fin);
			continue;
		}
		fseek(fin, sizeof(ft), SEEK_SET);

		uint32_t crc32 = 0xffffffff;
		long long fsize = ft.size;
		do {
			int xnum = sizeof(buff);
			if (xnum > fsize) xnum = fsize;
			int len = fread(buff, 1, xnum, fin);
			if (len > 0) {
				crc32 = make_crc(crc32, (unsigned char*)buff, len);
				fsize -= len;
			}
			if (feof(fin)) break;
		} while(fsize > 0);

		if (fsize > 0) {
			fclose(fin);
			continue;
		}

		uint32_t fcrc32;
		fread(&fcrc32, 1, sizeof(fcrc32), fin);
		if (fcrc32 != crc32) {
			printf("file crc error %x != %x\n", crc32, fcrc32);
			fclose(fin);
			continue;
		}

		num++;
		if (update_info.update_fin == NULL) {
			fseek(fin, sizeof(ft), SEEK_SET);
			update_info.update_fin = fin;
			update_info.update_pos = 0;
			memcpy(&(update_info.ft), &ft, sizeof(ft));
			if (update_info.update_file != NULL) free(update_info.update_file);
			update_info.update_file = (char*)malloc(strlen(pDirent->d_name)+1);
			strcpy(update_info.update_file, pDirent->d_name);
			if (update_info.update_num != 0) break;  // find one file
		} else {
			fclose(fin);
		}
	}  

	if (update_info.update_num == 0) update_info.update_num = num; // first check update num

	closedir(pDir);  

	return num;
}
