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


#ifndef _SPIDISK_DEFINED
#define _SPIDISK_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#include "fatfs/diskio.h"
#include <system.h>


/*-----------------------------------------------------------------------*/
/* Configuration                                                         */
/*-----------------------------------------------------------------------*/

// SPIコントローラアドレス(PERIDOT SWI EPCS/EPCQアクセスポート 
#define SPI_DEV					(PERIDOT_HOSTBRIDGE_BASE + 0x14)

// セクタイレース/ページプログラムの最大待ち時間(ms単位)
#define SPI_ERASE_WAIT_MAX		(500)

// LBA変換テーブルキャッシュの有無 : 1=利用する / 0=しない 
#define _USE_SPI_SATCACHE		1

// SPI Flashデバイスの自動認識 : 1=する / 0=しない 
#define _USE_SPI_AUTODETECT		1

// 自動認識をしない場合の容量値(バイト) 
#define SPI_FLASH_MEMSIZE		(16*1024*1024/8)



/*-----------------------------------------------------------------------*/
/* Function prototype                                                    */
/*-----------------------------------------------------------------------*/

typedef struct {
	DWORD storage_size;		// ディスクイメージのサイズ(バイト数・ブロック単位) 
	DWORD top_address;		// ディスクイメージの先頭アドレス(消去サイズ境界に合わせる) 
	WORD rsv_top_sector;	// 代替セクタの先頭オフセットセクタ 
	WORD sat_top_sector;	// LBA変換テーブルの先頭オフセットセクタ 
	WORD pba_count;			// 物理セクタの数 
	WORD rsv_count;			// 代替セクタの数 
	WORD lba_count;			// 論理セクタの数 
	WORD *lba_table;		// LBA変換テーブルへのポインタ（キャッシュ値） 
	WORD last_rsv_sector;	// 最後に割り当てられた代替セクタ（キャッシュ値） 
} DEF_SPIDISK;


// SPIディスク物理フォーマット 
DRESULT spidisk_format(
	DWORD disksize,			// 割り当てディスクサイズ(バイト) 
	WORD rsv_count			// 予約セクタ数 
);



#ifdef __cplusplus
}
#endif

#endif
