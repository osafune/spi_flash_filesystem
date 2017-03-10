SPI Flash Filesystem
====================

SPI Flashメモリ上にファイルシステムを構築するドライバです。
ファイルシステムは[ChaN氏製作のFatFs](http://elm-chan.org/fsw/ff/00index_j.html) R0.12cを組み込んでいます。  

特徴
----
- 接続されているSPI Flashメモリのパラメータを自動で取得します。
- 代替セクタ機能を実装しており、SPI Flashの書き換え回数上限によるファイル破損を抑制できます。
- FPGAコンフィグレーションやブートコード用のために先頭アドレス側に任意サイズの予約領域を持つ事ができます。
- FatFsのコンフィグレーションに応じてコード量の圧縮を行います。


使用環境
========
- SPIマスタが使用可能なMCU
- NiosIIで全機能使用する場合は200kバイトのメモリ(HALおよびLFNのUNICODEテーブルを含む)
- 16Mbit～2Gbitで4kバイトイレースに対応しているSPI Flash

ソースコードは[PERIDOT Hostbridge](https://github.com/osafune/peridot_peripherals/tree/master/ip/peridot_hostbridge)のEPCS/EPCQにアクセスレジスタ用になっています。  
他のSPIマスタ環境で動作させる場合は、`spidisk.c`のspiアクセス部分を修正してください。  


使い方
======

1. 初回にSPI Flashへディスク領域を作成します。  
ディスク領域は1Mバイト～SPI Flashの最大容量の間で4kバイト単位で指定できます。  
ディスクイメージはボトムアドレス側から配置され、SPI Flashの容量よりも少ないディスクイメージを作成した場合は先頭アドレス側が未使用領域となります。
未使用領域はディスクとしては認識されないので、FPGAコンフィグレーションやブートコード用の領域として利用できます。  
SPI Flashのローレベルフォーマットは全セクタのチェックを行うため、時間がかかります。  
自動認識に対応していないSPI Flashや、ファイルシステムが実装できないSPI Flashの場合は`RES_NOTRDY`を返します。

2. FatFsのf_mkfsでFATボリュームを作成します。  
1および2が終わっていれば、通常のファイルシステムとしてアクセスすることができます。

3. f_open、f_read、f_writeで読み書き。  
ファイル書き込みではセクタ単位でイレースを行うため、書き込み速度は高速ではありません。  
セクタイレースからセクタ書き込み完了までのクリティカルフェーズに一定の時間がかかるため、書き込みを行う際には異常終了が発生しないよう注意を払う必要があります。  
FatFsで読み出し専用（_FS_READONLY == 1）にした場合、SPI Flashへは読み出し動作のみになります。

コード例
--------

```C
#include <stdio.h>
#include "fatfs/ff.h"
#include "spidisk.h"

int main(void)
{
    FATFS fs;           /* FatFs work area needed for each volume */
    FIL fil;            /* File object */
    FRESULT res;        /* API result code */
    UINT bw;            /* Bytes written */
    BYTE work[_MAX_SS]; /* Work area (larger is better for process time) */

    // SPIディスクのローレベルフォーマット
    res = spidisk_format(0, 0);
    if (res) {
        printf("[!] spidisk_format error %d\n\n", res);
        exit(-1);
    }

    // FATボリューム作成
    res = f_mkfs("", FM_ANY, 0, work, sizeof work);
    if (res) {
        printf("[!] f_mkfs error %d\n\n", res);
        exit(-1);
    }

    // ワークエリア登録
	f_mount(&fs, "", 0);

    // ファイルリード
    res = f_open(&fil, "message.txt", FA_READ);
    if (res) ...

    // ファイルライト
    f_write(&fil, "Hello, World!\r\n", 15, &bw);
    if (bw != 15) ...

    // ファイルクローズ
    f_close(&fil);

    ...
```

ポーティング
------------

`spidisk.c`のspi_waitreadyとspi_transactionがI/Oにアクセスする関数になります。動作環境に応じて修正してください。  

- `DWORD spi_waitready(void)`  
SPIトランザクションの終了を待ち、受信したデータを返します。

- `DWORD spi_transaction(DWORD)`  
1バイト送信／1バイト受信のトランザクションを行います。  
引数のbit0～7に送信バイト、bit8にデバイスセレクトが設定されます。  
返値のbit0～7に受信バイトをセットします。上位bitは不定でかまいません。  
デバイス非選択（SPI_SS_NEGATE）時にチップセレクトの最低ネゲート時間を満たすため、SS_nネゲート状態で1バイトの無効トランザクションを発行します。無効トランザクションが発行できないSPIマスタの場合は、何らかの代替手段にて時間待ちを実装してください。  


ライセンス
=========

The MIT License (MIT)  
Copyright (c) 2017 J-7SYSTEM WORKS LIMITED.

~~~~
/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT file system module  R0.12c                             /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 2017, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/----------------------------------------------------------------------------*/
~~~~
