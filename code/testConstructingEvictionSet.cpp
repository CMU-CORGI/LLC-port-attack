#include <vector>

#include "constants.h"
#include "constructingEvictionSet.h"

int main() {
    Node* array = nullptr;
    std::vector<Node*> evictionSet = GetEvictionSet(&array, /*setIndex=*/0);

    delete [] array;

    return 0;
}
