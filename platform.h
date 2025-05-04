#ifndef PLATFORM_H__
#define PLATFORM_H__

#ifdef __SDCC
#define MYCC __sdcccall(1)
#else
#define MYCC
#endif

#endif //PLATFORM_H__

