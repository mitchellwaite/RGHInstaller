#include "OutputConsole.h"
#include "AtgConsole.h"
#include "AtgInput.h"
#include "AtgUtil.h"
#include <time.h>
#include "Corona4G.h"
#include "Automation.h"
#include "keyextract.h"

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
		unsigned int r = sfcx_init();
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
		unsigned int r = sfcx_init();
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


int patch_raw_kv_pages(char * flashdmp, unsigned char cpukey[0x10], char * updflash)
{
	int rc = 0;

	// This is all assuming a retail-ish NAND image with
	// a retail KV as input. Devkit KVs are a whole different thing
	int kvSize = 0x4000;
	int kvOffset = 0x4000;
	int pageCount = 20;

	int kvOffsetPhys = 0x4200;
	int kvSizePhys = 0x4200;

	// Sanity check when copying pages to the new image
	int kvOffsetFlash = 0;
	int kvSizeFlash = 0;

	unsigned char kvBuf[0x4200] = {'\0'};
	unsigned char kvBufNoEcc[0x4000] = {'\0'};

	unsigned char kvBufPatch[0x4200] = {'\0'};

	unsigned char kvRc4Key[0x10] = {'\0'};

	unsigned char cpukeyDest[0x10] = {'\0'};
	unsigned char kvRc4KeyDest[0x10] = {'\0'};

	unsigned char secret[0x2] = { 0x07, 0x12 };

	FILE *fd;

	if (fopen_s(&fd, flashdmp, "rb") != 0)
	{		
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FOR_READING, flashdmp);
		return -1;
	}

	// KV size is stored at 0x60 in NAND, it's the first page so
	// we don't need to account for SPARE data
	fseek(fd,0x60,SEEK_SET);

	if (fread_s(&kvSizeFlash, sizeof(kvSizeFlash), sizeof(kvSizeFlash), 1, fd) != 1)
	{
		dprintf("Couldn't read KV size from %s\n", flashdmp);
		fclose(fd);
		return -1;
	}

	fseek(fd,0x6C,SEEK_SET);

	if (fread_s(&kvOffsetFlash, sizeof(kvOffsetFlash), sizeof(kvOffsetFlash), 1, fd) != 1)
	{
		dprintf("Couldn't read KV offset from %s\n", flashdmp);
		fclose(fd);
		return -1;
	}

	if( kvOffsetFlash != kvOffset || kvSizeFlash != kvSize )
	{
		dprintf("Error, only consoles with retail type 1 or type 2 KVs are supported!\n");
		fclose(fd);
		return -1;
	}

	fseek(fd,kvOffsetPhys,SEEK_SET);

	if (fread_s(&kvBuf, sizeof(kvBuf),kvSizePhys,1,fd) != 1)
	{
		dprintf("Couldn't read KV from %s\n", flashdmp);
		fclose(fd);
		return -1;
	}

	// Presumably we've read the KV. Now we've got to patch the new image
	fclose(fd);

	// un-ecc the KV
	for(int i = 0; i<pageCount; i++)
	{
		memcpy(&kvBufNoEcc[0x200 * i],&kvBuf[0x210 * i],0x200); 
	}

	// Generate the RC4 key for decrypting the KV
	XeCryptHmacSha(cpukey, 0x10, kvBufNoEcc, 0x10, NULL, 0, NULL, 0, kvRc4Key, sizeof(kvRc4Key));

	dprintf("KV decryption key: ");
	for (int i = 0; i < 0x10; i++)
	{
		dprintf("%02X", kvRc4Key[i]);
	}
	dprintf("\n");

	// Decrypt the KV, leaving the nonce
	XeCryptRc4(kvRc4Key,sizeof(kvRc4Key),&kvBufNoEcc[0x10],sizeof(kvBufNoEcc) - 0x10);

	if (fopen_s(&fd, updflash, "rb+") != 0)
	{		
		dprintf(MSG_ERROR MSG_UNABLE_TO_OPEN_FOR_WRITING, updflash);
		return -1;
	}

	// 0xC6020 is the start of fuseline 4 when the fuses are stored at 0xC0000 logical
	// If we read 0x10 bytes we'll get the virtual CPU key
	fseek(fd,0xC6020,SEEK_SET);

	if (fread_s(cpukeyDest,0x10,0x10,1,fd) != 1)
	{
		dprintf("Couldn't read CPU key from %s\n", flashdmp);
		fclose(fd);
		return -1;
	}

	dprintf("New Virtual CPU key: ");
	for (int i = 0; i < 0x10; i++)
	{
		dprintf("%02X", cpukeyDest[i]);
	}
	dprintf("\n");

	// Read the KV from the new image
	fseek(fd,kvOffsetPhys,SEEK_SET);

	if (fread_s(&kvBufPatch, sizeof(kvBufPatch),kvSizePhys,1,fd) != 1)
	{
		dprintf("Couldn't read KV from %s\n", flashdmp);
		fclose(fd);
		return -1;
	}

	// TODO this doesn't actually work right... gotta copy what JRunner does
	// Generate the RC4 key used for re-encrypting the KV
	XeCryptHmacSha(cpukeyDest, 0x10, kvBufPatch, 0x10, NULL, 0, NULL, 0, kvRc4KeyDest, sizeof(kvRc4KeyDest));

	dprintf("KV encryption key: ");
	for (int i = 0; i < 0x10; i++)
	{
		dprintf("%02X", kvRc4KeyDest[i]);
	}
	dprintf("\n");

	// Re-encrypt the KV
	XeCryptRc4(kvRc4KeyDest,sizeof(kvRc4KeyDest),&kvBufNoEcc[0x10],sizeof(kvBufNoEcc) - 0x10);

	// Copy the nonce
	memcpy(kvBufNoEcc,kvBufPatch,0x10);

	for( int i = 0; i<pageCount; i++ )
	{
		// Copy only the page data
		memcpy(&kvBufPatch[0x210 * i], &kvBufNoEcc[0x200 * i], 0x200);

		// We need to recalculate the ECC
		memset(&kvBufPatch[(0x210 * i) + 0x20C],0x0, 4); //zero only EDC bytes  
		sfcx_calcecc((unsigned int *)&kvBufPatch[0x210 * i]); //recalc EDC bytes
	}

	fseek(fd,kvOffsetPhys,SEEK_SET);

	if( fwrite(kvBufPatch,kvSizePhys,1,fd) < 0 )
	{
		dprintf("Error: Failed to write KV to patch image %d\n",errno);
		fclose(fd);
		return -1;
	}

	fclose(fd);

	return rc;
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
		SaveCPUKey("game:\\cpukey.txt");
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
				Sleep(3000);
				return;
			}

			baseImagePath = "game:\\images\\falcon_glitch2m_rgh3.bin";
			break;
		default:
			dprintf("Console type unsupported. Exiting...");
			Sleep(3000);
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

			sprintf_s(backupPath, 512, "game:\\flashdmp_%s.bin", consoleSerial);

			dprintf("Saving existing NAND to %s\n",backupPath);

			dumper(backupPath);
			
			dprintf("Making a copy of the base image...\n");

			CopyFile(baseImagePath,"game:\\updflash.bin",false);

			dprintf("Patching KV...");

			if(patch_raw_kv_pages(backupPath, cpukey, "game:\\updflash.bin"))
			{
				dprintf("Failed to patch KV! Exiting...\n");
				Sleep(3000);
				return;
			}
			else
			{
				dprintf("Success\n");
			}
			
			dprintf("Flashing NAND..\n");
			
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