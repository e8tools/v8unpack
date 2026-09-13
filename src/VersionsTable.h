/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/

#ifndef V8UNPACK_VERSIONSTABLE_H
#define V8UNPACK_VERSIONSTABLE_H

#include <string>
#include <utility>
#include <vector>

namespace v8unpack {

int VersionsShow(const std::string &filename);
int VersionsGet(const std::string &filename, const std::string &block_name);
int VersionsSet(const std::string &filename, const std::string &block_name, const std::string &version);
int VersionsSet(const std::string &filename, const std::vector<std::pair<std::string, std::string>> &items);
int VersionsUpdate(const std::string &filename, const std::string &block_name);
int VersionsUpdate(const std::string &filename, const std::vector<std::string> &names);

}

#endif //V8UNPACK_VERSIONSTABLE_H
