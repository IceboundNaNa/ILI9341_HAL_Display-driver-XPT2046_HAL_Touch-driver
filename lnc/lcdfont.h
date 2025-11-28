/**
****************************************************************************************************
 * @file        lcdfont.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2021-10-16
 * @brief       包含12*12,16*16,24*24,32*32 四种LCD用ASCII字体
 ****************************************************************************************************
 */

#ifndef __LCDFONT_H
#define __LCDFONT_H

/*
 * 使用 'extern' 关键字来声明（Declare）这些字体数组。
 * 真正的定义（Definition）将被放在一个新的 lcdfont.c 文件中。
 * 这样就保证了这些数组在整个项目中只被定义一次。
 */

/* 12*12 ASCII字符集点阵 */
extern const unsigned char asc2_1206[95][12];

/* 16*16 ASCII字符集点阵 */
extern const unsigned char asc2_1608[95][16];

/* 24*24 ASICII字符集点阵 */
extern const unsigned char asc2_2412[95][36];

/* 32*32 ASCII字符集点阵 */
extern const unsigned char asc2_3216[95][64];

/* 图片数据 */
extern const unsigned char gImage_blue_archive[];
extern const unsigned char gImage_qq_logo[];

#endif /* __LCDFONT_H */