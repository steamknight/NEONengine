#ifndef __ACEPP__FILE_H___INCLUDED__
#define __ACEPP__FILE_H___INCLUDED__

#include <ace/utils/file.h>

#include <mtl/memory.h>

namespace ace
{
    using file_ptr = mtl::unique_ptr<tFile, ::fileClose>;
}  // namespace ace

#endif  // __ACEPP__FILE_H___INCLUDED__
