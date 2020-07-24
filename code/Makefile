CXX = g++
CXXFLAGS = -O3 -std=c++17
PTHREAD = -pthread
HUGEPAGE_FLAGS = LD_PRELOAD=libhugetlbfs.so HUGETLB_MORECORE=yes

PROGRAMS = testConstructingEvictionSet portAttack

all: $(PROGRAMS)

constructingEvictionSet.o: constructingEvictionSet.cpp constants.h
	$(CXX) $(CXXFLAGS) -c constructingEvictionSet.cpp

testConstructingEvictionSet: testConstructingEvictionSet.cpp \
	                     constructingEvictionSet.o constants.h
	$(CXX) $(CXXFLAGS) -o $@ testConstructingEvictionSet.cpp \
	constructingEvictionSet.o

portAttack: portAttack.cpp constructingEvictionSet.o constants.h
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o $@ \
	portAttack.cpp constructingEvictionSet.o

runTestConstructingEvictionSet: testConstructingEvictionSet
	$(HUGEPAGE_FLAGS) taskset -c 0 ./testConstructingEvictionSet

# Logical core to socket mapping for Intel Xeon E5-2650 v4.
# We want to enforce running the attack on a single socket.
#
# Socket 1:  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
#           24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35
# Socket 2: 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
#           36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
runPortAttack: portAttack
	$(HUGEPAGE_FLAGS) taskset -c \
	0,1,2,3,4,5,6,7,8,9,10,11,24,25,26,27,28,29,30,31,32,33,34,35 \
	./portAttack

clean:
	rm -f *.o $(PROGRAMS)
