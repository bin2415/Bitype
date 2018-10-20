#ifndef _BITYPE_BITYPE_H
#define _BITYPE_BITYPE_H

#define L1_BITS (1UL << 22)
#define L2_BITS (1UL << 23)
#define L1_MASK ((1UL << 22) - 1UL)
#define L2_MASK ((1UL << 23) - 1UL)
#define L2_NUM 26


#define STACKALLOC 1
#define HEAPALLOC 2
#define GLOBALALLOC 3
#define REALLOC 4
#define PLACEMENTNEW 5
#define REINTERPRET 6

#define numGloUp 0
#define numStackUp 1
#define numStackRm 2
#define numHeapUp 3
#define numHeapRm 4

#define lookupmiss 5
#define lookupsum 6
#define lookupwrong 7
#define allCount 8
#define confusionSum 9

void InstallAtExitHandler();
__attribute__ ((visibility ("default"))) void ***BitypeLookupStart;
__attribute__ ((visibility ("default"))) void **BitypeSecondLevelEnd;
__attribute__ ((visibility ("default"))) void **BitypeSecondLevelStart;
#endif