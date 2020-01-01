/**
 ****************************************************************************************
 *
 * @file   bx_crc32.h
 *
 * @brief  .
 *
 * @author  Hui Chen
 * @date    2019-01-16 15:45
 * @version <0.0.0.1>
 *
 * @license 
 *              Copyright (C) Apollo 2019
 *                         ALL Right Reserved.
 *
 ****************************************************************************************
 */
#ifndef APP_BX_CRC32_H_
#define APP_BX_CRC32_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */


/*
 * DEFINES
 ****************************************************************************************
 */
///IEEE802.3标准CRC32多项式   x32 + x26 + x23 + x22 + x16 + x12 + x11 + x10 + x8 + x7 + x5 + x4 + x2 + x+ 1
/// x32  则对应32bit = 1, x26 则对应26bit=1，得出一个值：(1<<32)|(1<<26)|(1<<23)|(1<<22)|...|(1<<1)|(1)=0x104C11DB7，对于CRC32取低32位，则＝0x4C11DB7
#define CRC32_POLY_VAL    0x04C11DB7
#define CRC32_INIT_VAL    0xFFFFFFFF
#define CRC32_XOR_VAL     0xFFFFFFFF
/*
 * MACROS
 ****************************************************************************************
 */
#define CRC32_XOR(crc32)  crc32 ^= CRC32_XOR_VAL
/*
 * ENUMERATIONS
 ****************************************************************************************
 */


/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */


/*
 * GLOBAL VARIABLE DECLARATIONS
 ****************************************************************************************
 */


/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief   Func    码表生成
 *                  如:X32+X26+...X1+1,poly=(1<<26)|...|(1<<1)|(1<<0)
 *
 * @param[in] poly     crc32 Polynomial (crc 多项式)
 *
 ****************************************************************************************
 */
extern void crc32_init(unsigned long poly);
/**
 ****************************************************************************************
 * @brief   Func    crc32 calc
 *                  如:X32+X26+...X1+1,poly=(1<<26)|...|(1<<1)|(1<<0)
 *
 * @param[in] crc     crc32 初值
 * @param[in] input   crc32  input data
 * @param[in] len     crc32  data len
 *
 * @return The result of crc32
 ****************************************************************************************
 */
extern unsigned long crc32_calc(unsigned long crc, void* input, unsigned long len);


#endif /* APP_BX_CRC32_H_ */ 
