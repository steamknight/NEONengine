#ifndef __ASSERT_H__INCLUDED__
#define __ASSERT_H__INCLUDED__

#undef assert
#ifndef ACE_DEBUG
#define assert(e, m) ((void)0)
#else
#include <ace/types.h>
#define assert(e, m) ((void)((e) || (aceAssert(m, __FILE__, __LINE__), 0)))

void aceAssert(char const* szMessage, char const* szFile, ULONG ulLine);
#endif


#endif //__ASSERT_H__INCLUDED__