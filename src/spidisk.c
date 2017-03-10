// ------------------------------------------------------------------- //
//  PERIDOT-NGS SPI flash Filesystem (SPI-disk driver)                 //
// ------------------------------------------------------------------- //
//
//  ver 0.91
//		2017/03/10	s.osafune@gmail.com
//
// ******************************************************************* //
//  The MIT License (MIT)
//  Copyright (c) 2017 J-7SYSTEM WORKS LIMITED.
//
//  Permission is hereby granted, free of charge, to any person
//  obtaining a copy of this software and associated documentation
//  files (the "Software"), to deal in the Software without restriction,
//  including without limitation the rights to use, copy, modify, merge,
//  publish, distribute, sublicense, and/or sell copies of the Software,
//  and to permit persons to whom the Software is furnished to do so,
//  subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
//  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//  NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
//  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
//  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
//  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// ******************************************************************* //

#include <io.h>
#include "fatfs/diskio.h"
#include "fatfs/ffconf.h"
#include "spidisk.h"

//#define _DEBUG_		// デバッグ用メッセージ表示 


/*-----------------------------------------------------------------------*/
/* Define a macro                                                        */
/*-----------------------------------------------------------------------*/

#define SPI_SS_ASSERT			(1<<8)
#define SPI_SS_NEGATE			((0<<8)|0xff)
#define SPI_TRANS_START			(1<<9)
#define SPI_TRANS_READY			(1<<9)

#define SPI_PAGE_SIZE			(256)	// プログラムページサイズ (バイト数) 
#define SPI_ERASEPAGE_COUNT		(16)	// 消去ページ数(4kバイト/セクタ) 
#define SPI_ERASE_SIZE			(SPI_PAGE_SIZE * SPI_ERASEPAGE_COUNT)
#define SPI_SECTOR_SIZE			(SPI_ERASE_SIZE)

#define SPI_CMD_WRITE_ENABLE	(0x06)
#define SPI_CMD_WRITE_DISABLE	(0x04)
#define SPI_CMD_READ_STATUS		(0x05)
#define SPI_CMD_GET_JEDECID		(0x9f)
#define SPI_CMD_READ_SFDP		(0x5a)
#define SPI_CMD_RESET_ENABLE	(0x66)
#define SPI_CMD_RESET			(0x99)

#define SPI_CMD_PAGE_PROGRAM	(0x02)
#define SPI_CMD_SECTOR_ERASE	(0x20)
#define SPI_CMD_READ_DATA		(0x03)
#define SPI_CMD_FAST_READ		(0x0b)

#define SPI_CMD4_PAGE_PROGRAM	(0x12)
#define SPI_CMD4_SECTOR_ERASE	(0x21)
#define SPI_CMD4_READ_DATA		(0x13)
#define SPI_CMD4_FAST_READ		(0x0c)

#define SPI_RETRY_COUNT			(3)		// エラー発生時の再試行回数 

#if !_FS_READONLY
 #define _USE_SPI_WRITE			1
#else
 #define _USE_SPI_WRITE			0
#endif

#if (_USE_SPI_WRITE && _USE_MKFS)
 #define _USE_SPI_FORMAT		1
#else
 #define _USE_SPI_FORMAT		0
#endif


#define RIFF_SET_ID(_x, _id0,_id1,_id2,_id3)\
	*(((BYTE *)(_x))+0)=(_id0);\
	*(((BYTE *)(_x))+1)=(_id1);\
	*(((BYTE *)(_x))+2)=(_id2);\
	*(((BYTE *)(_x))+3)=(_id3);

#define RIFF_CHECK_ID(_x, _id0,_id1,_id2,_id3)\
	(*(((BYTE *)(_x))+0)==(_id0) &&\
	 *(((BYTE *)(_x))+1)==(_id1) &&\
	 *(((BYTE *)(_x))+2)==(_id2) &&\
	 *(((BYTE *)(_x))+3)==(_id3))

#define RIFF_SET_DWORD(_x, _dw)\
	*(((BYTE *)(_x))+0)=(((_dw)>> 0) & 0xff);\
	*(((BYTE *)(_x))+1)=(((_dw)>> 8) & 0xff);\
	*(((BYTE *)(_x))+2)=(((_dw)>>16) & 0xff);\
	*(((BYTE *)(_x))+3)=(((_dw)>>24) & 0xff);

#define RIFF_SET_WORD(_x, _w)\
	*(((BYTE *)(_x))+0)=(((_w)>> 0) & 0xff);\
	*(((BYTE *)(_x))+1)=(((_w)>> 8) & 0xff);

#define RIFF_GET_DWORD(_x)\
	((DWORD)*(((BYTE *)(_x))+0)<< 0) |\
	((DWORD)*(((BYTE *)(_x))+1)<< 8) |\
	((DWORD)*(((BYTE *)(_x))+2)<<16) |\
	((DWORD)*(((BYTE *)(_x))+3)<<24)

#define RIFF_GET_WORD(_x)\
	((WORD)*(((BYTE *)(_x))+0)<< 0) |\
	((WORD)*(((BYTE *)(_x))+1)<< 8)

#ifdef _DEBUG_
 #include <stdio.h>
 #define dgb_printf printf								// デバッグメッセージ表示 
#else
 static int dgb_printf(const char *format, ...) { return 0; }
#endif

#if _USE_SPI_SATCACHE
 #include <malloc.h>
 #define spiff_malloc(_x)		malloc(_x)				// メモリアロケータ 
 #define spiff_free(_x)			free(_x)
#endif

#if _USE_SPI_WRITE
 #include <unistd.h>
 #define spiff_delay_ms(_x)		usleep((_x)*1000)		// 1ms単位で待つ関数のマクロ 
#endif


DEF_SPIDISK *spidisk = NULL;	// SPIディスクハンドラ 



/*-----------------------------------------------------------------------*/
/* SPI master peripheral handler                                         */
/*-----------------------------------------------------------------------*/

// SPIマスタペリフェラルの通信完了を待つ 
static DWORD spi_waitready(void)
{
	DWORD res;

	do {
		res = IORD(SPI_DEV, 0);
	} while( !(res & SPI_TRANS_READY) );

	return res;
}

// SPIマスタで1バイトの送受信を行う 
static DWORD spi_transaction(
	DWORD send
)
{
	IOWR(SPI_DEV, 0, SPI_TRANS_START | send);
	return spi_waitready();
}



/*-----------------------------------------------------------------------*/
/* Access to SPI Flash device                                            */
/*-----------------------------------------------------------------------*/

static DRESULT spi_getinfo(
	DWORD *memsize,
	DWORD *id
)
{
	BYTE sfdp[16];		// SFDP work
	DWORD jedecid;
	UINT i;

	dgb_printf("[SPI] flash device info\n");
	spi_waitready();

	/* JEDEC IDの読み出し */

	spi_transaction(SPI_SS_ASSERT | SPI_CMD_GET_JEDECID);
	jedecid  = (spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff) << 16;	// MID
	jedecid |= (spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff) << 8;		// DID2
	jedecid |= (spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff) << 0;		// DID1
	spi_transaction(SPI_SS_NEGATE);

	*id = jedecid;
	dgb_printf("    manufacturer ID = 0x%02x\n    device ID = 0x%04x\n",
					(jedecid >> 16)& 0xff, jedecid & 0xffff);


#if _USE_SPI_AUTODETECT
	/* SFDPヘッダ読み出し */

	spi_transaction(SPI_SS_ASSERT | SPI_CMD_READ_SFDP);
	spi_transaction(SPI_SS_ASSERT | 0x00);		// A23-16
	spi_transaction(SPI_SS_ASSERT | 0x00);		// A15-8
	spi_transaction(SPI_SS_ASSERT | 0x00);		// A7-0
	spi_transaction(SPI_SS_ASSERT | 0xff);		// dummy

	for(i=0 ; i<16 ; i++) {
		sfdp[i] = spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff;
	}
	spi_transaction(SPI_SS_NEGATE);

	if (!RIFF_CHECK_ID(&sfdp[0], 'S','F','D','P')) return RES_NOTRDY;	// SFDPに対応していない 
	dgb_printf("    SFDP supported\n    parameter table offset = 0x%02x%02x%02x\n",
					sfdp[14], sfdp[13], sfdp[12]);


	/* 容量および対応機能の取得 */

	spi_transaction(SPI_SS_ASSERT | SPI_CMD_READ_SFDP);
	spi_transaction(SPI_SS_ASSERT | sfdp[14]);	// A23-16
	spi_transaction(SPI_SS_ASSERT | sfdp[13]);	// A15-8
	spi_transaction(SPI_SS_ASSERT | sfdp[12]);	// A7-0
	spi_transaction(SPI_SS_ASSERT | 0xff);		// dummy

	for(i=0 ; i<8 ; i++) {
		sfdp[i] = spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff;
	}
	spi_transaction(SPI_SS_NEGATE);

	if ((sfdp[0] & (3<<0)) != 1) return RES_NOTRDY;			// 4kBセクタ消去に対応していない 
	if (sfdp[1] != SPI_CMD_SECTOR_ERASE) return RES_NOTRDY;
	dgb_printf("    4kB erase supported\n    erase command set = 0x%02x\n", sfdp[1]);

	if (!((sfdp[2] & (3<<1)) == (0<<1) || (sfdp[2] & (3<<1)) == (1<<1))) return RES_NOTRDY;
															// 3byteアドレッシングに対応していない 
	dgb_printf("    3byte addressing supported\n");

	switch (sfdp[7]) {
		default:
			return RES_NOTRDY;
			break;

		case 0x00:	// 16Mbit
			*memsize = 2*1024*1024;
			break;
		case 0x01:	// 32Mbit
			*memsize = 4*1024*1024;
			break;
		case 0x03:	// 64Mbit
			*memsize = 8*1024*1024;
			break;
		case 0x07:	// 128Mbit
			*memsize = 16*1024*1024;
			break;
		case 0x0f:	// 256Mbit
			*memsize = 32*1024*1024;
			break;
		case 0x1f:	// 512Mbit
			*memsize = 64*1024*1024;
			break;
		case 0x3f:	// 1Gbit
			*memsize = 128*1024*1024;
			break;
		case 0x7f:	// 2Gbit
			*memsize = 256*1024*1024;
			break;
	}
#else
		*memsize = SPI_FLASH_MEMSIZE;
		dgb_printf("    forced settings\n");
#endif

	dgb_printf("    flash memory size = %d bytes\n", *memsize);


	return RES_OK;
}


static DRESULT spi_read(
	BYTE *buff,
	DWORD address,
	DWORD byte
)
{
	spi_waitready();

	if (address >= 16*1024*1024) {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD4_READ_DATA);		// Read 4byte address
		spi_transaction(SPI_SS_ASSERT |((address >> 24)& 0xff));
	} else {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_READ_DATA);			// Read 3byte address
	}
	spi_transaction(SPI_SS_ASSERT |((address >> 16)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  8)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  0)& 0xff));

	do {
		*buff++ = (spi_transaction(SPI_SS_ASSERT | 0xff) & 0xff);
	} while (--byte);
	spi_transaction(SPI_SS_NEGATE);

	return RES_OK;
}


#if _USE_SPI_WRITE
static DRESULT spi_erase_sector(
	DWORD address
)
{
	DWORD res;
	UINT t;

	address &= ~(SPI_ERASE_SIZE-1);
	spi_waitready();

	// 書き込みイネーブル 
	spi_transaction(SPI_SS_ASSERT | SPI_CMD_WRITE_ENABLE);			// WP Unlock
	spi_transaction(SPI_SS_NEGATE);

	// セクタ消去 
	if (address >= 16*1024*1024) {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD4_SECTOR_ERASE);		// Erase sector(4byte address)
		spi_transaction(SPI_SS_ASSERT |((address >> 24)& 0xff));
	} else {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_SECTOR_ERASE);		// Erase sector(3byte address)
	}
	spi_transaction(SPI_SS_ASSERT |((address >> 16)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  8)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  0)& 0xff));
	spi_transaction(SPI_SS_NEGATE);

	// 消去完了待ち 
	for(t=SPI_ERASE_WAIT_MAX ; t>0 ; t--) {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_READ_STATUS);		// Read Status
		res = spi_transaction(SPI_SS_ASSERT | 0xff);
		spi_transaction(SPI_SS_NEGATE);

		if (!(res & (1<<0))) break;									// busyが1の間待つ 
		spiff_delay_ms(1);											// 1ms以上待つ 
	}
	if (t == 0) {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_RESET_ENABLE);		// タイムアウトしたら デバイスリセット 
		spi_transaction(SPI_SS_NEGATE);
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_RESET);
		spi_transaction(SPI_SS_NEGATE);

		return RES_ERROR;
	}

	return RES_OK;
}


static DRESULT spi_program_page(
	const BYTE *buff,	/* Data to be written */
	DWORD address
)
{
	const BYTE *p;
	BYTE *v, verify[SPI_PAGE_SIZE];
	UINT n;

	address &= ~(SPI_PAGE_SIZE-1);
	spi_waitready();

	// 書き込みイネーブル 
	spi_transaction(SPI_SS_ASSERT | SPI_CMD_WRITE_ENABLE);			// WP Unlock
	spi_transaction(SPI_SS_NEGATE);

	// ページ書き込み 
	if (address >= 16*1024*1024) {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD4_PAGE_PROGRAM);		// Page program(4byte address)
		spi_transaction(SPI_SS_ASSERT |((address >> 24)& 0xff));
	} else {
		spi_transaction(SPI_SS_ASSERT | SPI_CMD_PAGE_PROGRAM);		// Page program(3byte address)
	}
	spi_transaction(SPI_SS_ASSERT |((address >> 16)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  8)& 0xff));
	spi_transaction(SPI_SS_ASSERT |((address >>  0)& 0xff));

	p = buff;
	for(n=SPI_PAGE_SIZE ; n>0 ; n--) spi_transaction(SPI_SS_ASSERT | *p++);
	spi_transaction(SPI_SS_NEGATE);

	// 書き込み完了待ち 
	spi_transaction(SPI_SS_ASSERT | SPI_CMD_READ_STATUS);			// Read Status
	while(spi_transaction(SPI_SS_ASSERT | 0xff) & (1<<0)) {}		// busyが1の間待つ 
	spi_transaction(SPI_SS_NEGATE);

	// ベリファイ 
	spi_read(verify, address, SPI_PAGE_SIZE);
	p = buff;
	v = verify;
	for(n=SPI_PAGE_SIZE ; n>0 ; n--) {
		if (*p++ != *v++) break;
	}
	if (n != 0) return RES_ERROR;

	return RES_OK;
}
#endif



/*-----------------------------------------------------------------------*/
/* Format a physical disk                                                */
/*-----------------------------------------------------------------------*/

#if _USE_SPI_FORMAT
DRESULT spidisk_format(
	DWORD disksize,
	WORD rsv_count
)
{
	DWORD memsize, id, diskinfo_sector, startaddr;
	WORD rsv_top_sector, sat_top_sector;
	WORD all_sector_count, rsv_sector_count, sat_sector_count, dat_sector_count;
	DWORD address, sat_address;
	WORD lba_sector, phy_sector, rsv_sector;
	UINT n, retry;
	BYTE buff[SPI_PAGE_SIZE];

	/* パラメータ計算 */

	if (spi_getinfo(&memsize, &id)) return RES_NOTRDY;

	dgb_printf("[DISK] spi disk format\n");

	if (disksize == 0) disksize = memsize;
	if (disksize < 1*1024*1024) {
		dgb_printf("[!] format parameter error\n");
		return RES_PARERR;
	}

	all_sector_count = (disksize / SPI_ERASE_SIZE) - 1;

	if (memsize < (all_sector_count + 1) * SPI_ERASE_SIZE) {
		dgb_printf("[!] format parameter error\n");
		return RES_PARERR;
	}

	if (rsv_count == 0) {
		rsv_sector_count = (all_sector_count / 32) + 10;
	} else {
		rsv_sector_count = rsv_count;
	}

	sat_sector_count = ((all_sector_count - rsv_sector_count)*2 / SPI_ERASE_SIZE) + 1;
	dat_sector_count = all_sector_count - rsv_sector_count - sat_sector_count;

	if (dat_sector_count < 128) {
		dgb_printf("[!] format parameter error\n");
		return RES_PARERR;
	}

	diskinfo_sector = (memsize / SPI_ERASE_SIZE) - 1;
	startaddr = memsize - (all_sector_count + 1) * SPI_ERASE_SIZE;
	sat_top_sector = all_sector_count - sat_sector_count;
	rsv_top_sector = all_sector_count - sat_sector_count - rsv_sector_count;


	dgb_printf("    diskinfo offset = 0x%08x (sector %d)\n",
					diskinfo_sector * SPI_ERASE_SIZE, diskinfo_sector);
	dgb_printf("    diskimage top offset = 0x%08x (sector %d)\n    rsv top sector = %d, offset = 0x%08x\n",
					startaddr , startaddr / SPI_ERASE_SIZE,
					rsv_top_sector, startaddr + rsv_top_sector * SPI_ERASE_SIZE);
	dgb_printf("    sat top sector = %d, offset = 0x%08x\n",
					sat_top_sector, startaddr + sat_top_sector * SPI_ERASE_SIZE);


	/* セクタ消去テストとLBA変換テーブル作成 */

	dgb_printf("    format");
	address = startaddr + sat_top_sector * SPI_ERASE_SIZE;

	for(n=sat_sector_count ; n>0 ; n--) {
		for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
			if (spi_erase_sector(address) == RES_OK) break;
		}
		if (retry == 0) {
			dgb_printf("\n[!] sector allocation table erase was failed. (0x%08x)\n", address);
			return RES_ERROR;
		}

		address += SPI_ERASE_SIZE;
	}


	lba_sector = 0;
	rsv_sector = rsv_top_sector;
	sat_address = startaddr + sat_top_sector * SPI_ERASE_SIZE;

	for(n=0 ; n<SPI_PAGE_SIZE ; n++) buff[n] = 0xff;

	do {
		phy_sector = lba_sector;

		while(1) {
			address = startaddr + phy_sector * SPI_ERASE_SIZE;

			for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
				if (spi_erase_sector(address) == RES_OK) break;
			}
			if (retry) {
				break;
			} else {
				phy_sector = rsv_sector++;

				if (phy_sector >= sat_top_sector) {
					dgb_printf("\n[!] remap lba %d was failed.\n", lba_sector);
					return RES_ERROR;
				}
			}
		}

		n = (lba_sector & (SPI_PAGE_SIZE/2-1)) * 2;
		buff[n] = phy_sector & 0xff;
		buff[n+1] = (phy_sector >> 8) & 0xff;

		lba_sector++;

		if ( (lba_sector & (SPI_PAGE_SIZE/2-1)) == 0 || lba_sector == dat_sector_count) {
			for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
				if (spi_program_page(buff, sat_address) == RES_OK) break;
			}
			if (retry == 0) {
				dgb_printf("\n[!] sector allocation table program was failed. (0x%08x)\n", sat_address);
				return RES_ERROR;
			}

			sat_address += SPI_PAGE_SIZE;

			for(n=0 ; n<SPI_PAGE_SIZE ; n++) buff[n] = 0xff;
			dgb_printf(".");
		}

	} while(lba_sector < dat_sector_count);

	dgb_printf("done\n");


	/* ディスク情報テーブル作成 */

	for(n=0 ; n<SPI_PAGE_SIZE ; n++) buff[n] = 0xff;

	RIFF_SET_ID(&buff[0], 'R','I','F','F');
	RIFF_SET_DWORD(&buff[4], 4+8+16);
	RIFF_SET_ID(&buff[8], 'D','I','S','K');

	RIFF_SET_ID(&buff[12], 'i','n','f','o');
	RIFF_SET_DWORD(&buff[16], 16);

	RIFF_SET_DWORD(&buff[20], 1);									// + 0 DW VERSION
	RIFF_SET_DWORD(&buff[24], all_sector_count * SPI_ERASE_SIZE);	// + 4 DW DISKSIZE
	RIFF_SET_DWORD(&buff[28], startaddr);							// + 8 DW DISK_TOPADDR
	RIFF_SET_WORD(&buff[32], rsv_top_sector);						// +12 W  RSV_TOP_SECTOR
	RIFF_SET_WORD(&buff[34], sat_top_sector);						// +14 W  SAT_TOP_SECTOR

	address = diskinfo_sector * SPI_ERASE_SIZE;

	for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
		if (spi_erase_sector(address) == RES_OK) break;
	}
	if (retry == 0) {
		dgb_printf("[!] diskinfo sector erase was failed. (0x%08x)\n", address);
		return RES_ERROR;
	}

	for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
		if (spi_program_page(buff, address) == RES_OK) break;
	}
	if (retry == 0) {
		dgb_printf("[!] diskinfo sector program was failed. (0x%08x)\n", address);
		return RES_ERROR;
	}


	return RES_OK;
}
#endif



/*-----------------------------------------------------------------------*/
/* Initialize a physical disk                                            */
/*-----------------------------------------------------------------------*/

DEF_SPIDISK spidiskinfo;

static DRESULT spidisk_init(void)
{
	DWORD memsize, id, infosector;
	DWORD disksize, startaddr, version;
	WORD rsv_top_sector, sat_top_sector;
	WORD all_sector_count, rsv_sector_count, sat_sector_count, dat_sector_count;
	BYTE buff[SPI_ERASE_SIZE];
	UINT i;

	/* ディスク情報テーブル読み出し */

	if (spi_getinfo(&memsize, &id)) return RES_NOTRDY;

	infosector = (memsize / SPI_ERASE_SIZE) - 1;
	spi_read(buff, infosector * SPI_ERASE_SIZE, SPI_ERASE_SIZE);

	dgb_printf("[INFO] diskinfo offset = 0x%08x (sector %d)\n",
					infosector * SPI_ERASE_SIZE, infosector);

	if ( !(RIFF_CHECK_ID(&buff[8], 'D','I','S','K') && RIFF_CHECK_ID(&buff[12], 'i','n','f','o')) ) {
		dgb_printf("    disk infomation data is not found.\n");
		return RES_NOTRDY;
	}

	version = RIFF_GET_DWORD(&buff[20]);			// + 0 DW VERSION
	disksize  = RIFF_GET_DWORD(&buff[24]);			// + 4 DW DISKSIZE
	startaddr = RIFF_GET_DWORD(&buff[28]);			// + 8 DW DISK_TOPADDR
	rsv_top_sector = RIFF_GET_WORD(&buff[32]);		// +12  W RSV_TOP_SECTOR
	sat_top_sector = RIFF_GET_WORD(&buff[34]);		// +14  W SAT_TOP_SECTOR

	dgb_printf("    signature :");
	for(i=0 ; i<8 ; i++) dgb_printf(" '%c'", buff[8+i]);
	dgb_printf("\n    version : %d\n", version);


	/* ティスク情報構造体の初期化 */

	all_sector_count = disksize / SPI_ERASE_SIZE;
	rsv_sector_count = sat_top_sector - rsv_top_sector;
	sat_sector_count = ((all_sector_count - rsv_sector_count)*2 / SPI_ERASE_SIZE) + 1;
	dat_sector_count = all_sector_count - rsv_sector_count - sat_sector_count;

	spidisk = &spidiskinfo;

	spidisk->storage_size = disksize;
	spidisk->top_address = startaddr;
	spidisk->rsv_top_sector = rsv_top_sector;
	spidisk->sat_top_sector = sat_top_sector;

	spidisk->pba_count = all_sector_count;
	spidisk->rsv_count = rsv_sector_count;
	spidisk->lba_count = dat_sector_count;

	spidisk->lba_table = NULL;
	spidisk->last_rsv_sector = 0;

	dgb_printf("    diskimage top offset = 0x%08x (sector %d)\n    reserve sector top = %d\n    sat sector top = %d\n",
					startaddr, startaddr / SPI_ERASE_SIZE,
					spidisk->rsv_top_sector, spidisk->sat_top_sector);

	dgb_printf("    physical sector count = %d\n    reserve sector count = %d\n    logical sector count = %d\n    sat sector count = %d\n",
					spidisk->pba_count, spidisk->rsv_count, spidisk->lba_count,
					sat_sector_count);

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Access a physical sector                                              */
/*-----------------------------------------------------------------------*/

static DRESULT read_physector(
	BYTE *buff,			/* Data buffer to store read data */
	DWORD sector		/* Sector address in Physical */
)
{
	DWORD address;

	if (spidisk == NULL) return RES_NOTRDY;
	address = spidisk->top_address + sector * SPI_ERASE_SIZE;

	return spi_read(buff, address, SPI_ERASE_SIZE);
}


#if _USE_SPI_WRITE
static DRESULT write_physector(
	const BYTE *buff,	/* Data to be written */
	DWORD sector		/* Sector address in Offset */
)
{
	DWORD address;
	UINT i,retry;

	if (spidisk == NULL) return RES_NOTRDY;
	address = spidisk->top_address + sector * SPI_ERASE_SIZE;

	for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
		if (spi_erase_sector(address) == RES_OK) break;
	}
	if (retry == 0) return RES_ERROR;

	for(i=SPI_ERASEPAGE_COUNT ; i>0 ; i--) {
		for(retry=SPI_RETRY_COUNT ; retry>0 ; retry--) {
			if (spi_program_page(buff, address) == RES_OK) break;
		}
		if (retry == 0) return RES_ERROR;

		buff += SPI_PAGE_SIZE;
		address += SPI_PAGE_SIZE;
	}

	return RES_OK;
}
#endif



/*-----------------------------------------------------------------------*/
/* LBA sector manager                                                    */
/*-----------------------------------------------------------------------*/

#if _USE_SPI_SATCACHE
static DRESULT lba_satload(void)
{
	WORD *p,*pcache;
	DWORD sector;
	UINT i,n;
	BYTE buff[SPI_ERASE_SIZE];

	if (spidisk == NULL) return RES_NOTRDY;

	if (spidisk->lba_table == NULL) {
		pcache = (WORD *)spiff_malloc(spidisk->lba_count * 2);

		if (pcache == NULL) goto error_exit;
	} else {
		pcache = spidisk->lba_table;
	}

	p = pcache;
	sector = spidisk->sat_top_sector;
	for(n=(spidisk->lba_count / (SPI_ERASE_SIZE/2))+1 ; n>0 ; n--) {
		if (read_physector(buff, sector++)) goto error_exit;

		for(i=0 ; i<SPI_ERASE_SIZE ; i+=2) *p++ = buff[i] | (buff[i+1] << 8);
	}

	spidisk->lba_table = pcache;

	return RES_OK;

error_exit:
	spiff_free(pcache);

	return RES_ERROR;
}
#endif


static DRESULT lba_getnumber(
	DWORD lba_sector,	/* Sector address in LBA */
	DWORD *phy_sector	/* Sector address in Physical */
)
{
	DWORD address;
	BYTE buff[2];

	if (spidisk == NULL) return RES_NOTRDY;
	if (lba_sector >= spidisk->lba_count) return RES_PARERR;

	if (_USE_SPI_SATCACHE && spidisk->lba_table != NULL) {
		*phy_sector = *(spidisk->lba_table + lba_sector);

	} else {
		address = spidisk->top_address + spidisk->sat_top_sector * SPI_ERASE_SIZE + lba_sector * 2;
		spi_read(buff, address, 2);

		*phy_sector = buff[0] | (buff[1] << 8);
	}

	return RES_OK;
}


#if _USE_SPI_WRITE
static DRESULT lba_remap(
	DWORD lba_sector	/* Sector address in LBA */
)
{
	DRESULT res;
	BYTE buff[SPI_ERASE_SIZE];
	WORD *p,t,rsv;
	DWORD satsector;
	UINT n,lba;

	if (spidisk == NULL) return RES_NOTRDY;
	if (lba_sector >= spidisk->lba_count) return RES_PARERR;


	/* 使われている代替セクタを検索 */

	rsv = spidisk->last_rsv_sector;

	if (rsv == 0) {
		if (_USE_SPI_SATCACHE && spidisk->lba_table != NULL) {
			p = spidisk->lba_table;
			for(lba=0 ; lba < spidisk->lba_count ; lba++,p++) {
				if (*p > rsv) rsv = *p;
			}

		} else {
			satsector = spidisk->sat_top_sector;
			n = 0;
			for(lba=0 ; lba < spidisk->lba_count ; lba++) {
				if ((lba & (SPI_ERASE_SIZE/2-1)) == 0) {
					if (read_physector(buff, satsector++)) return RES_ERROR;
					n = 0;
				}

				t = buff[n] | (buff[n+1] << 8);
				if (t > rsv) rsv = t;
				n += 2;
			}
		}

		if (rsv < spidisk->rsv_top_sector) rsv = spidisk->rsv_top_sector - 1;
	}


	/* LBA変換テーブルの更新 */

	rsv++;

	// 割り当てられる代替セクタがない 
	if (rsv >= spidisk->sat_top_sector) return RES_ERROR;

	// 代替セクタの割り当て 
	satsector = spidisk->sat_top_sector + (lba_sector / (SPI_ERASE_SIZE/2));

	if (_USE_SPI_SATCACHE && spidisk->lba_table != NULL) {
		*(spidisk->lba_table + lba_sector) = rsv;

		p = spidisk->lba_table + (lba_sector & ~(SPI_ERASE_SIZE/2-1));
		for(n=0 ; n<SPI_ERASE_SIZE ; n+=2,p++) {
			buff[n] = *p & 0xff;
			buff[n+1] = (*p >> 8) & 0xff;
		}

	} else {
		if (read_physector(buff, satsector)) return RES_ERROR;

		n = (lba_sector & (SPI_ERASE_SIZE/2-1)) * 2;
		buff[n] = rsv & 0xff;
		buff[n+1] = (rsv >> 8) & 0xff;
	}


	/* LBA変換テーブルの書き戻し */

	do {
		res = write_physector(buff, satsector);
		if (res) return RES_ERROR;
	} while(res);

	spidisk->last_rsv_sector = rsv;


	return RES_OK;
}
#endif



/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = 0;

	if (pdrv) return RES_PARERR;

	if (spidisk == NULL) stat |= STA_NOINIT;

	return stat;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv) return RES_PARERR;

	if (disk_status(pdrv) & STA_NOINIT) {

		if (spidisk_init()) return RES_NOTRDY;
#if _USE_SPI_SATCACHE
		lba_satload();
#endif
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Read LBA Sector                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DWORD offset;

	if (pdrv) return RES_PARERR;
	if (spidisk == NULL) return RES_NOTRDY;

	while(count) {
		if (lba_getnumber(sector, &offset)) break;
		if (read_physector(buff, offset)) break;

		buff += SPI_SECTOR_SIZE;
		sector++;
		count--;
	}

	return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write LBA Sector                                                      */
/*-----------------------------------------------------------------------*/

#if _USE_SPI_WRITE
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DWORD offset;

	if (pdrv) return RES_PARERR;
	if (spidisk == NULL) return RES_NOTRDY;

	while(count) {
		if (lba_getnumber(sector, &offset)) break;
		if (write_physector(buff, offset)) {
			if (lba_remap(sector)) break;
			continue;
		}

		buff += SPI_SECTOR_SIZE;
		sector++;
		count--;
	}

	return count ? RES_ERROR : RES_OK;
}
#endif



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res;

	if (spidisk == NULL) return RES_NOTRDY;

	res = RES_ERROR;
	switch (cmd) {
		case CTRL_SYNC :		/* Make sure that no pending write process */
			res = RES_OK;
			break;

		case GET_SECTOR_COUNT :	/* Get number of sectors on the disk (DWORD) */
			*(DWORD*)buff = spidisk->lba_count;
			res = RES_OK;
			break;

		case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
			*(DWORD*)buff = SPI_ERASE_SIZE / SPI_SECTOR_SIZE;
			res = RES_OK;
			break;

		case GET_SECTOR_SIZE :	/* Get sector size in unit of sector (DWORD) */
			*(DWORD*)buff = SPI_SECTOR_SIZE;
			res = RES_OK;
			break;

		default:
			res = RES_PARERR;
	}

	return res;
}
