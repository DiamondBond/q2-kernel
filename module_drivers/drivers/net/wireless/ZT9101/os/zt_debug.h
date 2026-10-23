/*
 * zt_debug.h
 *
 * used for debug
 *
 * Author: pansiwei
 *
 * Copyright (c) 2021 Shandong ZTop Microelectronics Co., Ltd
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __ZT_DEBUG_H__
#define __ZT_DEBUG_H__

#define ZT_DEBUG_DEBUG         0x01U
#define ZT_DEBUG_INFO          0x02U
#define ZT_DEBUG_WARN          0x04U
#define ZT_DEBUG_ERROR         0x08U
#define ZT_DEBUG_MASK          0x0FU

#ifdef ZT_DEBUG_ENABLE
#ifndef ZT_DEBUG_LEVEL
#define ZT_DEBUG_LEVEL   ZT_DEBUG_MASK /* all debug information */
#endif
#define ZT_DEBUG_COLOR
#else
#undef ZT_DEBUG_LEVEL
#define ZT_DEBUG_LEVEL   ZT_DEBUG_DEBUG /* only error information */
#undef ZT_DEBUG_COLOR
#endif

#ifdef ZT_RELEASE_VERSION
#undef ZT_DEBUG_LEVEL
#define ZT_DEBUG_LEVEL  ZT_DEBUG_ERROR
#endif

/*
 * The color for terminal (foreground)
 * BLACK    30
 * RED      31
 * GREEN    32
 * YELLOW   33
 * BLUE     34
 * PURPLE   35
 * CYAN     36
 * WHITE    37
 */
#ifdef ZT_DEBUG_COLOR
#define _ZT_DEBUG_HDR(lvl_name, color_n) \
    ZT_KERN_LEVELS "\033["#color_n"m["lvl_name"]"
#define _ZT_DEBUG_END   "\033[0m\n"
#else
#define _ZT_DEBUG_HDR(lvl_name, color_n)    ZT_KERN_LEVELS "["lvl_name"]"
#define _ZT_DEBUG_END   "\n"
#endif

#if (ZT_DEBUG_LEVEL & ZT_DEBUG_DEBUG)
#define LOG_D(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR("D", 0) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL & ZT_DEBUG_INFO)
#define LOG_I(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR("I", 32) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL & ZT_DEBUG_WARN)
#define LOG_W(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR("W", 33) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...)
#endif

#if (ZT_DEBUG_LEVEL & ZT_DEBUG_ERROR)
#define LOG_E(fmt, ...) \
    ZT_LOG_PRINT(_ZT_DEBUG_HDR("E", 31) fmt _ZT_DEBUG_END, ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...)
#endif

#ifndef zt_log_array
#if (ZT_DEBUG_LEVEL & ZT_DEBUG_DEBUG)
static zt_inline void zt_log_array(void *ptr, zt_u16 len)
{
    zt_u16 i = 0;
    zt_u16 num;
    zt_u8 *pdata = ptr;

#define NUM_PER_LINE    8
    ZT_LOG_PRINT(ZT_KERN_LEVELS "\r\n");
    for (i = 0, num = len / NUM_PER_LINE; i < num; i++, pdata += 8)
    {
        ZT_LOG_PRINT(ZT_KERN_LEVELS "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     pdata[0], pdata[1], pdata[2], pdata[3],
                     pdata[4], pdata[5], pdata[6], pdata[7]);
    }
    num = len % NUM_PER_LINE;
    if (num)
    {
        for (i = 0; i < num; i++)
        {
            ZT_LOG_PRINT(ZT_KERN_LEVELS "%02X", pdata[i]);
        }
    }
    ZT_LOG_PRINT(ZT_KERN_LEVELS "\r\n");
}
#else
#define zt_log_array(...)
#endif
#endif

#endif      /* END OF __ZT_DEBUG_H__ */
