#include <afina/Storage.h>
#include <afina/execute/Set.h>

#include <iostream>
#include <sstream>

namespace Afina {
namespace Execute {
Logger& logger = Logger::Instance();
// memcached protocol: "set" means "store this data".
void Set::Execute(Storage &storage, const std::string &args, std::string &out) {

    logger.write("Set(", _key, "):", args);
    storage.Put(_key, args);
    out = "STORED";
//    for (int i = 0; i < 1000000; i++) {
//        out += "aaaaaaaaaaaaaaa";
//    }
}

} // namespace Execute
} // namespace Afina
