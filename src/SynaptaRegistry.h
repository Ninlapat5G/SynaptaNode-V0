#pragma once
#include <vector>

// Forward declaration to avoid circular includes between
// SynaptaDevice (registers here) and SynaptaNode (reads here).
class SynaptaDevice;

// Static device registry — devices auto-register via their constructor.
// SynaptaNode picks them up during begin().
namespace _SynaptaRegistry {
    inline std::vector<SynaptaDevice*>& devices() {
        static std::vector<SynaptaDevice*> list;
        return list;
    }
}
