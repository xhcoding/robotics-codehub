#ifndef XPT2046_H_
#define XPT2046_H_

#define XPT2046_XP 0x9C
#define XPT2046_YP 0xDC
#define XPT2046_VBAT 0xAC
#define XPT2046_AUX 0xEC

unsigned int XPT2046_readAd(unsigned char command);

#endif
