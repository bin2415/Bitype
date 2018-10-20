#include "bitype.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "sanitizer_common/sanitizer_common.h"
#include <execinfo.h>
#include <atomic>
#include <fstream>

using namespace std;

#define BITYPE_ERROR -10
#define BT_BUF_SIZE 100
#define PRINT_BAD_CASTING

static unsigned int BitypePageNum = 10;
//static void **BitypeSecondLevelEnd = NULL;
//static void **BitypeSecondLevelStart = NULL;

static size_t mmap_size = L2_BITS * sizeof(void*) * BitypePageNum * 2;
static size_t added_memory_size = L2_BITS * BitypePageNum * 2;
unsigned long REINTERPRET_MAGIC = 0xFFFFFFFFFFFFFF;
bool debugArray[10000] = {false};

std::atomic<unsigned long> count_index[10];

void IncVal(int index, int count){
    count_index[index].fetch_add(count);
}
__attribute__((always_inline))
inline void printTypeConfusion(int index) {
  int j, nptrs;
  void *buffer[BT_BUF_SIZE];
  char **strings;

#ifdef PRINT_BAD_CASTING
  printf("== Bitype Type Confusion Report Start==\n");
  printf("Debug location number is %d\n", index);
  //printf("%d %" PRIu64 " %" PRIu64 "\n", ErrorType, SrcHash, DstHash);

  nptrs = backtrace(buffer, BT_BUF_SIZE);
  strings = backtrace_symbols(buffer, nptrs);
  if (strings != NULL)
    for (j = 0; j < nptrs; j++)
      printf("%s\n", strings[j]);
  free(strings);
  printf("== Bitype Type Confusion Report End==\n");
#endif

}

//initialization of lookup table, reference of cfixx : https://github.com/HexHive/CFIXX
void bitypeInitialization(){
    size_t len = L1_BITS * sizeof(void*) + L2_BITS  * sizeof(void*) * BitypePageNum * 2;
    printf("Hello, initialization\n");
    if((BitypeLookupStart = (void ***)mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        fprintf(stderr, "[BITYPE ERROR] Mapping look up table failed\n");
        exit(BITYPE_ERROR);
    }
    printf("Hello, mmap first success\n");
    void** address1 = (void**)BitypeLookupStart;
    //cout << "lookup start address is " <<  BitypeLookupStart << endl;
    //mmaped bitype second level start address and end adderss
    BitypeSecondLevelStart = address1 + L1_BITS;
    printf("Bitype mapped address is %llx\n", BitypeLookupStart);
    printf("Bitype mmaped second start address is %llx\n", BitypeSecondLevelStart);
    //cout << "Bitype second level start address is" << BitypeSecondLevelStart << endl;
    BitypeSecondLevelEnd = address1 + len/sizeof(void*);
    printf("Bitype mmaped second end address is %llx\n", BitypeSecondLevelEnd);
    InstallAtExitHandler();
}

extern "C"{

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_update_arraySize(void *thisPtr, uint32_t ArraySize){
            // because in ptmalloc, the address -8 is the malloc size, so we can use it to imply the arraysize
        unsigned long arraySizeAddr = (unsigned long)thisPtr;
        unsigned long idx11 = arraySizeAddr >> L2_NUM & L1_MASK;
        void **level22 = BitypeLookupStart[idx11];
        unsigned idx22 = arraySizeAddr >> 3 & L2_MASK;
        if(level22 == NULL){
            if(BitypeSecondLevelStart >= BitypeSecondLevelEnd){
                printf("Hello, mmap second\n");
                if((BitypeSecondLevelStart = (void **)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
                    fprintf(stderr, "[BITYPE ERROR] Mapping second level failed\n");
                    exit(BITYPE_ERROR);
                }
                printf("Hello, mmap second success\n");
                BitypeSecondLevelEnd = BitypeSecondLevelStart + added_memory_size;
            }
            void **tmp = BitypeSecondLevelStart;
            BitypeLookupStart[idx11] = tmp;
            level22 = tmp;
            BitypeSecondLevelStart += L2_BITS * 2;
        }
        idx22 = idx22 << 1;
        idx22 += 1;
        level22[idx22] = (void *)ArraySize;
        return;
    }

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_direct_updateObjTrace(void *thisPtr, void* referenceAddr){
    //printf("update\n");
    unsigned long addressTmp = (unsigned long)thisPtr;
    unsigned long idx1 = (unsigned long)addressTmp >> L2_NUM & L1_MASK;
    void **level2 = BitypeLookupStart[idx1];

    unsigned long idx2 = (unsigned long)addressTmp >> 3 & L2_MASK;
    idx2 = idx2 << 1;
    unsigned long idx22 = idx2 + 1;

    //level2 has not be assigned 
    if(level2 == NULL){
        if(BitypeSecondLevelStart >= BitypeSecondLevelEnd){
            if((BitypeSecondLevelStart = (void **)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1 , 0)) == MAP_FAILED){
                fprintf(stderr, "[BITYPE ERROR] Mapping second level failed\n");
                exit(BITYPE_ERROR);
            }
            BitypeSecondLevelEnd = BitypeSecondLevelStart + added_memory_size;
        }
        void **tmp = BitypeSecondLevelStart;

        BitypeLookupStart[idx1] = tmp;
        //cout << "BitypeLookupStart idx1 " << BitypeLookupStart[idx1] << endl;
        level2 = tmp;
        BitypeSecondLevelStart += L2_BITS * 2;
    }

    level2[idx2] = referenceAddr;
    level2[idx22] = NULL;
    return;
}

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_malloc_new_space(){

                if((BitypeSecondLevelStart = (void **)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
                    fprintf(stderr, "[BITYPE ERROR] Mapping second level failed\n");
                    exit(BITYPE_ERROR);
                }
                BitypeSecondLevelEnd = BitypeSecondLevelStart + added_memory_size;
    }

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_updateObjTrace(void *thisPtr, void *referenceAddr, uint32_t TypeSize, uint32_t ArraySize){
        //printf("update\n");
        for(uint32_t i = 0; i < ArraySize; i++){
            unsigned long addr = ((unsigned long)thisPtr + (TypeSize * i));
            unsigned long idx1 = (unsigned long)addr >> L2_NUM & L1_MASK;
            void **level2 = BitypeLookupStart[idx1];
            unsigned idx2 = addr >> 3 & L2_MASK;
            idx2 = idx2 << 1;
            unsigned idx22 = idx2 + 1;

            if(level2 == NULL){
                if(BitypeSecondLevelStart >= BitypeSecondLevelEnd){
                    if((BitypeSecondLevelStart = (void **)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
                        fprintf(stderr, "[BITYPE ERROR] Mapping second level failed\n");
                        exit(BITYPE_ERROR);
                    }
                    BitypeSecondLevelEnd = BitypeSecondLevelStart + added_memory_size;
                }
                void **tmp = BitypeSecondLevelStart;
                BitypeLookupStart[idx1] = tmp;
                level2 = tmp;
                BitypeSecondLevelStart += L2_BITS * 2;
            }
            level2[idx2] = referenceAddr;
            level2[idx22] = NULL;
        }
        return;
    }
} // extern C


//TODO add source information
__attribute__((always_inline))
inline static void* bitype_verify_cast(void *sourcePtr, void *thisPtr, unsigned index, unsigned char code, int debugIndex){
    //printf("Hello, I am on the bitype verify cast\n");
    IncVal(allCount, 1);
    if(sourcePtr == NULL) return nullptr;
    //printf("code is %u\n", code);
    index += 2;
    IncVal(lookupsum, 1);

    unsigned long idx1 = (unsigned long)thisPtr >> L2_NUM & L1_MASK;
    void **level2 = BitypeLookupStart[idx1];
    if(level2 == NULL){
        //printf("miss it\n");
        IncVal(lookupmiss, 1);
        return thisPtr;
    }
    unsigned idx2 = (unsigned long)thisPtr >> 3 & L2_MASK;
    idx2 = idx2 << 1;
    void *referenceAddr = level2[idx2];
    if(referenceAddr == NULL){
        //printf("miss it\n");
        IncVal(lookupmiss, 1);
        return thisPtr;   
    }
    //cout << "Reference Addr is " << referenceAddr << endl;
    unsigned char * arr = (unsigned char *)referenceAddr;
    unsigned char tempChar = arr[index];
    unsigned short *tempArr = (unsigned short *)referenceAddr;
    //printf("tempChar is %u\n", tempChar);
    //unsigned char debugChar = tempChar ^ code;
    //printf("debug char is %u\n", debugChar); 
    if((tempChar^code) > tempChar){
        //printTypeConfusion();
        if(debugIndex < 0){
            IncVal(lookupwrong, 1);
            printf("typeconfusion class index is %u\n", (unsigned short)tempArr[0]);
            printTypeConfusion(debugIndex);
        }else{
            if(!debugArray[debugIndex]){
                debugArray[debugIndex] = true;
                printf("typeconfusion class index is %u\n", (unsigned short)tempArr[0]);
                printTypeConfusion(debugIndex);
                IncVal(lookupwrong, 1);
            }
        }
        IncVal(confusionSum, 1);
        return nullptr;        
    }
    //printf("Got it\n");
    return thisPtr;
}

extern "C"{
SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_direct_eraseObj(void *thisPtr){
    //cout << "Hello ? bitype direct erase obj\n";
    unsigned long idx1 = (unsigned long)thisPtr >> L2_NUM & L1_MASK;
    unsigned long idx2 = (unsigned long)thisPtr >> 3 & L2_MASK;
    idx2 = idx2 << 1;
    void **level2 = BitypeLookupStart[idx1];
    if(level2 != NULL)
        level2[idx2] = NULL;
    return;
    }

SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_direct_eraseArraySize(void *thisPtr){
    unsigned long idx1 = (unsigned long)thisPtr >> L2_NUM & L1_MASK;
    unsigned long idx2 = (unsigned long)thisPtr >> 3 & L2_MASK;
    idx2 = idx2 << 1;
    idx2 += 1;
    void **level2 = BitypeLookupStart[idx1];
    if(level2 != NULL)
        level2[idx2] = NULL;
    
    return;
    }
}

//__bitype_direct_eraseObj

extern "C"{
SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_eraseObj(void *thisPtr, void *objAddr, const uint32_t TypeSize, unsigned long ArraySize, const uint32_t AllocType){
    //cout << "hello?\n";
    if(AllocType == HEAPALLOC || AllocType == REALLOC){
        unsigned long idx1 = (unsigned long)objAddr >> L2_NUM & L1_MASK;
        unsigned long idx2 = (unsigned long)objAddr >> 3 & L2_MASK;
        //idx2 += 1;
        void **level2 = BitypeLookupStart[idx1];
        if(level2 == NULL){
            return;
        }
        
        idx2 = idx2 << 1;
        idx2 += 1;
        void *sized = level2[idx2];
        if(sized && (unsigned long)sized != REINTERPRET_MAGIC){
                ArraySize = (unsigned long)sized;
        }
        else
            ArraySize = 1;
    }
    //printf("The arraysize is %lld\n", ArraySize);
    for(uint32_t i = 0; i < ArraySize; i++){
        unsigned long addr = ((unsigned long)thisPtr + (TypeSize * i));
        unsigned long idx1 = addr >> L2_NUM & L1_MASK;
        unsigned long idx2 = addr >> 3 & L2_MASK;
        idx2 = idx2 << 1;
        void **level2 = BitypeLookupStart[idx1];
        if(level2 != NULL){
            level2[idx2] = NULL;
        }
    
    }
    return;
}

} //extern "C"


extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_cast_verification(void* sourcePtr, void *thisPtr, unsigned index, unsigned char code, int debugIndex){
    bitype_verify_cast(sourcePtr, thisPtr, index, code, debugIndex);
}

SANITIZER_INTERFACE_ATTRIBUTE
void* __bitype_dynamic_cast_verification(void *thisPtr, unsigned index, unsigned char code){
    return bitype_verify_cast(thisPtr, thisPtr, index, code, -1);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_print_type_confusion(int debugIndex){
    if(debugIndex < 0){
        //printTypeConfusion(debugIndex);
    }else if(!debugArray[debugIndex]){
        //printTypeConfusion(debugIndex);
        debugArray[debugIndex] = true;
        }
}

SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_handle_reinterpret_cast(void *thisPtr, void* referenceAddr){
    unsigned long addressTmp = (unsigned long)thisPtr;
    unsigned long idx1 = (unsigned long)addressTmp >> L2_NUM & L1_MASK;
    void **level2 = BitypeLookupStart[idx1];

    unsigned long idx2 = (unsigned long)addressTmp >> 3 & L2_MASK;
    idx2 = idx2 << 1;

    //level2 has not be assigned 
    if(level2 == NULL){
        if(BitypeSecondLevelStart >= BitypeSecondLevelEnd){
            if((BitypeSecondLevelStart = (void **)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1 , 0)) == MAP_FAILED){
                fprintf(stderr, "[BITYPE ERROR] Mapping second level failed\n");
                exit(BITYPE_ERROR);
            }
            BitypeSecondLevelEnd = BitypeSecondLevelStart + added_memory_size;
        }
        void **tmp = BitypeSecondLevelStart;

        BitypeLookupStart[idx1] = tmp;
        //cout << "BitypeLookupStart idx1 " << BitypeLookupStart[idx1] << endl;
        level2 = tmp;
        BitypeSecondLevelStart += L2_BITS * 2;
    }

    unsigned long idx22 = idx2 + 1;
    if(level2[idx2]){
        if(level2[idx22] == NULL || (unsigned long)level2[idx22] != REINTERPRET_MAGIC)
            return;
    }        
    
    level2[idx2] = referenceAddr;
    level2[idx22] = (void *) REINTERPRET_MAGIC;
    return;
}

} // extern "C"

extern "C"{
    SANITIZER_INTERFACE_ATTRIBUTE
    void __obj_update_count(uint32_t objUpdateType, uint64_t vla){
          switch (objUpdateType) {
            case STACKALLOC:
                IncVal(numStackUp, vla);
                break;
            case GLOBALALLOC:
                IncVal(numGloUp, vla);
                break;
            case HEAPALLOC:
            case REALLOC:
                IncVal(numHeapUp, vla);
                break;
  }
    }

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_debug_function(unsigned char temp){
        printf("Hello, I am here %u\n", temp);
    }
}

static void printResult(){
    //printf("print the result\n");
    string fileName = "./total_result.txt";
    ofstream ifile;
    ifile.open(fileName, ios::app | ios::out);
    ifile << "heap update count is " << count_index[numHeapUp].load() << "\n";
    ifile << "stack update count is " << count_index[numStackUp].load() << "\n";
    ifile << "global update count is " << count_index[numGloUp].load() << "\n";
    ifile << "all count num is " << count_index[allCount].load() << "\n";
    ifile << "cast verificate sum " << count_index[lookupsum].load() << "\n";
    ifile << "cast miss count is " << count_index[lookupmiss].load() << "\n";
    ifile << "print type confusion count is " << count_index[lookupwrong].load() << "\n";
    ifile << "type confusion count is " << count_index[confusionSum].load() << "\n";
    ifile.close();

}


static void BitypeAtExit(void) {
  printResult();
}

void InstallAtExitHandler() {
  atexit(BitypeAtExit);
}
__attribute__((section(".preinit_array"), used)) void(*_bitype_preinit)(void) = bitypeInitialization;