#pragma once

#include <string>

namespace Core {

int exportZipPass(std::string path);

int importZipPass(std::string path);

int importQueuedZipPass();

void trimZipPassHistory();

int clearStreetPassConfig();

} // namespace Core
