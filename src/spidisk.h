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

// SPI�R���g���[���A�h���X(PERIDOT SWI EPCS/EPCQ�A�N�Z�X�|�[�g 
#define SPI_DEV					(PERIDOT_HOSTBRIDGE_BASE + 0x14)

// �Z�N�^�C���[�X/�y�[�W�v���O�����̍ő�҂�����(ms�P��)
#define SPI_ERASE_WAIT_MAX		(500)

// LBA�ϊ��e�[�u���L���b�V���̗L�� : 1=���p���� / 0=���Ȃ� 
#define _USE_SPI_SATCACHE		1

// SPI Flash�f�o�C�X�̎����F�� : 1=���� / 0=���Ȃ� 
#define _USE_SPI_AUTODETECT		1

// �����F�������Ȃ��ꍇ�̗e�ʒl(�o�C�g) 
#define SPI_FLASH_MEMSIZE		(16*1024*1024/8)



/*-----------------------------------------------------------------------*/
/* Function prototype                                                    */
/*-----------------------------------------------------------------------*/

typedef struct {
	DWORD storage_size;		// �f�B�X�N�C���[�W�̃T�C�Y(�o�C�g���E�u���b�N�P��) 
	DWORD top_address;		// �f�B�X�N�C���[�W�̐擪�A�h���X(�����T�C�Y���E�ɍ��킹��) 
	WORD rsv_top_sector;	// ��փZ�N�^�̐擪�I�t�Z�b�g�Z�N�^ 
	WORD sat_top_sector;	// LBA�ϊ��e�[�u���̐擪�I�t�Z�b�g�Z�N�^ 
	WORD pba_count;			// �����Z�N�^�̐� 
	WORD rsv_count;			// ��փZ�N�^�̐� 
	WORD lba_count;			// �_���Z�N�^�̐� 
	WORD *lba_table;		// LBA�ϊ��e�[�u���ւ̃|�C���^�i�L���b�V���l�j 
	WORD last_rsv_sector;	// �Ō�Ɋ��蓖�Ă�ꂽ��փZ�N�^�i�L���b�V���l�j 
} DEF_SPIDISK;


// SPI�f�B�X�N�����t�H�[�}�b�g 
DRESULT spidisk_format(
	DWORD disksize,			// ���蓖�ăf�B�X�N�T�C�Y(�o�C�g) 
	WORD rsv_count			// �\��Z�N�^�� 
);



#ifdef __cplusplus
}
#endif

#endif
