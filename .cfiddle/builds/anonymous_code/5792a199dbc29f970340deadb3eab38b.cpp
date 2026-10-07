

#include<cstdint>
#include<cstdlib>

extern "C" 
uint64_t* array(uint64_t * data, uint64_t size, uint64_t arg1) {
    uint64_t sum=0;
    for(uint x = 0; x < size; x++) {
        sum+=data[x];
    }

    data[0] = sum;
    return data;
}


// Cfiddle-signature=5792a199dbc29f970340deadb3eab38b