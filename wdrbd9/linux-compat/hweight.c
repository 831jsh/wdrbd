﻿#ifndef __HWEIGHT_H__
#define __HWEIGHT_H__
#include "linux-compat/bitops.h"

/**
* hweightN - returns the hamming weight of a N-bit word
* @x: the word to weigh
*
* The Hamming Weight of a number is the total number of bits set in it.
*/

unsigned int hweight32(unsigned int w)
{
    unsigned int res = w - ((w >> 1) & 0x55555555);
    res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
    res = (res + (res >> 4)) & 0x0F0F0F0F;
    res = res + (res >> 8);
    return (res + (res >> 16)) & 0x000000FF;
}

unsigned int hweight16(unsigned int w)
{
    unsigned int res = w - ((w >> 1) & 0x5555);
    res = (res & 0x3333) + ((res >> 2) & 0x3333);
    res = (res + (res >> 4)) & 0x0F0F;
    return (res + (res >> 8)) & 0x00FF;
}

unsigned int hweight8(unsigned int w)
{
    unsigned int res = w - ((w >> 1) & 0x55);
    res = (res & 0x33) + ((res >> 2) & 0x33);
    return (res + (res >> 4)) & 0x0F;
}

unsigned long hweight64(__u64 w)
{
#if BITS_PER_LONG == 32
    return hweight32((unsigned int)(w >> 32)) + hweight32((unsigned int)w);
#elif BITS_PER_LONG == 64
#ifdef ARCH_HAS_FAST_MULTIPLIER
    w -= (w >> 1) & 0x5555555555555555ul;
    w = (w & 0x3333333333333333ul) + ((w >> 2) & 0x3333333333333333ul);
    w = (w + (w >> 4)) & 0x0f0f0f0f0f0f0f0ful;
    return (w * 0x0101010101010101ul) >> 56;
#else
    __u64 res = w - ((w >> 1) & 0x5555555555555555ul);
    res = (res & 0x3333333333333333ul) + ((res >> 2) & 0x3333333333333333ul);
    res = (res + (res >> 4)) & 0x0F0F0F0F0F0F0F0Ful;
    res = res + (res >> 8);
    res = res + (res >> 16);
    return (res + (res >> 32)) & 0x00000000000000FFul;
#endif
#endif
}
#endif
