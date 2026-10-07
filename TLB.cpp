
//START
#include<cstdint>
#include<cstdlib>
#include<vector>
#include<algorithm>
#include"function_map.hpp"
#include <sys/mman.h>
#include <random> 

template<size_t BYTES>
struct MM {
    struct MM* next;  // I know that pointers are 8 bytes on this machine.
    uint64_t junk[BYTES/8 - 1]; // This forces the struct MM to take a up a whole cache line, abolishing spatial locality.
};


template<class MM>
MM *  __attribute__ ((noinline))  miss(MM * start, uint64_t count)  {
    for(uint64_t i = 0; i < count; i++) { // Here's the loop that does this misses. It's very simple.
        start = start->next;
    }
    return start;
}

template<size_t BYTES>
uint64_t* TLB(uint64_t * data, uint64_t size, uint64_t arg1) {
    struct MM<BYTES> * array = NULL;
    int r =  posix_memalign(reinterpret_cast<void**>(&array), 4096, size);
	if (r == -1) { 
		std::cerr << "posix_memalign() failed.  Exiting: " << strerror(errno) << "\n";
		exit(1);
    }

    r = madvise(reinterpret_cast<void*>(array), size, MADV_NOHUGEPAGE);
	if (r == -1) { 
		std::cerr << "madvise() failed.  Exiting: " << strerror(errno) << "\n";
		exit(1);
    }
    
    std::cout << "array alignment is " << (reinterpret_cast<uintptr_t>(array) % 4096) << "\n";
    std::cout << "array size is " << size/BYTES << " element; " << size << "B\n";
    
    // This is clever part  'index' is going to determine where the pointers go.  We fill it consecutive integers.
    std::vector<uint64_t> index;
    for(uint64_t i = 0; i < size/BYTES; i++) {
        index.push_back(i);
    }
    // Randomize the list of indexes.
//    std::random_shuffle(index.begin(), index.end());
    std::random_device rd;
    std::mt19937 g(rd());

    std::shuffle(index.begin(), index.end(), g);
    // Convert the indexes into pointers.
    for(uint64_t i = 0; i < size/BYTES; i++) {
        array[index[i]].next = &array[index[(i + 1) % (size/BYTES)]]; 
    } 

    MM<BYTES> * start = &array[0];
    start = miss(start, arg1); // 128 million accesses.
    return reinterpret_cast<uint64_t*>(start); // This is a garbage value, but if we don't return it, the compiler will optimize out the call to miss.
} 
//END

extern "C"
uint64_t* TLB_8(uint64_t * data, uint64_t size, uint64_t arg1) {
        return TLB<8>(data, size, arg1);
}

FUNCTION(one_array_1arg, TLB_8);

extern "C"
uint64_t* TLB_4096(uint64_t * data, uint64_t size, uint64_t arg1) {
        return TLB<4096>(data, size, arg1);
}

FUNCTION(one_array_1arg, TLB_4096);

extern "C"
uint64_t* TLB_4160(uint64_t * data, uint64_t size, uint64_t arg1) {
        return TLB<4160>(data, size, arg1);
}

FUNCTION(one_array_1arg, TLB_4160);

extern "C"
uint64_t* TLB_2M(uint64_t * data, uint64_t size, uint64_t arg1) {
        return TLB<2*1024*1024 + 64>(data, size, arg1);
}

FUNCTION(one_array_1arg, TLB_2M);

//-O3