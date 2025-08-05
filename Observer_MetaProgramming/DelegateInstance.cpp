#include "DelegateInstance.h"
#include <atomic>

namespace Delegates
{
    std::atomic<uint64_t> GDelegateNextID(1);

    uint64_t FDelegateHandle::GenerateNewID()
    {
        return GDelegateNextID.fetch_add(1, std::memory_order_relaxed);
    }
}



