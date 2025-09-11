#ifndef __ACEPP__LOG_H__INCLUDED__
#define __ACEPP__LOG_H__INCLUDED__

#include <ace/managers/log.h>

namespace ace
{
    class log_block
    {
        public:
        explicit log_block(char* pBlockName) : _pBlockName(pBlockName)
        {
            logBlockBegin(_pBlockName);
        }
        ~log_block() { logBlockEnd(_pBlockName); }

        private:
        char* _pBlockName;
    };

#ifdef ACE_DEBUG
#define ACE_LOG_BLOCK_IMPL(blockName, line) auto __log_block##line = ace::log_block((blockName))
#define ACE_LOG_BLOCK(blockName)            ACE_LOG_BLOCK_IMPL(blockName, __LINE__)
#define ACE_TRACE_IMPL(line)                auto __log_block##line = ace::log_block(const_cast<char*>(__func__))
#define ACE_TRACE()                         ACE_TRACE_IMPL(__LINE__)
#define ACE_LOG(domain, fmt, ...)           logWrite(domain ": " fmt, ##__VA_ARGS__)
#else
#define ACE_LOG_BLOCK(blockName)
#define ACE_TRACE()
#define ACE_LOG(domain, fmt, ...)
#endif

}  // namespace ace

#endif  // __ACEPP__LOG_H__INCLUDED__