#include "OutputConsole.h"
#include "AtgConsole.h"
#include "AtgInput.h"
#include "AtgUtil.h"
#include <time.h>
#include "Corona4G.h"
#include "Automation.h"
#include "keyextract.h"
#include "crypt\hmac_sha1.h"
#include "crypt\rc4.h"

extern "C" {
#include "xenon_sfcx.h"
}

#pragma comment(lib, "xav")
#pragma comment(lib, "xapilib")

extern "C" NTSTATUS XeKeysGetKey(WORD KeyId, PVOID KeyBuffer, PDWORD keyLength);

ATG::GAMEPAD*	m_pGamepad; // Gamepad for input
time_t start,end; //Timer times for measuring time difference
double tdiff; // Double for time difference
bool started = false, dumped = false, dump_in_progress = false, MMC = false, write = false, AutoMode = false, GotSerial = false;
unsigned int config = 0;
BYTE consoleSerial[0xC];

extern "C" VOID HalReturnToFirmware(DWORD mode); // To Shutdown console when done ;)
extern "C" VOID XInputdPowerDownDevice(DWORD flag); // To Kill controllers
static unsigned long bswap32(unsigned long t) { return ((t & 0xFF) << 24) | ((t & 0xFF00) << 8) | ((t & 0xFF0000) >> 8) | ((t & 0xFF000000) >> 24); } //For ECC calculation
VOID KillControllers()
{
	XInputdPowerDownDevice(0x10000000);
	XInputdPowerDownDevice(0x10000001);
	XInputdPowerDownDevice(0x10000002);
	XInputdPowerDownDevice(0x10000003);
}

bool CheckPage(BYTE* page)
{
	unsigned int* data = (unsigned int*)page;
	unsigned int i=0, val=0, v=0;
	unsigned char edc[4];	
	for (i = 0; i < 0x1066; i++)
	{
		if (!(i & 31))
			v = ~bswap32(*data++);		
		val ^= v & 1;
		v>>=1;
		if (val & 1)
			val ^= 0x6954559;
		val >>= 1;
	}

	val = ~val;
	// 26 bit ecc data
	edc[0] = ((val << 6) | (page[0x20C] & 0x3F)) & 0xFF;
	edc[1] = (val >> 2) & 0xFF;
	edc[2] = (val >> 10) & 0xFF;
	edc[3] = (val >> 18) & 0xFF;
	return ((edc[0] == page[0x20C]) && (edc[1] == page[0x20D]) && (edc[2] == page[0x20E]) && (edc[3] == page[0x20F]));
}

int HasSpare(char* filename)
{
	BYTE buf[0x630];
	FILE* fd;
	dprintf(MSG_CHECKING_FOR_SPARE, filename);
	if (fopen_s(&fd, filename, "rb") != 0)
	{		
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FOR_READING, filename);
		return -1;
	}
	if (fread_s(buf, 0x630, 0x630, 1, fd) != 1)
	{
		dprintf(MSG_ERROR MSG_UNABLE_TO_READ_0X630_BYTES_FROM, filename);
		fclose(fd);
		return -1;
	}
	fclose(fd);
	if (buf[0] != 0xFF && buf[1] != 0x4F)
	{
		dprintf(MSG_ERROR MSG_BAD_MAGIC, filename);
		return -1;
	}	
	for (int offset = 0; offset < 0x630; offset += 0x210)
	{
		if (CheckPage(&buf[offset]))
		{
			dprintf(MSG_SPARE_DETECTED, filename);
			return 0;
		}
	}
	dprintf(MSG_SPARE_NOT_DETECTED, filename);
	return 1;
}

void AutoCountdown(int timeout = 5)
{
	for (; timeout > 0; timeout--)
	{
		dprintf(MSG_YOU_HAVE_SECONDS_BEFORE_CONTINUE, timeout);
		Sleep(1000);
	}
	dprintf(MSG_TIMES_UP);
}

VOID flasher()
{
	if (!AutoMode)
	{
		dprintf(MSG_PRESS_START_TO_FLASH);
		for(;;)
		{
			m_pGamepad = ATG::Input::GetMergedInput();
			if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_START)
				break;
			else if (m_pGamepad->wPressedButtons)
				XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
		}
	}
	started = true;
	KillControllers();
	//ClearConsole();
	time(&start);
	dprintf(MSG_WARNING_DO_NOT_TOUCH_CONSOLE_OR_CONTROLLER);
	int tmp = HasSpare("game:\\updflash.bin");
	if (tmp == -1)
		XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
	bool isEcced = (tmp == 0);	
	if (!MMC)
	{
		if (!isEcced)
		{
			if (!AutoMode)
			{
				dprintf(MSG_WARNING_YOU_ARE_ABOUT_TO_FLASH_NO_SPARE_TO_SPARE_CONSOLE);
				for(;;)
				{
					m_pGamepad = ATG::Input::GetMergedInput();
					if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_START)
						break;
					else if (m_pGamepad->wPressedButtons)
						XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);				
				}
			}
			else
			{
				dprintf(MSG_WARNING_YOU_ARE_ABOUT_TO_FLASH_NO_SPARE_TO_SPARE_CONSOLE_AUTO);
				AutoCountdown();
			}
		}
		/*unsigned int r = */sfcx_init();
		//sfcx_printinfo(r);
#ifdef USE_UNICODE
		dprintf(L"\n\n");
#else
		dprintf("\n\n");
#endif
		try_rawflash("game:\\updflash.bin");
		sfcx_setconf(config);
	}
	else
	{
		if (isEcced)
		{
			if (!AutoMode)
			{
				dprintf(MSG_WARNING_YOU_ARE_ABOUT_TO_FLASH_SPARE_TO_NO_SPARE_CONSOLE);
				for(;;)
				{
					m_pGamepad = ATG::Input::GetMergedInput();
					if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_START)
						break;
					else if (m_pGamepad->wPressedButtons)
						XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
				}
			}
			else
			{
				dprintf(MSG_WARNING_YOU_ARE_ABOUT_TO_FLASH_SPARE_TO_NO_SPARE_CONSOLE_AUTO);
				AutoCountdown();
			}
		}
		try_rawflash4g("game:\\updflash.bin");
	}
	time(&end);
	tdiff = difftime(end,start);
	dprintf(MSG_COMPLETED_AFTER_SECONDS, tdiff);
	dprintf(MSG_REBOOTING_IN);
	for (int i = 5; i > 0; i--)
	{
#ifdef USE_UNICODE
		dprintf(L"%i", i);
#else
		dprintf("%i", i);
#endif
		for (int j = 0; j < 4; j++)
		{
			Sleep(250);
#ifdef USE_UNICODE
			dprintf(L".");
#else
			dprintf(".");
#endif
		}
	}
	dprintf(MSG_BYE);
	HalReturnToFirmware(2);
}

VOID dumper(char *filename)
{
	started = true;
	if (!MMC)
	{
		sfcx_init();
		sfcx_setconf(config);
		int size = sfc.size_bytes_phys;
		if((size == (RAW_NAND_64*4)) || (size == (RAW_NAND_64*8)))
		{
			if (!AutoMode)
			{
				dprintf(MSG_PRESS_A_TO_DUMP_SYSTEM_ONLY);
				dprintf(MSG_PRESS_B_TO_DUMP_FULL_NAND);
				dprintf(MSG_PRESS_BACK_TO_ABORT_DUMP);
				for(;;)
				{
					m_pGamepad = ATG::Input::GetMergedInput();
					if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_A)
					{
						size = RAW_NAND_64;
						break;
					}
					else if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_B)
						break;
					else if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_BACK)
						XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
					else if (m_pGamepad->wPressedButtons)
					{
						dprintf(MSG_TRY_AGAIN);
						dprintf(MSG_PRESS_A_TO_DUMP_SYSTEM_ONLY);
						dprintf(MSG_PRESS_B_TO_DUMP_FULL_NAND);
						dprintf(MSG_PRESS_BACK_TO_ABORT_DUMP);
					}
				}
			}
			else
			{
				if (size == (RAW_NAND_64*4))
					size = 256;
				else
					size = 512;
				dprintf(MSG_BB_DETECTED_SETTING_64MB, size);
				size = RAW_NAND_64;
			}
		}
		//ClearConsole();
		time(&start);
		/*unsigned int r = */sfcx_init();
		//sfcx_printinfo(r);
		try_rawdump(filename, size);
		sfcx_setconf(config);
	}
	else
	{
		//ClearConsole();
		time(&start);
		try_rawdump4g(filename);
	}
	time(&end);
	tdiff = difftime(end,start);
	dprintf(MSG_COMPLETED_AFTER_SECONDS, tdiff);
	dumped = true;
}

void PrintExecutingCountdown(int max)
{
	for (; max > 0; max--)
	{
		dprintf(MSG_EXECUTING_COMMAND_IN_SECONDS, max);
		Sleep(1000);
	}
	dprintf(MSG_EXECUTING_COMMAND);
}

void TryAutoMode()
{
	dprintf(MSG_LOOKING_FOR_CMD_FILE_FOR_AUTO_MODE);
	if (fexists("game:\\simpleflasher.cmd"))
	{
		AutoMode = true;
		dprintf(MSG_SIMPLEFLASHER_CMD_FOUND_ENTERING_AUTO);
		int mode = CheckMode("game:\\simpleflasher.cmd");

#ifdef READ_ONLY
		if ( (mode == 2) || (mode == 3) )
		{
			dprintf(MSG_READ_ONLY_RETURNING_TO_MANUAL_MODE);
			AutoMode = false;
			return;
		}
#endif

		if (mode == 1) //AutoDump
		{
			dprintf(MSG_AUTO_DUMP_FOUND);
			dumper("game:\\flashdmp.bin");
			GenerateHash("game:\\flashdmp.bin");
		}
		else if (mode == 2) //AutoFlash
		{
			dprintf(MSG_AUTO_FLASH_FOUND);
			if (CheckHash("game:\\updflash.bin"))
			{
				PrintExecutingCountdown(5);
				flasher();
			}
			else
				dprintf(MSG_ERROR MSG_HASH_DONT_MATCH);
		}
		else if (mode == 3) //AutoSafeFlash
		{
			dprintf(MSG_AUTO_SAFE_FLASH_FOUND);
			if (CheckHash("game:\\updflash.bin"))
			{
				PrintExecutingCountdown(5);
				dumper("game:\\recovery.bin");
				AutoCountdown();
				flasher();
			}
			else
				dprintf(MSG_ERROR MSG_HASH_DONT_MATCH);
		}
		else if (mode == 4) //AutoExit, only want key...
		{
			dprintf(MSG_AUTO_EXIT_FOUND);
			PrintExecutingCountdown(5);
		}
		else if (mode == 5) //AutoReboot Hard Reset
		{
			dprintf(MSG_AUTO_REBOOT_FOUND);
			PrintExecutingCountdown(5);
			HalReturnToFirmware(2);
		}
		else
		{
			dprintf(MSG_BAD_COMMAND_FILE_RETURNING_TO_MANUAL_MODE);
			AutoMode = false;
			return;
		}
		if (AutoMode)
			XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0); //We don't want to return to manual mode ;)
	}
	else
	{
		dprintf(MSG_COMMAND_FILE_NOT_FOUND_ENTERING_MANUAL_MODE);
	}
}

void PrintConsoleInfo(bool GotKey)
{
	dprintf(MSG_CONSOLE_INFO_LINE);
	PrintDash();
    PrintConsoleType();
	if (GotKey)
		PrintCPUKey();
	if (GotSerial)
		dprintf(MSG_CONSOLE_SERIAL, consoleSerial);
	dprintf(MSG_CONSOLE_INFO_BOTTOM);
}

bool CheckGameMounted() {
	FILE * fd;
	if (fopen_s(&fd, "game:\\test.tmp", "w") != 0)
	{
		dprintf(MSG_ERROR MSG_GAME_NOT_MOUNTED_TRYING_USB);
		fclose(fd);
		if (mount("game:", "\\Device\\Mass0") != 0)
		{
			dprintf(MSG_ERROR MSG_GAME_NOT_MOUNTED_TRYING_HDD);
			if (mount("game:", "\\Device\\Harddisk0\\Partition1") != 0)
			{
				dprintf(MSG_ERROR MSG_GAME_NOT_MOUNTED);
				return false;
			}
		}
	}
	else
	{
		fclose(fd);
		remove("game:\\test.tmp");
	}
	return true;
}

int bytesToUint32(unsigned char * data, int offset)
{
	return *(int *)(data + offset);
}

void print_key(unsigned char * key, char * keyName)
{
	dprintf("%s: ", keyName);
	for (int i = 0; i < 0x10; i++)
	{
		dprintf("%02X", key[i]);
	}
	dprintf("\n");
}

// Get the number of pages that contain an
// object of the given size. Set phys to 
// true if the size is physical w/ ECC
DWORD getLogicalPageCount(DWORD size)
{
	return size / 0x200;
}

// Convert a logical NAND address into a physical
// offset in the NAND image. Logical page size is
// 0x200, physical page size is 0x210
DWORD logicalToPhysicalAddr(DWORD logicalAddr)
{
	int pageNum = logicalAddr / 0x200;
	int offsetInPage = logicalAddr % 0x200;

	return ( pageNum * 0x210 ) + offsetInPage;
}

int seekAndReadDataObject(long offset, void * ptr, size_t size, FILE * fd)
{
	//dprintf("read %x %p %x %p\n",offset, ptr, size, fd);

	if(fseek(fd,offset,SEEK_SET))
	{
		return -1;
	}

	if(fread(ptr,size,1,fd) != 1)
	{
		return -2;
	}

	return 0;
}

int seekAndReadDWORD(long offset, DWORD * data, FILE * fd)
{
	return seekAndReadDataObject(offset, data, sizeof(DWORD), fd);
}

int seekAndWriteDataObject(long offset, void * ptr, size_t size, FILE * fd)
{
	//dprintf("write %x %p %x %p\n",offset, ptr, size, fd);

	if(fseek(fd,offset,SEEK_SET))
	{
		return -1;
	}

	if(fwrite(ptr,size,1,fd) != 1)
	{
		return -2;
	}

	return 0;
}

int patch_raw_kv_pages(char * flashdmp, char * updflash, unsigned char cpukey[0x10], bool bIsMMCConsole)
{
	int rc = 0;

	FILE * fd;
	unsigned char rc4_state[0x100];
	HMAC_SHA1_CTX ctx;

	BYTE srcKvPages[0x8400] = {'\0'};
	BYTE srcKvHmacKey[0x10] = {'\0'};
	BYTE srcKvRc4Key[0x10] = {'\0'};

	DWORD srcKvSize = 0;
	DWORD srcKvOffset = 0;
	DWORD srcKvSizePhys = 0;
	DWORD srcKvOffsetPhys = 0;
	DWORD srcKvPageCount = 0;

	DWORD destKvSize = 0;
	DWORD destKvOffset = 0;
	BYTE destKvPages[0x8400] = {'\0'};
	BYTE destCpuKey[0x10] = {'\0'};
	BYTE destKvHmacSecret[] = { 0x07, 0x12 };
	BYTE out[0x20] = { '\0' };

	BYTE destKvHmacKey[0x10] = {'\0'};
	BYTE destKvRc4Key[0x10] = {'\0'};

	BYTE kvPagesNoEcc[0x8000] = {'\0'};
	BYTE kvPagesNoEccCopy[sizeof(kvPagesNoEcc)] = {'\0'};

	DWORD patchSlotAddr = 0;
	DWORD patchSlotSize = 0;
	DWORD vfusesOffset = 0;
	BYTE fuseline0[] = {0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

	if(fopen_s(&fd,flashdmp,"rb"))
	{
		dprintf(" ! Unable to open source NAND image %s\n", flashdmp);
		return -1;
	}

	// DWORD 0x60 stores the KV size. We're going to enforce that this
	// only works on retail systems with size 0x4200
	rc = seekAndReadDWORD(0x60, &srcKvSize, fd);

	if( 0 != rc )
	{
		dprintf(" ! Error (%d): unable to read source KV size\n", rc);
		fclose(fd);
		return -1;
	}

	// DWORD 0x6C stores the KV offset. We're going to enforce that this
	// only works on retail systems with offset 0x4200
	rc = seekAndReadDWORD(0x6C, &srcKvOffset, fd);

	if( 0 != rc )
	{
		dprintf(" ! Error (%d): unable to read source KV offset\n", rc);
		fclose(fd);
		return -1;
	}

	// At this time, only retail style images with KV at 0x4000 that are 0x4000 bytes large are supported
	if( srcKvSize != 0x4000 || srcKvOffset != 0x4000 )
	{
		dprintf(" ! Error, unsupported source image. Only retail KVs are supported.\n");
		fclose(fd);
		return -1;
	}

	if( srcKvSize > sizeof(kvPagesNoEcc) )
	{
		dprintf(" ! Error, unsupported source image. KV size is larger than data buffer.\n");
		fclose(fd);
		return -1;
	}

	srcKvPageCount = getLogicalPageCount(srcKvSize);

	// If this is an MMC console physical offset == logical offset (corona's)
	if(bIsMMCConsole)
	{
		srcKvSizePhys = srcKvSize;
		srcKvOffsetPhys = srcKvOffset;
	}
	else
	{
		srcKvSizePhys =  srcKvPageCount * 0x210;
		srcKvOffsetPhys = logicalToPhysicalAddr(srcKvOffset);
	}

	dprintf(" * KV Size: 0x%x\n * KV Offset: 0x%x\n * Page Count: 0x%x\n",srcKvSizePhys,srcKvOffsetPhys,srcKvPageCount);
	
	rc = seekAndReadDataObject(srcKvOffsetPhys,srcKvPages,srcKvSizePhys,fd);

	fclose(fd);

	if(0 != rc)
	{
		dprintf(" ! Error: unable to read source KV\n");
		return -1;
	}

	// Copy the encrypted KV without ECC data to the buffer
	for(UINT i = 0; i < srcKvPageCount; i++)
	{
		memcpy(&kvPagesNoEcc[0x200 * i],&srcKvPages[0x210 * i], 0x200);
	}

	//fd = fopen("game:\\kv_enc.bin","wb");
	//fwrite(kvPagesNoEcc,0x4000,1,fd);
	//fclose(fd);

	// Calculate the decryption key
	memcpy(srcKvHmacKey,kvPagesNoEcc,0x10);
	HMAC_SHA1(cpukey, srcKvHmacKey, srcKvRc4Key, 0x10);

	//print_key(cpukey, "CPU Key");
	//print_key(srcKvHmacKey, "Source HMAC Key");
	//print_key(srcKvRc4Key, "Source RC4 Key");

	// Decrypt the KV
	memset(rc4_state, 0, 0x100);
	rc4_init(rc4_state, srcKvRc4Key, 0x10);
	rc4_crypt(rc4_state, (unsigned char*) &kvPagesNoEcc[0x10], srcKvSize - 0x10);

	//fd = fopen("game:\\kv_dec.bin","wb");
	//fwrite(kvPagesNoEcc,0x4000,1,fd);
	//fclose(fd);

	// Get the destination KV data and such
	if(fopen_s(&fd,updflash,"rb+"))
	{
		dprintf(" ! failed to fopen %s", updflash);
		return -1;
	}

	// 0x64 contains the patch slot address
	// 0x70 contains the patch slot size
	rc = seekAndReadDWORD(0x64, &patchSlotAddr, fd);

	if( 0 != rc )
	{
		dprintf(" ! Error (%d): couldn't read patch slot address\n");
		fclose(fd);
		return -1;
	}

	rc = seekAndReadDWORD(0x70, &patchSlotSize, fd);

	// Certain xeBuild image types don't set the patch slot size correctly
	// Bail out early here if the patch slot size comes back as 0
	if( 0 != rc || 0 == patchSlotSize )
	{
		dprintf(" ! Error (%d): couldn't read patch slot size\n", rc);
		fclose(fd);
		return -1;
	}

	// VFuses and kernel patches are always in the second patch slot
	vfusesOffset = logicalToPhysicalAddr(patchSlotAddr + patchSlotSize);

	dprintf(" * Offset of virtual fuses: 0x%x\n",vfusesOffset);

	// Sanity check, we'll read in fuseline 0
	rc = seekAndReadDataObject(vfusesOffset, destCpuKey, 0x8, fd);

	if( rc != 0 || memcmp(destCpuKey,fuseline0,0x8) != 0 )
	{
		dprintf(" ! Error (%d): couldn't find vfuses at offset %x\n",rc,vfusesOffset);
		fclose(fd);
		return -1;
	}

	// Fuseline 4 and 5 make up the CPU key, and are located at
	// the vfuse offset + 0x20
	rc = seekAndReadDataObject(vfusesOffset + 0x20, destCpuKey, 0x10, fd);

	if( rc != 0 )
	{
		dprintf(" ! Error (%d): couldn't read Virtual CPU key\n",rc);
		fclose(fd);
		return -1;
	}

	print_key(destCpuKey, " * New Virtual CPU Key");
	SaveCPUKey("game:\\cpukey.txt",destCpuKey);

	// DWORD 0x60 stores the KV size. We're going to enforce that this
	// only works on retail systems with size 0x4200
	rc = seekAndReadDWORD(0x60, &destKvSize, fd);

	if( 0 != rc )
	{
		dprintf(" ! Error (%d): unable to read destination KV size\n", rc);
		fclose(fd);
		return -1;
	}

	// DWORD 0x6C stores the KV offset. We're going to enforce that this
	// only works on retail systems with offset 0x4200
	rc = seekAndReadDWORD(0x6C, &destKvOffset, fd);

	if( 0 != rc )
	{
		dprintf(" ! Error (%d): unable to read destination KV offset\n", rc);
		fclose(fd);
		return -1;
	}

	if( srcKvOffset != destKvOffset || srcKvSize != destKvSize )
	{
		dprintf(" ! Error: source and destination KV size and offset do not match!\n", rc);
		dprintf(" ! Dest KV Size: 0x%x\nDest KV Offset: 0x%x\n",destKvSize,destKvOffset);
		fclose(fd);
		return -1;
	}

	// We know that the source and destination KV size and offset are identical,
	// so we can use all the "source" physical offsets without recalculating
	rc = seekAndReadDataObject(srcKvOffsetPhys, destKvPages, srcKvSizePhys, fd);

	if(0 != rc)
	{
		dprintf(" ! Error (%d): unable to read destination KV\n", rc);
		fclose(fd);
		return -1;
	}

	// Now that we know our destination CPU key, re-encrypt the KV
	// This code is all borrowed from libxenon
	memcpy(kvPagesNoEccCopy,kvPagesNoEcc,sizeof(kvPagesNoEcc));

	HMAC_SHA1_Init(&ctx);
	HMAC_SHA1_UpdateKey(&ctx, (unsigned char *) destCpuKey, 0x10);
	HMAC_SHA1_EndKey(&ctx);

	HMAC_SHA1_StartMessage(&ctx);

	HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*) &kvPagesNoEccCopy[0x10], destKvSize - 0x10);
	HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*) &destKvHmacSecret[0x00], 0x02);	//Special appendage

	HMAC_SHA1_EndMessage(out, &ctx);
	HMAC_SHA1_Done(&ctx);

	// Memcpy our HMAC key out of the result and calculate
	// the RC4 encryption key
	memcpy(destKvHmacKey,out,0x10);
	HMAC_SHA1(destCpuKey, destKvHmacKey, destKvRc4Key, 0x10);

	//print_key(destKvHmacKey, "Destination HMAC Key");
	//print_key(destKvRc4Key, "Destination RC4 Key");

	// Re-encrypt the KV
	memset(rc4_state, 0, 0x100);
	rc4_init(rc4_state, destKvRc4Key ,0x10);
	rc4_crypt(rc4_state, (unsigned char*) &kvPagesNoEcc[0x10], destKvSize - 0x10);

	// Copy the HMAC key
	memcpy(kvPagesNoEcc,destKvHmacKey,0x10);

	// Copy the KV pages to the destination
	for(UINT i = 0; i < srcKvPageCount; i++)
	{
		// Copy just the data of the page
		memcpy(&destKvPages[i * 0x210],&kvPagesNoEcc[i * 0x200],0x200);

		// Zero out and recalculate the ECC bytes
		memset(&destKvPages[(i * 0x210) + 0x20C],0,4);
		sfcx_calcecc((unsigned int *)&destKvPages[i * 0x210]);
	}

	rc = seekAndWriteDataObject(srcKvOffsetPhys,destKvPages,srcKvSizePhys,fd);

	fclose(fd);

	if( 0 != rc )
	{
		dprintf("Error (%d): unable to write destination KV\n", rc);
		return -1;
	}

	return 0;
}

//--------------------------------------------------------------------------------------
// Name: main()
// Desc: Entry point to the program
//--------------------------------------------------------------------------------------
VOID __cdecl main()
{
	char * baseImagePath = "";
	unsigned char cpukey[0x10];

	// Initialize the console window
	MakeConsole("embed:\\font", CONSOLE_COLOR_BLUE, CONSOLE_COLOR_WHITE);
	if (!CheckGameMounted())
		return;

#ifdef TRANSLATION_BY
#ifdef USE_UNICODE
	dprintf(L"Simple 360 NAND Flasher by Swizzy v1.5 (BETA)\n");
#else
	dprintf("Simple 360 NAND Flasher by Swizzy v1.5 (BETA)\n");
#endif
	dprintf(TRANSLATION_BY);
#else
	dprintf("Simple RGH Installer (BETA)\n\n");
#endif

#ifdef READ_ONLY
	dprintf(MSG_READ_ONLY_NOTICE);
#endif

	//dprintf(MSG_DETECTING_NAND_TYPE);
	MMC = (sfcx_detecttype() == 1); // 1 = MMC, 0 = RAW NAND
	if (!MMC)
		config = sfcx_getconf();
	bool GotKey = false;
	//dprintf(MSG_ATTEMTPING_TO_GRAB_CPUKEY);
	if (GetCPUKey((unsigned char *)&cpukey))
	{
		//SaveCPUKey("game:\\cpukey.txt");
		GotKey = true;
	}
	else
	{
		dprintf(MSG_ERROR MSG_INCOMPATIBLE_DASHLAUNCH);
		Sleep(2000);
		return;
		//GotKey = false;
	}
	//dprintf(MSG_ATTEMPTING_TO_GET_CONSOLE_SERIAL);
	DWORD dwtmp = 0xC;
	GotSerial = XeKeysGetKey(0x14, consoleSerial, &dwtmp) >= 0;
	PrintConsoleInfo(GotKey);

	switch(CONSOLE_TYPE_FROM_FLAGS)
	{
		case CONSOLE_TYPE_FALCON:
			if (!fexists("game:\\images\\falcon_glitch2m_rgh3.bin"))
			{
				dprintf("Falcon base image is missing. Exiting...\n");
				Sleep(5000);
				return;
			}

			baseImagePath = "game:\\images\\falcon_glitch2m_rgh3.bin";
			break;
		default:
			dprintf("Console type unsupported. Exiting...");
			Sleep(5000);
			return;
	}

	dprintf("Base image path: %s\n",baseImagePath);

	dprintf("\n");


	dprintf("Press A to install RGH3 (dump and flash)\n");
	dprintf("Press any other button to exit.\n\n");

	for(;;)
	{
		m_pGamepad = ATG::Input::GetMergedInput();

		if (m_pGamepad->wPressedButtons & XINPUT_GAMEPAD_A)
		{
			char backupPath[512];
			char backupKeyPath[512];

			sprintf_s(backupPath, 512, "game:\\flashdmp_%s.bin", consoleSerial);
			sprintf_s(backupKeyPath, 512, "game:\\cpukey_%s.txt", consoleSerial);

			dprintf("Saving existing NAND to %s\n",backupPath);

			dumper(backupPath);
			SaveCPUKey(backupKeyPath,cpukey);

			dprintf("Making a copy of the base image...\n");

			CopyFile(baseImagePath,"game:\\updflash.bin",false);

			dprintf("Patching KV...\n");

			if(patch_raw_kv_pages(backupPath, "game:\\updflash.bin", cpukey, MMC == 1))
			{
				dprintf("Failed to patch KV! Press any button to exit.\n");
				
				for(;;)
				{
					if(m_pGamepad->wPressedButtons)
					{
						m_pGamepad = ATG::Input::GetMergedInput();
						return;
					}
				}

				return;
			}
			else
			{
				dprintf("Success\n");
			}
			
			dprintf("Flashing NAND...\n");
			
			flasher();
		}
		else if(m_pGamepad->wPressedButtons)
		{
			break;
		}
	}

	if (!MMC)
		sfcx_setconf(config);
}