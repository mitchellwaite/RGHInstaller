#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "portable_endian.h"
#include <cassert>

typedef struct SMALL_BLOCK_SPARE_DATA
{
   unsigned char BlockID1;
   unsigned char BlockID0;
   unsigned char FsSequence0;
   unsigned char FsSequence1;
   unsigned char FsSequence2;
   unsigned char BadBlockIndicator;
   unsigned char FsSequence3;
   unsigned char FsSize1;
   unsigned char FsSize0;
   unsigned char FsFreePageCount;
   unsigned char FsUnused1;
   unsigned char FsUnused0;
   unsigned char ECC3; // first six bits are the FS block type, last two are part of the ECC
   unsigned char ECC2;
   unsigned char ECC1;
   unsigned char ECC0;
} SMALL_BLOCK_SPARE_DATA;

int getPhysicalBlockAddress(int block_id)
{
   // Small block NAND
   // 0x20 pages per block * 0x210 physical pagesize
   return block_id * 0x210 * 0x20;
}

int getLogicalBlockAddress(int block_id)
{
   // Small block NAND
   // 0x20 pages per block * 0x200 pagesize
   return block_id * 0x200 * 0x20;
}

void printSpareData(SMALL_BLOCK_SPARE_DATA spare)
{
   // Block ID is stored as a little endian value in the spare data
   uint16_t block_id = (((spare.BlockID0&0xF)<<8)+(spare.BlockID1));//le16toh(*(uint16_t *)&(spare.BlockID1));
   uint16_t fs_size = le16toh(*(uint16_t *)&(spare.FsSize1));
   uint32_t fs_version = (spare.FsSequence3 << 24) + (spare.FsSequence2 << 16) + (spare.FsSequence1 << 8) + spare.FsSequence0;


   //printf("Block ID 1: 0x%x\n",spare.BlockID1);
   //printf("Block ID 0: 0x%x\n",spare.BlockID0);

   printf("-- spare data dump --\n");
   
   printf("Block ID: 0x%x\n",block_id);
   printf("PBA: 0x%x\n",getPhysicalBlockAddress(block_id));
   printf("LBA: 0x%x\n",getLogicalBlockAddress(block_id));

   //printf("FS sequence: 0x%x 0x%x 0x%x 0x%x\n",spare.FsSequence0,spare.FsSequence1,spare.FsSequence2,spare.FsSequence3);
   printf("File Version: %d\n", fs_version);
   //printf("Bad Block Indicator: 0x%x\n",spare.BadBlockIndicator);
   printf("File Size: 0x%x\n",fs_size);
   //printf("FS free page Count: 0x%x\n",spare.FsPageCount);
   printf("Used pages in block: 0x%x\n",0x20 - spare.FsFreePageCount);

   //printf("FS Unused: 0x%x 0x%x\n",spare.FsUnused0,spare.FsUnused1);

   printf("FS Block Type: 0x%x\n",spare.ECC3&0x3f); // bottom 6 bits of ECC3 is the block type
   //printf("ECC: 0x%x 0x%x 0x%x 0x%x\n",spare.ECC3&0xC0,spare.ECC2,spare.ECC1,spare.ECC0);
}

uint8_t getMobileDatType(SMALL_BLOCK_SPARE_DATA spare)
{
   uint8_t FsBlockType = spare.ECC3&0x3f;

   if(FsBlockType > 0x30 && FsBlockType <= 0x39)
   {
      return FsBlockType;
   }
   else
   {
      return 0x0;
   }
}

int main(int argc, char * argv[])
{
   SMALL_BLOCK_SPARE_DATA sprd;

   assert(sizeof(SMALL_BLOCK_SPARE_DATA) == 0x10);

   FILE * fd = fopen("testimg.bin","rb");

   uint8_t mobileDatType = 0x0;

   for(int i = 0x0; i < 0x4000; i++)
   {
      // Seek to the spare data
      fseek(fd, getPhysicalBlockAddress(i) + 0x200, SEEK_SET);
      fread(&sprd,sizeof(SMALL_BLOCK_SPARE_DATA),1,fd);

      mobileDatType = getMobileDatType(sprd);

      if(mobileDatType != 0x0)
      {
         printf("Found Mobile%c in block %x\n",mobileDatType + 0x11,i);
         printSpareData(sprd);
         printf("\n");
      }
   }

   fclose(fd);

   return 0;
}