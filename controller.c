#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <Windows.h>
// DirectX 8
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

/*
typedef struct DIJOYSTATE {
    LONG    lX;              // x-axis position
    LONG    lY;              // y-axis position
    LONG    lZ;              // z-axis position
    LONG    lRx;             // x-axis rotation
    LONG    lRy;             // y-axis rotation
    LONG    lRz;             // z-axis rotation
    LONG    rglSlider[2];    // extra axes positions
    DWORD   rgdwPOV[4];      // POV directions
    BYTE    rgbButtons[32];  // 32 buttons
} DIJOYSTATE, *LPDIJOYSTATE;
*/

#ifndef CONTROLLER_CUSTOM_BOOL
enum bool_e {
	false = 0,
	true
};
typedef uint8_t bool;
#endif

typedef struct EnumDevicesContext {
	LPDIRECTINPUT8 pDirectInput;
	LPDIRECTINPUTDEVICE8 *ppJoystickDevice;
} EnumDevicesContext;

BOOL CALLBACK EnumDevicesCallback( const DIDEVICEINSTANCE *pInstance, EnumDevicesContext *pContext ) {
	HRESULT hResult = IDirectInput8_CreateDevice( pContext->pDirectInput, &pInstance->guidInstance, pContext->ppJoystickDevice, NULL );
	if ( FAILED( hResult ) )  return DIENUM_CONTINUE;
	return DIENUM_STOP;
}

// ESC [ s  -- Save Cursor
#define CONSOLE_SC "\x1B[s"
#define CONSOLE_SC_LEN 3
// ESC [ u  -- Restore cursor
#define CONSOLE_RC "\x1B[u"
#define CONSOLE_RC_LEN 3

int print_controller_generic( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
	int cursor = 0;

	// Position + Rotation
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		" lX: [%5ld]  lY: [%5ld]  lZ: [%5ld]\n"
		"lRx: [%5ld] lRy: [%5ld] lRz: [%5ld]\n",
		j->lX, j->lY, j->lZ,
		j->lRx, j->lRy, j->lRz );

	// Sliders
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		"rglSlider:\n"
		"  [0]: [%5ld]\n"
		"  [1]: [%5ld]\n",
		j->rglSlider[ 0 ], j->rglSlider[ 1 ] );

	// POVs
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		"rgdwPOV:\n"
		"  [0]: [%10lu]\n"
		"  [1]: [%10lu]\n"
		"  [2]: [%10lu]\n"
		"  [3]: [%10lu]\n",
		j->rgdwPOV[ 0 ], j->rgdwPOV[ 1 ], j->rgdwPOV[ 2 ], j->rgdwPOV[ 3 ]
	 );

	// Buttons
	cursor += snprintf( buffer + cursor, buffer_size - cursor, "rgbButtons:\n" );
	BYTE *pbValue = ( BYTE * )&j->rgbButtons;
	for ( int i = 0; i < 32 / 4; i += 1 ) {
		cursor += snprintf( buffer + cursor, buffer_size - cursor,
			"\t[%d]: [%3hhu]\n", i, *( pbValue + i ) );
	}

	return cursor;
}

int print_controller_ps4( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
	// Don't judge...
	BYTE bRect = j->rgbButtons[ 0 ];
	BYTE bCross = j->rgbButtons[ 1 ];
	BYTE bCircle = j->rgbButtons[ 2 ];
	BYTE bTri = j->rgbButtons[ 3 ];
	const char *pszShapes[ 4 ] = {
		( bRect )   ? u8"□" : "-",
		( bCross )  ? u8"x" : "-",
		( bCircle ) ? u8"o" : "-",
		( bTri )    ? u8"△" : "-"
	};

	const char *pszArrows[ 4 ] = {
		"-",  // ArrowUp
		"-",  // ArrowRight
		"-",  // ArrowDown
		"-"   // ArrowLeft
	};
	DWORD dwArrows = j->rgdwPOV[ 0 ];
	DWORD dwDegrees = 0;
	if ( dwArrows != 0xFFFFFFFF ) {
		dwDegrees = dwArrows / 100;
		DWORD dwDirection = ( dwArrows + 2250 ) / 4500;  // Round to nearest 45 degrees
		switch ( dwDirection % 8 ) {
			case 1: pszArrows[ 1 ] = u8"→";  // Fall-through
			case 0: pszArrows[ 0 ] = u8"↑"; break;

			case 3: pszArrows[ 2 ] = u8"↓";  // Fall-through
			case 2: pszArrows[ 1 ] = u8"→"; break;

			case 5: pszArrows[ 3 ] = u8"←";  // Fall-through
			case 4: pszArrows[ 2 ] = u8"↓"; break;

			case 7: pszArrows[ 0 ] = u8"↑";  // Fall-through
			case 6: pszArrows[ 3 ] = u8"←"; break;
		}
	}
	LONG lL2R2[ 2 ] = {
		// L2:
		j->lRx,
		// R2:
		j->lRy
	};
	float fL2R2[ 2 ] = {
		// From: 0..65535
		//   To: 0f..1f
		// L2:
		lL2R2[ 0 ] * ( 1.0f / 65535.0f ),
		// R2:
		lL2R2[ 1 ] * ( 1.0f / 65535.0f )
	};
	LONG lSticks[ 4 ] = {
		// Left:
		j->lX, j->lY,
		// Right:
		j->lZ, j->lRz
	};
	float fSticks[ 4 ] = {
		// From: 0..32767..65535
		//   To: -1f..0f..1f
		// Left:
		( lSticks[ 0 ] == 32767 ) ? lSticks[ 0 ] * ( 1.0f / 32767.0f ) - 1.0f : lSticks[ 0 ] * ( 1.0f / 32767.5f ) - 1.0f,
		( lSticks[ 1 ] == 32767 ) ? lSticks[ 1 ] * ( 1.0f / 32767.0f ) - 1.0f : lSticks[ 1 ] * ( 1.0f / 32767.5f ) - 1.0f,
		// Right:
		( lSticks[ 2 ] == 32767 ) ? lSticks[ 2 ] * ( 1.0f / 32767.0f ) - 1.0f : lSticks[ 2 ] * ( 1.0f / 32767.5f ) - 1.0f,
		( lSticks[ 3 ] == 32767 ) ? lSticks[ 3 ] * ( 1.0f / 32767.0f ) - 1.0f : lSticks[ 3 ] * ( 1.0f / 32767.5f ) - 1.0f
	};
	if ( fSticks[ 1 ] != 0 )  fSticks[ 1 ] *= -1.0f;  // Invert L stick Y axis without turning 0.0 into -0.0
	if ( fSticks[ 3 ] != 0 )  fSticks[ 3 ] *= -1.0f;  // Invert R stick Y axis without turning 0.0 into -0.0
	BYTE bL1 = j->rgbButtons[ 4 ];
	BYTE bR1 = j->rgbButtons[ 5 ];
	BYTE bL2 = j->rgbButtons[ 6 ];
	BYTE bR2 = j->rgbButtons[ 7 ];
	BYTE bSHARE = j->rgbButtons[ 8 ];
	BYTE bOPTIONS = j->rgbButtons[ 9 ];
	BYTE bLstick = j->rgbButtons[ 10 ];
	BYTE bRstick = j->rgbButtons[ 11 ];
	BYTE bPS = j->rgbButtons[ 12 ];
	BYTE bTOUCH = j->rgbButtons[ 13 ];
	const char *pszSpecials[ 4 ] = {
		( bSHARE )   ? "SHARE"   : "-",
		( bOPTIONS ) ? "OPTIONS" : "-",
		( bPS )      ? "PS"      : "-",
		( bTOUCH )   ? "TOUCH"   : "-"
	};
	
	int cursor = 0;
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		"Arrows: [%s, %s, %s, %s] (%3ld)\n"
		"Shapes: [%s, %s, %s, %s]\n"
		"Special: [%5s, %7s, %2s, %5s]\n"
		"L1: [%3hhu] R1: [%3hhu]\n"
		"L2: [%3hhu, %5ld] (%9.6f)\n"
		"R2: [%3hhu, %5ld] (%9.6f)\n"
		"L Stick: [%3hhu, %5ld,%5ld] (%9.6f,%9.6f)\n"
		"R Stick: [%3hhu, %5ld,%5ld] (%9.6f,%9.6f)\n",
		pszArrows[ 0 ], pszArrows[ 1 ], pszArrows[ 2 ], pszArrows[ 3 ], dwDegrees,
		pszShapes[ 0 ], pszShapes[ 1 ], pszShapes[ 2 ], pszShapes[ 3 ],
		pszSpecials[ 0 ], pszSpecials[ 0 ], pszSpecials[ 0 ], pszSpecials[ 0 ],
		bL1, bR1,
		bL2, lL2R2[ 0 ], fL2R2[ 0 ],
		bR2, lL2R2[ 1 ], fL2R2[ 1 ],
		bLstick, lSticks[ 0 ], lSticks[ 1 ], fSticks[ 0 ], fSticks[ 1 ],
		bRstick, lSticks[ 2 ], lSticks[ 3 ], fSticks[ 2 ], fSticks[ 3 ] );

	return cursor;
}

int main( int arguments_count, char *arguments[] ) {
	HINSTANCE hInstance = GetModuleHandleA( NULL ); // Current program's handle
	if ( hInstance == NULL ) {
		fprintf( stderr, "Failed to get current's program module handle. (hInstance=0x%X)\n", hInstance );
		return -1;
	}
	printf( "Got current program's module handle. (hInstance=0x%X)\n", *hInstance );

	// Enable control escape codes to save/reset cursor position.
	DWORD dwMode;
	GetConsoleMode( hInstance, &dwMode );
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode( hInstance, dwMode );

	/* Initialize DirectInput */

	HRESULT hDIResult;
	LPDIRECTINPUT8 pDirectInput = NULL;
	hDIResult = DirectInput8Create( hInstance, DIRECTINPUT_VERSION, &IID_IDirectInput8, &pDirectInput, NULL );
	if ( hDIResult != DI_OK ) {
		fprintf( stderr, "Failed to initialize DirectInput (version=0x%X). (0x%X)\n", DIRECTINPUT_VERSION, hDIResult );
		return -2;
	}
	printf( "Initialized DirectInput (version=0x%X).\n", DIRECTINPUT_VERSION );

	/* Enumerate attached gamepad devices */

	LPDIRECTINPUTDEVICE8 pJoystickDevice = NULL;
	EnumDevicesContext ctx = { .pDirectInput = pDirectInput, .ppJoystickDevice = &pJoystickDevice };
enumDevices:
	hDIResult = IDirectInput8_EnumDevices( pDirectInput, DI8DEVCLASS_GAMECTRL, EnumDevicesCallback, &ctx, DIEDFL_ATTACHEDONLY );
	if ( hDIResult != DI_OK ) {
		fprintf( stderr, "Failed to enumerate devices. (0x%X)\n", hDIResult );
		return -3;
	} else if ( pJoystickDevice == NULL ) {
		printf( "No attached Joysticks found. Connect one and try again.\n");
		system("pause");
		goto enumDevices;
	}
	printf( "Found attached Joystick device.\n" );

	/* Set Joystick data format */

	hDIResult = IDirectInputDevice8_SetDataFormat( pJoystickDevice, &c_dfDIJoystick );
	if ( hDIResult != DI_OK ) {
		fprintf( stderr, "Failed to set Joystick device data format to '%s' (0x%X).\n", "c_dfDIJoystick", hDIResult );
		return -4;
	}
	printf( "Set Joystick device data format to '%s'.\n", "c_dfDIJoystick" );

	HWND hWindow = GetConsoleWindow();
	if ( hWindow == NULL ) {
		fprintf( stderr, "Failed to get console window. (hWindow=0x%X)\n", hWindow );
		return -5;
	}
	printf( "Got console window. (hWindow=0x%X)\n", hWindow );
	
	/* Set cooperative level */

	// Use of `DISCL_EXCLUSIVE | DISCL_FOREGROUND` resulted in Permission denial.
	// It works with `DISCL_NONEXCLUSIVE | DISCL_BACKGROUND` just fine.
	hDIResult = IDirectInputDevice8_SetCooperativeLevel( pJoystickDevice, hWindow, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND );
	if ( hDIResult != DI_OK ) {
		fprintf( stderr, "Failed to set Joystick device cooperative level to %s. (0x%X)\n", "DISCL_NONEXCLUSIVE | DISCL_BACKGROUND", hDIResult );
		return -6;
	}
	printf( "Set Joystick device cooperative level to %s.\n", "DISCL_NONEXCLUSIVE | DISCL_BACKGROUND" );

	/* Acquire device */

	hDIResult = IDirectInputDevice8_Acquire( pJoystickDevice );
	if ( hDIResult != DI_OK ) {
		fprintf( stderr, "Failed to acquire Joystick device. (0x%X)", hDIResult );
		return -7;
	}
	printf( "Acquired Joystick device.\n" );

	int pollRate = 60;  // 60Hz = 60 times per second
	float pollTimeIntervalMs = 1000.0f / pollRate;
	DWORD dwPollTimeIntervalMs = ( DWORD )pollTimeIntervalMs;  // at 60Hz, 1000 / 60 = 16.666f = 16

	HANDLE hConsoleOutput = GetStdHandle( STD_OUTPUT_HANDLE );

	// Save cursor position to then overwrite previous output.
	WriteConsoleA( hConsoleOutput, CONSOLE_SC, CONSOLE_SC_LEN, NULL, NULL );

	// Allocate string buffer on heap.
#define BUFFER_SIZE 4096
	char *buffer = malloc( BUFFER_SIZE * sizeof( char ) );
	if ( buffer == NULL ) {
		fprintf( stderr, "Failed to allocate %d bytes of memory for string buffer.", BUFFER_SIZE );
		return -8;
	}
	
	DIJOYSTATE js;
	HRESULT hResult;
	// Infinite loop.
	// To terminate process, press `CTRL+C` on focused command line window,
	//   or close it with the window close button [X].
	while ( 1 ) {
		// Sleeping just poll time interval doesn't get us to exact poll rate
		//   because of Sleep() imprecision, context switches, and more importantly
		//   the unaccounted time of formatting and printing the recorded data output.
		// But it is fine, this is just a toy terminal app!
		Sleep( dwPollTimeIntervalMs );

		// Restore cursor position before overwriting output.
		WriteConsoleA( hConsoleOutput, CONSOLE_RC, CONSOLE_RC_LEN, NULL, NULL );

		/* Read Joystick state */

		hResult = IDirectInputDevice8_GetDeviceState( pJoystickDevice, sizeof( DIJOYSTATE ), &js );
		if ( hResult != DI_OK ) {
			fprintf( stderr, "Failed to get Joystick state. (0x%X)\n", hResult );
			continue;
		}

		// Overwrite output with new data.
		int written = print_controller_ps4( buffer, BUFFER_SIZE, &js );
		WriteConsoleA( hConsoleOutput, buffer, written, NULL, NULL );
	}

	// The program actually never gets to this point.
	// We are just being good guys.
	IDirectInputDevice8_Release( pJoystickDevice );
	free( buffer );

	return 0;
}
