#pragma once

#include <vector>

#include "constants.h"

std::vector<Node*> GetEvictionSet(Node** array, const uint64_t setIndex);
