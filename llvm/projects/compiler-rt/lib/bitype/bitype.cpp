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
//#define PRINT_BAD_CASTING

typedef struct size_offset{
    uint32_t size;
    int32_t offset;
} size_offset;

static unsigned int BitypePageNum = 10;

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
        if(thisPtr == NULL) return;
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
        size_offset* tmp_soff = (size_offset*)&(level22[idx22]);
        tmp_soff->size = ArraySize;
        //level22[idx22] = (void *)ArraySize;
        return;
    }

    SANITIZER_INTERFACE_ATTRIBUTE
    void __bitype_direct_updateObjTrace(void *thisPtr, void* referenceAddr, const int offset){
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
    size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
    tmp_soff->size = 0;
    tmp_soff->offset = offset;
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
    void __bitype_updateObjTrace(void *thisPtr, void *referenceAddr, uint32_t TypeSize, uint32_t ArraySize, uint32_t offset){
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
            size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
            tmp_soff->size = 0;
            tmp_soff->offset = offset;
        }
        return;
    }
} // extern C

__attribute__((always_inline))
inline static void print_confusion_inline(int debug){
        if(debug < 0){

            #ifdef BITYPE_LOG
            IncVal(lookupwrong, 1);
            #endif

            #ifdef PRINT_BAD_CASTING
            printTypeConfusion(debug);
            #endif

        }else{
            if(!debugArray[debug]){
                debugArray[debug] = true;

                #ifdef PRINT_BAD_CASTING
                printTypeConfusion(debug);
                #endif

                #ifdef BITYPE_LOG
                IncVal(lookupwrong, 1);
                #endif
            }
        
        }
}

//TODO add source information
__attribute__((always_inline))
inline static void* bitype_verify_cast(void *sourcePtr, void *thisPtr, unsigned index, unsigned char code, int debugIndex){
    //printf("Hello, I am on the bitype verify cast\n");
    //IncVal(allCount, 1);
    if(sourcePtr == NULL) return nullptr;
    //printf("code is %u\n", code);
    index += 2;

    #ifdef BITYPE_LOG
    IncVal(lookupsum, 1);
    #endif

    // lookup the entry begin
    unsigned long idx1 = (unsigned long)sourcePtr >> L2_NUM & L1_MASK;
    void **level2 = BitypeLookupStart[idx1];
    if(level2 == NULL){
        //printf("miss it\n");

        #ifdef BITYPE_LOG
        IncVal(lookupmiss, 1);
        #endif

        return sourcePtr;
        
    }
    unsigned idx2 = (unsigned long)sourcePtr >> 3 & L2_MASK;
    idx2 = idx2 << 1;
    void *referenceAddr = level2[idx2];
    if(referenceAddr == NULL){
        //printf("miss it\n");

        #ifdef BITYPE_LOG
        IncVal(lookupmiss, 1);
        #endif

        return sourcePtr;   
    }
    // lookup the entry end

    if(sourcePtr != thisPtr){
        unsigned idx22 = idx2 + 1;
        //int OffsetTmp = ((size_offset)(level2[idx22])).offset;
        size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
        int OffsetTmp = tmp_soff->offset;
        if(OffsetTmp == -1)
            OffsetTmp = 0;

        long offset = ((char *)thisPtr - ((char *)sourcePtr - OffsetTmp));

        if(offset < 0){
        print_confusion_inline(debugIndex);
        
        #ifdef BITYPE_LOG
        IncVal(confusionSum, 1);
        #endif

            return nullptr;
        }

        idx1 = (unsigned long)thisPtr >> L2_NUM & L1_MASK;
        level2 = BitypeLookupStart[idx1];
        if(level2 == NULL){
        print_confusion_inline(debugIndex);
            
            #ifdef BITYPE_LOG
            IncVal(confusionSum, 1);
            #endif

            return nullptr;
        }
        idx2 = (unsigned long)thisPtr >> 3 & L2_MASK;
        idx2 = idx2 << 1;
        referenceAddr = level2[idx2];
        if(referenceAddr == NULL){
        print_confusion_inline(debugIndex);

            #ifdef BITYPE_LOG
            IncVal(confusionSum, 1);
            #endif

            return nullptr;   
        }
    }
    //cout << "Reference Addr is " << referenceAddr << endl;
    unsigned char * arr = (unsigned char *)referenceAddr;
    unsigned char tempChar = arr[index];
    if((tempChar^code) > tempChar){

        print_confusion_inline(debugIndex);

        #ifdef BITYPE_LOG
        IncVal(confusionSum, 1);
        #endif

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
        {
            level2[idx2] = NULL;
            level2[idx2+1] = NULL;
        }
    return;
    }

SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_direct_eraseArraySize(void *thisPtr){
    if(thisPtr == NULL) return;
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
        size_offset* tmp_soff = (size_offset*)&(level2[idx2]);
        uint32_t sized = tmp_soff->size;
        if(sized){
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
void __bitype_print_type_confusion(int debug){
     if(debug < 0){

            #ifdef BITYPE_LOG
            IncVal(lookupwrong, 1);
            #endif

            #ifdef PRINT_BAD_CASTING
            printTypeConfusion(debug);
            #endif

        }else{
            if(!debugArray[debug]){
                debugArray[debug] = true;

                #ifdef PRINT_BAD_CASTING
                printTypeConfusion(debug);
                #endif

                #ifdef BITYPE_LOG
                IncVal(lookupwrong, 1);
                #endif
            }
        
        }
}

SANITIZER_INTERFACE_ATTRIBUTE
void __bitype_handle_reinterpret_cast(void *thisPtr, void* referenceAddr, const int offset){
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
       size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
       if(tmp_soff->offset != -1)
        return;
    }        
    
    level2[idx2] = referenceAddr;
    size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
    tmp_soff->size = 0;
    tmp_soff->offset = -1;
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

  #ifdef BITYPE_LOG
  printResult();
  #endif

}

void InstallAtExitHandler() {
  atexit(BitypeAtExit);
}
__attribute__((section(".preinit_array"), used)) void(*_bitype_preinit)(void) = bitypeInitialization;