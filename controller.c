#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <Windows.h>
// DirectInput8, part of DirectX 8.
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

#define CONTRL_ALLOC( count, type )                          malloc( count * sizeof( type ) )
#define CONTRL_REALLOC( pointer, old_size, new_size, type )  realloc( pointer, new_size * sizeof( type ) )
#define CONTRL_FREE( pointer )                               free( pointer )


#ifndef CONTRL_DEBUG
	#define CONTRL_DEBUG 0
#endif

#if CONTRL_DEBUG
	#define CONTRL_TRACE( format, ... )                        contrl__fprintf( stdout, "TRACE: ", 7, format, __VA_ARGS__ ); putc( '\n', stdout )
	#define CONTRL_TRACE2( prefix, prefix_size, format, ... )  contrl__fprintf( stdout, prefix, prefix_size, format, __VA_ARGS__ )
#else
	#define CONTRL_TRACE( format, ... )
	#define CONTRL_TRACE2( prefix, prefix_size, format, ... )
#endif /* CONTRL_DEBUG */

#define CONTRL_ERROR( exit_code, format, ... )  contrl__fprintf( stderr, "ERROR: ", 7, format, __VA_ARGS__ ); exit( exit_code )
#define CONTRL_WARN( format, ... )              contrl__fprintf( stdout, "WARNING: ", 9, format, __VA_ARGS__ )
#define CONTRL_PRINT( format, ... )             contrl__fprintf( stdout, NULL, 0, format, __VA_ARGS__ )
#define CONTRL_WPRINT( format, ... )            contrl__fwprintf( stdout, NULL, 0, format, __VA_ARGS__ )


// ESC [ s  -- Save Cursor
#define CONSOLE_SC "\x1B[s"
#define CONSOLE_SC_LEN 3
// ESC [ u  -- Restore cursor
#define CONSOLE_RC "\x1B[u"
#define CONSOLE_RC_LEN 3

// The controller state output buffer
#define STATE_BUFFER_SIZE 4096

// GUID's Vendor IDs
enum EVID {
	VID_SONY     = 0x054C,
	VID_LOGITECH = 0x046D,
	VID_MOZA     = 0x346E,
	VID_UNKNOWN1 = 0x1A86,

	VID_MAX      = 0xFFFF
};

// GUID's Product IDs
enum EPID {
	PID_SONY_DUALSHOCK4    = 0x09CC,
	PID_LOGITECH_G923      = 0xC266,
	PID_MOZA_R9_BASE       = 0x0002,
	PID_UNKNOWN1_HANDBRAKE = 0x6D3A,

	PID_MAX                = 0xFFFF
};

#define GUID_PRODUCT_GET_PID( guidData1 )  ( guidData1 >> 16 )
#define GUID_PRODUCT_GET_VID( guidData1 )  ( guidData1 & 0x0000FFFF )

#ifndef CONTRL_CUSTOM_BOOL
enum bool_e {
	false = 0,
	true
};
typedef uint8_t bool;
// Without double-negation (!!), the value can overflow.
// For example: `flags (256) & flag (256) == 256`, which is logically `true`,
// but it then gets casted to `uint8_t` which result in 0, a logical `false`.
#define C_BOOL( expr )  ( !!( expr ) )
#endif

typedef int ( * PFN_PrintDeviceState )( char *buffer, size_t buffer_size, DIJOYSTATE *j );

typedef struct DeviceInfo {
	uint16_t VID;  // Vendor ID
	uint16_t PID;  // Product ID
	uint32_t dwDevType;  // Device type
	const char *pszVendorName;
	const char *pszProductName;
	PFN_PrintDeviceState print_device_state_fn;
} DeviceInfo;

static void contrl__fprintf( FILE *stream, const char *prefix, size_t prefix_size, const char *format, ... ) {
	fwrite( prefix, sizeof( char ), prefix_size, stream );
	va_list args;
	va_start( args, format );
	vfprintf( stream, format, args );
	va_end( args );
}

static void contrl__fwprintf( FILE *stream, const wchar_t *prefix, size_t prefix_size, const wchar_t *format, ... ) {
	fwrite( prefix, sizeof( char ), prefix_size, stream );
	va_list args;
	va_start( args, format );
	vfwprintf( stream, format, args );
	va_end( args );
}


typedef struct DeviceEffectsSupportedContext {
	int nEffects;  // Out parameter.  Set to 0 before call!  Number of effects supported by the force-feedback system.
} DeviceEffectsSupportedContext;

static BOOL CALLBACK contrl__device_effects_supported_callback( const DIEFFECTINFO *pDIEffectInfo, DeviceEffectsSupportedContext *pContext ) {
	pContext->nEffects += 1;
	return DIENUM_CONTINUE;
}

typedef struct DeviceCountContext {
	int nDevices;  // Out parameter.  Set to 0 before call!  Number of effects supported by the force-feedback system.
} DeviceCountContext;

static BOOL CALLBACK contrl__device_count_callback( const DIEFFECTINFO *pDIEffectInfo, DeviceCountContext *pContext ) {
	pContext->nDevices += 1;
	return DIENUM_CONTINUE;
}

typedef struct DeviceGetFirstContext {
	LPDIRECTINPUT8        pDirectInput;        // In parameter.  Pointer to the instance of DirectInput8.
	LPDIRECTINPUTDEVICE8 *ppControllerDevice;  // In-Out parameter.  Pointer to a pointer to where created device pointer (LPDIRECTINPUTDEVICE8) will be stored.
	DIDEVICEINSTANCE     *pDeviceInstance;     // In-Out parameter, can be NULL.  Pointer to where DIDEVICEINSTANCE will be stored.
} DeviceGetFirstContext;

static BOOL CALLBACK contrl__device_get_first_callback( const DIDEVICEINSTANCE *pInstance, DeviceGetFirstContext *pContext ) {
	HRESULT hResult = IDirectInput8_CreateDevice(
		/*             pDevice */ pContext->pDirectInput,
		/*       pInstanceGUID */ &pInstance->guidInstance,
		/* ppDirectInputDevice */ pContext->ppControllerDevice,
		/*             pUnused */ NULL );
	if ( hResult != DI_OK )  return DIENUM_CONTINUE;
	if ( pContext->pDeviceInstance != NULL )  *pContext->pDeviceInstance = *pInstance;
	return DIENUM_STOP;
}

typedef struct DeviceGetAllContext {
	LPDIRECTINPUT8     pDirectInput;      // In parameter.  Pointer to the instance of DirectInput8.
	DIDEVICEINSTANCE  *pDeviceInstances;  // In-Out parameter, can be NULL.  Pointer to an array of `DIDEVICEINSTANCE`s where devices will be stored.
	int                nDevicesMax;       // In parameter.  Size of the storing `ppDeviceInstances` array.
	int                nDevicesGot;       // Out parameter.  Set to 0 before call!  Number of `DIDEVICEINSTANCE`s copied to `ppDeviceInstances`.
} DeviceGetAllContext;

static BOOL CALLBACK contrl__device_get_all_callback( const DIDEVICEINSTANCE *pInstance, DeviceGetAllContext *pContext ) {
	if ( pContext->pDeviceInstances == NULL )  return DIENUM_STOP;
	if ( pContext->nDevicesGot >= pContext->nDevicesMax )  return DIENUM_STOP;

	pContext->pDeviceInstances[ pContext->nDevicesGot ] = *pInstance;  // Copy by value
	pContext->nDevicesGot += 1;
	return DIENUM_CONTINUE;
}

static void contrl__debug_print_device_info( const DIDEVICEINSTANCE *i ) {
	const GUID *const gI = &i->guidInstance;
	const GUID *const gP = &i->guidProduct;
	const GUID *const gD = &i->guidFFDriver;

	char data4Bytes[ 64 ] = { 0 };  // Only 51 bytes are used, but aligned to 64.
	char *const gIData4 = data4Bytes;
	char *const gPData4 = gIData4 + 17;  // 16 chars + 1 null-terminator
	char *const gDData4 = gPData4 + 17;  // 16 chars + 1 null-terminator
	for ( int i = 0; i < 8; i += 1 ) {
		// Write 1 byte of each GUID simultaneously.
		sprintf( &gIData4[ 2 * i ], "%02X", gI->Data4[ i ] );
		sprintf( &gPData4[ 2 * i ], "%02X", gP->Data4[ i ] );
		sprintf( &gDData4[ 2 * i ], "%02X", gD->Data4[ i ] );
	}

	CONTRL_TRACE2( "TRACE: ", 7,
		"Device info:\n"
		"  dwSize: %lu\n"
		"  guidInstance: %08X-%04hX-%04hX-%s (\"%.*s\")\n"
		"  guidProduct: %08X-%04hX-%04hX-%s (\"%.*s\")\n"
		"  dwDevType: 0x%08X\n",
		i->dwSize,
		gI->Data1, gI->Data2, gI->Data3, gIData4, 6, &gI->Data4[ 2 ],
		gP->Data1, gP->Data2, gP->Data3, gPData4, 6, &gP->Data4[ 2 ],
		i->dwDevType );

#if UNICODE
	CONTRL_WPRINT( 
		L"  tszInstanceName: \"%s\"\n"
		L"  tszProductName: \"%s\"\n",
		i->tszInstanceName, i->tszProductName );
#else
	CONTRL_PRINT( 
		"  tszInstanceName: \"%s\"\n"
		"  tszProductName: \"%s\"\n",
		i->tszInstanceName, i->tszProductName );
#endif

	CONTRL_PRINT(
		"  guidFFDriver: %08X-%04hX-%04hX-%s\n"
		"  wUsagePage: 0x%08X\n"
		"  wUsage: 0x%08X\n",
		gD->Data1, gD->Data2, gD->Data3, gDData4,
		i->wUsagePage,
		i->wUsage );

	CONTRL_TRACE( "Vendor ID (VID): 0x%04X, Product ID (PID): 0x%04X",
		GUID_PRODUCT_GET_VID( gP->Data1 ), GUID_PRODUCT_GET_PID( gP->Data1 ) );
}

static void contrl__debug_print_device_capabilities( const DIDEVCAPS *c ) {
	CONTRL_TRACE( "Device capabilities:\n"
		"  dwSize: %u\n"
		"  dwFlags: 0x%08X\n"
		"  dwDevType: 0x%08X (type=0x%02X, subtype=0x%02X)\n"
		"  dwAxes: %u\n"
		"  dwButtons: %u\n"
		"  dwPOVs: %u\n"
		"  dwFFSamplePeriod: %u\n"
		"  dwFFMinTimeResolution: %u\n"
		"  dwFirmwareRevision: 0x%08X\n"
		"  dwHardwareRevision: 0x%08X\n"
		"  dwFFDriverVersion: 0x%08X",
		c->dwSize,
		c->dwFlags,
		c->dwDevType, GET_DIDEVICE_TYPE( c->dwDevType ), GET_DIDEVICE_SUBTYPE( c->dwDevType ),
		c->dwAxes,
		c->dwButtons,
		c->dwPOVs,
		c->dwFFSamplePeriod,
		c->dwFFMinTimeResolution,
		c->dwFirmwareRevision,
		c->dwHardwareRevision,
		c->dwFFDriverVersion );
}

int contrl_print_device_state_generic( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
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
		// Print 4 columns row-by-row.
		// Each column continues previous one.
		cursor += snprintf( buffer + cursor, buffer_size - cursor,
			"  [%2d]: [%3hhu]  [%2d]: [%3hhu]  [%2d]: [%3hhu]  [%2d]: [%3hhu]\n",
			i +  0, *( pbValue + i +  0 ),
			i +  8, *( pbValue + i +  8 ),
			i + 16, *( pbValue + i + 16 ),
			i + 24, *( pbValue + i + 24 ) );
	}

	return cursor;
}

static void contrl_test_haptics( const LPDIRECTINPUTDEVICE8 pControllerDevice ) {
	/* Enumerate device effects */

	DeviceEffectsSupportedContext ctxEffectsSupported = {
		.nEffects = 0  // Out counter
	};
	HRESULT hDIResult = IDirectInputDevice8_EnumEffects(
		/*       this */ pControllerDevice,
		/* lpCallback */ contrl__device_effects_supported_callback,
		/*      pvRef */ &ctxEffectsSupported,
		/*  dwEffType */ DIEFT_ALL );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -12, "Failed to enumerate controller device effects. (0x%X)\n", hDIResult );
	}

	DIEFFECT effect = { 0 };
	effect.dwSize = sizeof( DIEFFECT );
	effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTIDS;
	effect.dwDuration = 5 * 1000;  // In Us - microseconds
	effect.dwSamplePeriod = 0;  // Default
	// effect.dwGain = DI_FFNOMINALMAX;
	// effect.lpvTypeSpecificParams = DIEFT_CONSTANTFORCE;
	

	// In range of: [-10_000, 10_000]
	DICONSTANTFORCE cForce = { .lMagnitude = 100000 };

	effect.cbTypeSpecificParams = sizeof( DICONSTANTFORCE );
	effect.lpvTypeSpecificParams = &cForce;

	/* Create Force-feedback effect */

	LPDIRECTINPUTEFFECT pDIEffect = NULL;
	hDIResult = IDirectInputDevice8_CreateEffect(
		/*      this */ pControllerDevice,
		/*      guid */ &GUID_ConstantForce,
		/*     lpeff */ &effect,  // Passed effect setup parameters
		/*    ppdeff */ &pDIEffect,  // Returned DirectInput effect object
		/* pUnkOuter */ NULL );
	if ( hDIResult != DI_OK ) {
		const char *szError;
		switch ( hDIResult ) {
			case DIERR_DEVICEFULL:     szError = "DIERR_DEVICEFULL"; break;
			case DIERR_DEVICENOTREG:   szError = "DIERR_DEVICENOTREG"; break;
			case DIERR_INVALIDPARAM:   szError = "DIERR_INVALIDPARAM"; break;
			case DIERR_NOTINITIALIZED: szError = "DIERR_NOTINITIALIZED"; break;
			case E_NOTIMPL:            szError = "E_NOTIMPL"; break;
			default: szError = "(Unknown)";
		}
		CONTRL_ERROR( -11, "Failed to create force-feedback effect. (0x%X=%s)\n", hDIResult, szError );
	}

	/* Download effect - place the effect on the device */

	// What a dumb name... Upload, Place, Set - any would have made more sense.
	hDIResult = IDirectInputEffect_Download( pDIEffect );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -12, "Failed to upload effect to controller device. (0x%X)\n", hDIResult );
	}

	/* Start the effect */

	DWORD dwIterations = 1;
	// DIES_SOLO - Stop all other effects
	// DIES_NODOWNLOAD - Do not download automatically
	DWORD dwFlags = 0;
	hDIResult = IDirectInputEffect_Start( pDIEffect, dwIterations, dwFlags );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -13, "Failed to start effect on controller device. (0x%x)\n", hDIResult );
	}
}

int contrl_print_device_state_sony_dualshock4( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
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
		"Arrows: [%s, %s, %s, %s] (%3lu)\n"
		"Shapes: [%s, %s, %s, %s]\n"
		"Special: [%5s, %7s, %2s, %5s]\n"
		"L1: [%3hhu] R1: [%3hhu]\n"
		"L2: [%3hhu, %5ld] (%9.6f)\n"
		"R2: [%3hhu, %5ld] (%9.6f)\n"
		"L Stick: [%3hhu, %5ld,%5ld] (%9.6f,%9.6f)\n"
		"R Stick: [%3hhu, %5ld,%5ld] (%9.6f,%9.6f)\n",
		pszArrows[ 0 ], pszArrows[ 1 ], pszArrows[ 2 ], pszArrows[ 3 ], dwDegrees,
		pszShapes[ 0 ], pszShapes[ 1 ], pszShapes[ 2 ], pszShapes[ 3 ],
		pszSpecials[ 0 ], pszSpecials[ 1 ], pszSpecials[ 2 ], pszSpecials[ 3 ],
		bL1, bR1,
		bL2, lL2R2[ 0 ], fL2R2[ 0 ],
		bR2, lL2R2[ 1 ], fL2R2[ 1 ],
		bLstick, lSticks[ 0 ], lSticks[ 1 ], fSticks[ 0 ], fSticks[ 1 ],
		bRstick, lSticks[ 2 ], lSticks[ 3 ], fSticks[ 2 ], fSticks[ 3 ] );

	return cursor;
}

int contrl_print_device_state_logitech_g923( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
	BYTE bCross = j->rgbButtons[ 0 ];
	BYTE bRect = j->rgbButtons[ 1 ];
	BYTE bCircle = j->rgbButtons[ 2 ];
	BYTE bTri = j->rgbButtons[ 3 ];
	const char *pszShapes[ 4 ] = {
		( bCross )  ? u8"x" : "-",
		( bRect )   ? u8"□" : "-",
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

	LONG lAxes[ 4 ] = {
		j->rglSlider[ 0 ],  // Clutch
		j->lRz,  // Brake
		j->lY,  // Throttle
		j->lX  // Wheel
	};
	float fAxes[ 4 ] = {
		// Pedals:
		// From: 65535..0
		//   To: 0f..1f
		// Clutch:
		1.0f - ( lAxes[ 0 ] * ( 1.0f / 65535.0f ) ),
		// Brake:
		1.0f - ( lAxes[ 1 ] * ( 1.0f / 65535.0f ) ),
		// Throttle:
		1.0f - ( lAxes[ 2 ] * ( 1.0f / 65535.0f ) ),
		// Wheel:
		// From:   0..32767..65535
		//   To: -1f..0f..1f
		( lAxes[ 3 ] == 32767 ) ? lAxes[ 3 ] * ( 1.0f / 32767.0f ) - 1.0f : lAxes[ 3 ] * ( 1.0f / 32767.5f ) - 1.0f
	};

	BYTE bPaddleR = j->rgbButtons[ 4 ];
	BYTE bPaddleL = j->rgbButtons[ 5 ];
	BYTE bR2 = j->rgbButtons[ 6 ];
	BYTE bL2 = j->rgbButtons[ 7 ];
	BYTE bSHARE = j->rgbButtons[ 8 ];
	BYTE bOPTIONS = j->rgbButtons[ 9 ];
	BYTE bR3 = j->rgbButtons[ 10 ];
	BYTE bL3 = j->rgbButtons[ 11 ];
	BYTE bPlus = j->rgbButtons[ 19 ];
	BYTE bMinus = j->rgbButtons[ 20 ];
	BYTE bDialR = j->rgbButtons[ 21 ];
	BYTE bDialL = j->rgbButtons[ 22 ];
	BYTE bENTER = j->rgbButtons[ 23 ];
	BYTE bPS = j->rgbButtons[ 24 ];

	const char *pszSpecials[ 4 ] = {
		( bSHARE )   ? "SHARE"   : "-",
		( bOPTIONS ) ? "OPTIONS" : "-",
		( bENTER )   ? "ENTER"   : "-",
		( bPS )      ? "PS"      : "-"
	};

	int cursor = 0;
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		"Pedals:\n"
		"  Clutch: [%5d] (%9.6f)\n"
		"   Brake: [%5d] (%9.6f)\n"
		"Throttle: [%5d] (%9.6f)\n"
		"   Wheel: [%5d] (%9.6f)\n"
		"PaddleL: [%3hhu] PaddleR: [%3hhu]\n"
		"Arrows: [%s, %s, %s, %s] (%3u)\n"
		"Shapes: [%s, %s, %s, %s]\n"
		"Special: [%5s, %7s, %5s, %2s]\n"
		"L2: [%3hhu] R2: [%3hhu]\n"
		"L3: [%3hhu] R3: [%3hhu]\n"
		"Plus/Minus: [%3hhu, %3hhu]\n"
		"DialL: [%3hhu] DialR: [%3hhu]\n",
		lAxes[ 0 ], fAxes[ 0 ],
		lAxes[ 1 ], fAxes[ 1 ],
		lAxes[ 2 ], fAxes[ 2 ],
		lAxes[ 3 ], fAxes[ 3 ],
		bPaddleL, bPaddleR,
		pszArrows[ 0 ], pszArrows[ 1 ], pszArrows[ 2 ], pszArrows[ 3 ], dwDegrees,
		pszShapes[ 0 ], pszShapes[ 1 ], pszShapes[ 2 ], pszShapes[ 3 ],
		pszSpecials[ 0 ], pszSpecials[ 1 ], pszSpecials[ 2 ], pszSpecials[ 3 ],
		bL2, bR2,
		bL3, bR3,
		bPlus, bMinus,
		bDialL, bDialR );

	return cursor;
}

int contrl_print_device_state_unknown1_handbrake( char *buffer, size_t buffer_size, DIJOYSTATE *j ) {
	LONG lAxis = j->rglSlider[ 0 ];
	// Handbrake:
	// From: 0..65535
	//   To: 0f..1f
	float fAxis = lAxis * ( 1.0f / 65535.0f );
	BYTE bHandbrake = j->rgbButtons[ 0 ];
	int cursor = 0;
	cursor += snprintf( buffer + cursor, buffer_size - cursor,
		"Handbrake: [%3hhu, %5d] (%9.6f)\n",
		bHandbrake, lAxis, fAxis );

	return cursor;
}

bool contrl_device_get_info( const DIDEVICEINSTANCE *pDeviceInstance, DeviceInfo *pDeviceInfo );
bool contrl_device_get_info( const DIDEVICEINSTANCE *i, DeviceInfo *di ) {
	if ( i == NULL || di == NULL )  return false;

	di->VID = GUID_PRODUCT_GET_VID( i->guidProduct.Data1 );  // Vendor ID
	di->PID = GUID_PRODUCT_GET_PID( i->guidProduct.Data1 );  // Product ID
	di->pszVendorName = "?";
	di->pszProductName = "?";
	di->print_device_state_fn = contrl_print_device_state_generic;
	if ( di->VID == VID_SONY && di->PID == PID_SONY_DUALSHOCK4 ) {
		di->pszVendorName = "Sony";
		di->pszProductName = "DualShock 4";
		di->print_device_state_fn = contrl_print_device_state_sony_dualshock4;
	} else if ( di->VID == VID_LOGITECH && di->PID == PID_LOGITECH_G923 ) {
		di->pszVendorName = "Logitech";
		di->pszProductName = "G923 Racing Wheel";
		di->print_device_state_fn = contrl_print_device_state_logitech_g923;
	} else if ( di->VID == VID_MOZA && di->PID == PID_MOZA_R9_BASE ) {
		di->pszVendorName = "MOZA";
		di->pszProductName = "R9 Base";
		// Wheel base button bindings depend on the steering wheel attached.
		// Also, some wheels use buttons outside of 0-31 range of `DIJOYSTATE`,
		// so `DIJOYSTATE2` would be needed then.
		// di->print_device_state_fn
	} else if ( di->VID == VID_UNKNOWN1 && di->PID == PID_UNKNOWN1_HANDBRAKE ) {
		di->pszProductName = "Handbrake";
		di->print_device_state_fn = contrl_print_device_state_unknown1_handbrake;
	}

	return true;
}

int main( int arguments_count, char *arguments[] ) {
	HINSTANCE hInstance = GetModuleHandleA( NULL ); // Current program's handle
	if ( hInstance == NULL ) {
		CONTRL_ERROR( -1, "Failed to get current program's module handle. (hInstance=0x%X)\n", hInstance );
	}
	CONTRL_TRACE( "Got current program's module handle. (hInstance=0x%X)", *hInstance );

	// Enable control escape codes to save/reset cursor position.
	{
		DWORD dwMode;
		GetConsoleMode( hInstance, &dwMode );
		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode( hInstance, dwMode );
	}

	/* Initialize DirectInput */

	HRESULT hDIResult;
	LPDIRECTINPUT8 pDirectInput = NULL;
	hDIResult = DirectInput8Create(
		/* hModuleInstance */ hInstance,
		/*       dwVersion */ DIRECTINPUT_VERSION,
		/*  pInterfaceGUID */ &IID_IDirectInput8,
		/*   ppDirectInput */ &pDirectInput,
		/*         pUnused */ NULL );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -2, "Failed to initialize DirectInput8. (0x%X)\n", hDIResult );
	}
	CONTRL_TRACE( "Initialized DirectInput8.", NULL );

	/* Enumerate attached controller devices */

	int deviceInstancesSize = 8;
	DIDEVICEINSTANCE *deviceInstances = CONTRL_ALLOC( deviceInstancesSize, DIDEVICEINSTANCE );
	if ( deviceInstances == NULL ) {
		CONTRL_ERROR( -3, "Failed to allocate %d bytes of memory for device enumeration buffer.\n", deviceInstancesSize );
	}

	DeviceCountContext ctxDeviceCount = { .nDevices = 0 };
enumDevices:
	hDIResult = IDirectInput8_EnumDevices(
		/* pDirectInput */ pDirectInput,
		/*    dwDevType */ DI8DEVCLASS_GAMECTRL,
		/*    pCallback */ contrl__device_count_callback,
		/*     pContext */ &ctxDeviceCount,
		/*      dwFlags */ DIEDFL_ATTACHEDONLY );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -4, "Failed to enumerate attached devices. (0x%X)\n", hDIResult );
	} else if ( ctxDeviceCount.nDevices < 1 ) {
		CONTRL_WARN( "No attached controllers found. Connect one and try again.\n", NULL );
		system( "pause" );
		goto enumDevices;
	}

	// At this point, at least 1 device is found.

	// Grow array if there is not enough space.
	if ( ctxDeviceCount.nDevices > deviceInstancesSize ) {
		int newSize = ctxDeviceCount.nDevices;
		deviceInstances = CONTRL_REALLOC( deviceInstances, deviceInstancesSize, newSize, DIDEVICEINSTANCE );
		deviceInstancesSize = newSize;
	};

	DeviceGetAllContext ctxDeviceGetAll = {
		.pDirectInput = pDirectInput,
		.pDeviceInstances = deviceInstances,
		.nDevicesMax = deviceInstancesSize,
		.nDevicesGot = 0
	};
	hDIResult = IDirectInput8_EnumDevices(
		/* pDirectInput */ pDirectInput,
		/*    dwDevType */ DI8DEVCLASS_GAMECTRL,
		/*    pCallback */ contrl__device_get_all_callback,
		/*     pContext */ &ctxDeviceGetAll,
		/*      dwFlags */ DIEDFL_ATTACHEDONLY );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -5, "Failed to retrieve enumerated devices. (0x%X)\n", hDIResult );
	}

	/* Pick controller device */

	DeviceInfo deviceInfo = { 0 };
	int selected_controller_idx = 0;
	if ( ctxDeviceGetAll.nDevicesGot == 1 ) {
		// If there is only one attached controller device, pick it automatically.
		const DIDEVICEINSTANCE *pDeviceInstance = &deviceInstances[ 0 ];
		contrl_device_get_info( pDeviceInstance, &deviceInfo );
		const DeviceInfo *const i = &deviceInfo;
		CONTRL_PRINT( "Found attached controller device. (\"%s\", VendorID: 0x%04X (%s), ProductID: 0x%04X (%s))\n",
			pDeviceInstance->tszProductName, i->VID, i->pszVendorName, i->PID, i->pszProductName );
	} else {
		// There are multiple attached controller devices, let user pick one.
		CONTRL_PRINT( "Found multiple attached controller devices:\n", NULL );
		for ( int i = 0; i < ctxDeviceGetAll.nDevicesGot; i += 1 ) {
			const DIDEVICEINSTANCE *const di = &deviceInstances[ i ];  // di - device instance
			DeviceInfo info = { 0 };
			contrl_device_get_info( di, &info );
			// 1-based indexing for end-user.
			CONTRL_PRINT( "[%d] - \"%s\", VendorID: 0x%04X (%s), ProductID: 0x%04X (%s)\n",
				i + 1, di->tszProductName, info.VID, info.pszVendorName, info.PID, info.pszProductName );
		}
		CONTRL_PRINT( "Pick one by writing an index of that device (1-%d): ", ctxDeviceGetAll.nDevicesGot );

pickDevice:
		fscanf( stdin, "%d", &selected_controller_idx );
		if ( selected_controller_idx < 1 || selected_controller_idx > ctxDeviceGetAll.nDevicesGot ) {
			CONTRL_WARN( "Not a valid number in the range of 1-%d. Try again: ", ctxDeviceGetAll.nDevicesGot );
			goto pickDevice;
		}
		selected_controller_idx -= 1;  // From 1-based end-user indexing to 0-based internal indexing.
	}
	CONTRL_TRACE( "Selected controller index: %d", selected_controller_idx );
	const DIDEVICEINSTANCE *pDeviceInstance = &deviceInstances[ selected_controller_idx ];
	contrl_device_get_info( pDeviceInstance, &deviceInfo );

#if CONTRL_DEBUG
	contrl__debug_print_device_info( pDeviceInstance );
#endif

	/* Create device */

	LPDIRECTINPUTDEVICE8 pControllerDevice = NULL;
	hDIResult = IDirectInput8_CreateDevice(
		/*        pDirectInput */ pDirectInput,
		/*       pInstanceGUID */ &pDeviceInstance->guidInstance,
		/* ppDirectInputDevice */ &pControllerDevice,
		/*             pUnused */ NULL );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -6, "Failed to create DirectInput8 device. (0x%X)", hDIResult );
	}

	/* Get device capabilities */

	DIDEVCAPS diDeviceCapabilities = { 0 };
	diDeviceCapabilities.dwSize = sizeof( DIDEVCAPS );
	hDIResult = IDirectInputDevice8_GetCapabilities(
		/*    pDevice */ pControllerDevice,
		/* pDIDevCaps */ &diDeviceCapabilities );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -7, "Failed to get controller device capabilities. (0x%X)\n", hDIResult );
	}

#if CONTRL_DEBUG
	contrl__debug_print_device_capabilities( &diDeviceCapabilities );
#endif

	const DIDEVCAPS *const caps = &diDeviceCapabilities;
	CONTRL_PRINT( "Device uses: %u axes, %u buttons, %u POVs.\n",
		caps->dwAxes, caps->dwButtons, caps->dwPOVs );
	bool ffb_supported = C_BOOL( caps->dwFlags & DIDC_FORCEFEEDBACK );
	if ( ffb_supported ) {
		CONTRL_PRINT( "Device supports Force-FeedBack. (Sample Period: %u, Min Time Resolution: %u)\n",
			caps->dwFFSamplePeriod, caps->dwFFMinTimeResolution );
	}

	/* Set Joystick data format */

	hDIResult = IDirectInputDevice8_SetDataFormat(
		/*       pDevice */ pControllerDevice,
		/* pDIDataFormat */ &c_dfDIJoystick );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -8, "Failed to set controller device data format to '%s' (0x%X).\n", "c_dfDIJoystick", hDIResult );
	}
	CONTRL_TRACE( "Set controller device data format to '%s'.", "c_dfDIJoystick" );

	HWND hWindow = GetConsoleWindow();
	if ( hWindow == NULL ) {
		CONTRL_ERROR( -9, "Failed to get console window. (hWindow=0x%X)\n", hWindow );
	}
	CONTRL_TRACE( "Got console window. (hWindow=0x%X)", hWindow );
	
	/* Set cooperative level */

	// Use of `DISCL_EXCLUSIVE | DISCL_FOREGROUND` results in permission denial.
	// It works with `DISCL_NONEXCLUSIVE | DISCL_BACKGROUND` just fine.
	hDIResult = IDirectInputDevice8_SetCooperativeLevel(
		/* pDevice */ pControllerDevice,
		/* hWindow */ hWindow,
		/* dwFlags */ DISCL_NONEXCLUSIVE | DISCL_BACKGROUND );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -10, "Failed to set controller device cooperative level to %s. (0x%X)\n", "DISCL_NONEXCLUSIVE | DISCL_BACKGROUND", hDIResult );
	}
	CONTRL_TRACE( "Set controller device cooperative level to %s.", "DISCL_NONEXCLUSIVE | DISCL_BACKGROUND" );

	/* Acquire device */

	hDIResult = IDirectInputDevice8_Acquire(
		/* pDevice */ pControllerDevice );
	if ( hDIResult != DI_OK ) {
		CONTRL_ERROR( -11, "Failed to acquire controller device. (0x%X)\n", hDIResult );
	}
	CONTRL_TRACE( "Acquired controller device.", NULL );

	int pollRate = 60;  // 60Hz = 60 times per second
	float pollTimeIntervalMs = 1000.0f / pollRate;
	DWORD dwPollTimeIntervalMs = ( DWORD )pollTimeIntervalMs;  // at 60Hz, 1000 / 60 = 16.666f = 16

	HANDLE hConsoleOutput = GetStdHandle( STD_OUTPUT_HANDLE );

	// Save cursor position to then overwrite previous output.
	WriteConsoleA( hConsoleOutput, CONSOLE_SC, CONSOLE_SC_LEN, NULL, NULL );

	// Allocate controller state output string buffer on heap.
	char *buffer = CONTRL_ALLOC( STATE_BUFFER_SIZE, char );
	if ( buffer == NULL ) {
		CONTRL_ERROR( -12, "Failed to allocate %d bytes of memory for string buffer.\n", STATE_BUFFER_SIZE );
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

		/* Read controller device state (in Joystick format) */

		hResult = IDirectInputDevice8_GetDeviceState(
			/*    pDevice */ pControllerDevice, 
			/* dwDataSize */ sizeof( DIJOYSTATE ),
			/*      pData */ &js );
		if ( hResult != DI_OK ) {
			CONTRL_WARN( "Failed to get controller device state. (0x%X)\n", hResult );
			continue;
		}

		// Overwrite output with new data.
		int written = deviceInfo.print_device_state_fn( buffer, STATE_BUFFER_SIZE, &js );
		WriteConsoleA( hConsoleOutput, buffer, written, NULL, NULL );
	}

	// The program actually never gets to this point.
	// We are just being good guys.
	IDirectInputDevice8_Release(
		/* pDevice */ pControllerDevice );
	CONTRL_FREE( buffer );
	CONTRL_FREE( deviceInstances );

	return 0;
}
