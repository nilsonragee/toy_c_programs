#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

// How many decimal digits a 64-bit (2 ^ 64) integer (`uint64_t`) can hold.
#define INT64_MAX_DIGITS 20

#ifndef FTDC_CUSTOM_BOOL
enum bool_e {
	false = 0,
	true
};
typedef uint8_t bool;
#endif

#define FTDC_ALLOC( count, type ) \
	malloc( count * sizeof( type ) )
#define FTDC_REALLOC( pointer, old_size, new_size, type ) \
	realloc( pointer, new_size * sizeof( type ) )
#define FTDC_FREE( pointer ) \
	free( pointer )

#ifndef FTDC_DEBUG
	#define FTDC_DEBUG 0
#endif

#if FTDC_DEBUG
	#define FTDC_TRACE( format, ... ) \
		ftdc_fprintf( stdout, "TRACE: ", 7, format, __VA_ARGS__ ); \
		putc( '\n', stdout )
#else
	#define FTDC_TRACE( format, ... )  
#endif /* FTDC_DEBUG */

#define FTDC_ERROR( exit_code, format, ... ) \
	ftdc_fprintf( stderr, "ERROR: ", 7, format, __VA_ARGS__ ); \
	exit( exit_code )

#define FTDC_WARN( format, ... ) \
	ftdc_fprintf( stdout, "WARNING: ", 9, format, __VA_ARGS__ )

#define FTDC_PRINT( format, ... ) \
	ftdc_fprintf( stdout, NULL, 0, format, __VA_ARGS__ )

void ftdc_fprintf( FILE *stream, const char *prefix, size_t prefix_size, const char *format, ... ) {
	fwrite( prefix, sizeof( char ), prefix_size, stream );
	va_list args;
	va_start( args, format );
	vfprintf( stream, format, args );
	va_end( args );
}

// Appends any char.  Returns chars written.
// Chainable:  `cursor = append( buffer, size, cursor, '.' );`
int ftdc_append_char( char *buffer, size_t buffer_size, int cursor, char append ) {
	buffer[ cursor ] = append;
	return 1;
}

// Appends digit in the range of [0; 9].  Returns chars written.
// Chainable:  `cursor += append_digit( buffer, size, cursor, 1 );`
int ftdc_append_digit( char *buffer, size_t buffer_size, int cursor, char digit ) {
	if ( digit < 0 || digit > 9 ) {
		FTDC_WARN( "`append_digit`: Trying to append digit (%d) outside of expected 0-9 range!"
			" If you meant to append non-digit char, use `append_char`.\n", digit );
		return 0;
	}

	int written = ftdc_append_char( buffer, buffer_size, cursor, '0' + digit );  // Quick conversion to ASCII digit number char.
	return written;
}

// Appends `uint64_t` value number.  Returns chars written.
// Chainable:  `cursor += ftdc_append_uint64( buffer, size, cursor, number, 0 );`
int ftdc_append_uint64( char *buffer, size_t buffer_size, int cursor, uint64_t number, uint64_t magnitude ) {
	int old_cursor = cursor;
	if ( magnitude <= 1 ) {
		// Compute magnitude ourselves.
		magnitude = 1;  // In case if 0 is passed
		uint64_t num = number;
		while ( num >= 10 ) {
			num /= 10;
			magnitude *= 10;
		}
	}

	char digit;
	while ( magnitude >= 10 ) {
		digit = ( number / magnitude ) % 10;
		cursor += ftdc_append_digit( buffer, buffer_size, cursor, digit );
		magnitude /= 10;
	}
	digit = number % 10;
	cursor += ftdc_append_digit( buffer, buffer_size, cursor, digit );

	return cursor - old_cursor;
}

// Returns (G)reatest (C)ommon (D)ivisor.
uint64_t ftdc_GCD( uint64_t a, uint64_t b ) {
	uint64_t r = 0; // Remainder
	// Non-recursive iteration:
	// Instead of recursively calling itself, bloating the call stack,
	// just jump to the beginning of the loop.
loop:
	while ( b != 0 ) {
		// Same as: `a = GCD( b, a % b );`
		r = a % b;
		a = b;
		b = r;
		goto loop;
	}
	return a;
}

// Returns pointer to the beginning of the value string,
//   or NULL if '=' not found.
char *ftdc_skip_to_arg_value( char *arg ) {
	while( *arg != '\0' && *arg != '=' )  arg += sizeof( char );
	if ( *arg == '\0' )  return NULL;
	else                 arg += sizeof( char );  // Advance past '='
	return arg;
}

void ftdc_print_usage( void ) {
	FTDC_PRINT( "Usage: `ftdc [--option=value] [-O=value...] -- <numerator> <denominator>`\n"
		"Example: `ftdc --precision=50 -- 3 10`\n"
		"     or: `ftdc -P=50 -- 3 10`\n"
		"\n"
		"Options:\n"
		"  help, --help:       Prints help message.\n"
		"  -P,   --precision:  Sets fractional part precision, e.g. the number of digits after dot.\n", NULL );
}

int main( int arguments_count, char *arguments[] ) {
	if ( arguments_count < 2 ) {
		// If no arguments provided (excluding executable path), print help message.
		ftdc_print_usage();
		return 0;
	}

	uint64_t dec_frac_digits_max = 50; // Max number of digits to compute in fractional part.

	/* Parse optional arguments */

	int arg_cursor = 0;  // Skip first argument (executable path)
	char *arg;// = arguments[ arg_cursor ];
	// arg = ftdc_skip_dashes( arg );
	bool last_option_value_valid = true;
	do {
		arg_cursor += 1;
		arg = arguments[ arg_cursor ];
		
		if ( strncmp( arg, "help", 4 ) == 0 || strncmp( arg, "--help", 6 ) == 0 ) {
			ftdc_print_usage();
			return 0;
		} else if ( strncmp( arg, "-P", 2 ) == 0 || strncmp( arg, "--precision", 11 ) == 0 ) {
			char *value_str = ftdc_skip_to_arg_value( arg );
			if ( value_str == NULL ) {
				FTDC_WARN( "Option '%s' did not specify value, ignoring it.", arg );
				FTDC_PRINT( " Correct usage: `--option=value`.\n", NULL );
				last_option_value_valid = false;
			} else {
				uint64_t value = strtoull( value_str, NULL, 10 );  // Parse argument value
				if ( value == 0 && strcmp( value_str, "0" ) != 0 ) {
					FTDC_WARN( "Specified value '%s' in option '%s' is not valid, ignoring it.", value_str, arg );
					FTDC_PRINT( " Correct usage: `--option=value`.\n", NULL );
					last_option_value_valid = false;
				} else {
					dec_frac_digits_max = value;
					last_option_value_valid = true;
					FTDC_PRINT( "Set fractional pricision digits to %llu.\n", value );
				}
			}
		} else if ( strcmp( arg, "--" ) == 0 ) {
			break;
		} else {
			FTDC_WARN( "Unknown option '%s', ignoring it.", arg );
			if ( arg[ 0 ] != '-' )  {
				if ( !last_option_value_valid ) {
					FTDC_PRINT( " Did you mean to provide it as previous option value?\n", NULL );
				} else {
					FTDC_PRINT( " Did you forget to end option argument list with '--'?\n", NULL );
				}
				last_option_value_valid = true;
			} else {
				FTDC_PRINT( "\n", NULL );
			}
		}

	} while ( arg_cursor < arguments_count - 1 );

	if ( strcmp( arg, "--" ) != 0 ) {
		FTDC_ERROR( -1, "Argument list is not closed with '--'.\n", NULL );
	}

	arg_cursor += 1;
	arg = arguments[ arg_cursor ];

	/* Parse user arguments */

	int user_arguments_count = arguments_count - arg_cursor;
	if ( user_arguments_count < 1 ) {
		FTDC_ERROR( -2, "No user arguments provided. Expected: <numerator> <denominator>.", NULL );
	} else if ( user_arguments_count == 1 ) {
		FTDC_ERROR( -3, "Only <numerator> user argument provided. Expected: <numerator> <denominator>.", NULL );
	} else if ( user_arguments_count > 2 ) {
		FTDC_ERROR( -4, "Too many user arguments provided. Expected: <numerator> <denominator>.", NULL );
	}

	char *arg_numerator = arguments[ arg_cursor ];
	arg_cursor += 1;
	char *arg_denominator = arguments[ arg_cursor ];
	arg_cursor += 1;
	/* Numerator 0 is valid.  0 / 1 = 0.
	if ( arg_numerator[ 0 ] == '0' ) {
		FTDC_ERROR( -5, "Numerator cannot be 0.", NULL );ś
	}
	*/
	if ( arg_denominator[ 0 ] == '0' ) {
		FTDC_ERROR( -6, "Denominator cannot be 0.", NULL );
	}
	
	// Fraction number from input
	uint64_t given_frac_num = strtoull( arg_numerator, NULL, 10 );
	if ( given_frac_num == 0 && arg_numerator[ 0 ] != '0' ) {
		FTDC_ERROR( -7, "Numerator '%s' is not a valid integer number.", arg_numerator );
	}

	// Fraction denominator from input
	uint64_t given_frac_denom = strtoull( arg_denominator, NULL, 10 );
	if ( given_frac_denom == 0 ) {
		FTDC_ERROR( -8, "Denominator '%s' is not a valid integer number.\n", arg_denominator );
	}

	// Copy values to keep given fraction for result print.
	uint64_t frac_num = given_frac_num;
	uint64_t frac_denom = given_frac_denom;

	uint64_t dec_int = 0; // Decimal integer part:  [123].456

	/* 1. Simplify fraction */

	//  13 / 5  ->  13 / 5 
	//  15 / 5  ->   3 / 1
	//  5 / 15  ->   1 / 3
	if ( frac_num % frac_denom == 0 ) {
		// Greatest common divisor
		uint64_t divisor = ftdc_GCD( frac_num, frac_denom );
		frac_num /= divisor;
		frac_denom /= divisor;
	}

	FTDC_TRACE( "1. Simplify:  %llu / %llu  ->  %llu / %llu",
		given_frac_num, given_frac_denom,
		frac_num, frac_denom );
	
	/* 2. Extract integer part */

	// 13 / 5  ->  2  +  3 / 5 
	//  3 / 1  ->  3  +  0 / 1
	//  1 / 3  ->  1 / 3
	{
		uint64_t div_fraction = frac_num % frac_denom;  // Division fraction (after simplification)
		if ( div_fraction == 0 ) {
			//  3 / 1  ->  3  +  0 / 1
			dec_int = frac_num;
			frac_num = 0;
		} else if ( div_fraction > 0 ) {
			// 13 / 5  ->  2  +  3 / 5 
			dec_int = ( frac_num - div_fraction ) / frac_denom;
			frac_num = div_fraction; 
		}
	}
	uint64_t simpled_num = frac_num; // Copy to keep for result print.

	FTDC_TRACE( "2. Extract integer:  %llu / %llu  ->  %llu  +  %llu / %llu",
		frac_num + ( dec_int * frac_denom ), frac_denom,
		dec_int, frac_num, frac_denom );

	size_t decimal_str_size = ( INT64_MAX_DIGITS + 1 /* '.' */ + dec_frac_digits_max + 1 /* '\n' */ );
	if ( decimal_str_size > ( 1llu << 30 ) )
		FTDC_WARN( "Trying to allocate more than 1GB (2^30) memory for decimal string of %llu fractional digits.\n",
			dec_frac_digits_max );

	// Allocate string buffer on heap for decimal notation number string representation.
	char *decimal_str = FTDC_ALLOC( decimal_str_size, char );
	if ( decimal_str == NULL ) {
		FTDC_ERROR( -9, "Could not allocate %zu bytes of memory for decimal string of %llu fractional digits.",
			decimal_str_size, dec_frac_digits_max );
	}

	int cursor = 0;
	cursor += ftdc_append_uint64( decimal_str, decimal_str_size, cursor, dec_int, 0 );  // Append integer part

	if ( frac_num == 0 ) {
		// Given fraction turned out to be an integer decimal number,
		//   no need to compute fractional part.  Jump to end.
		FTDC_TRACE( "No fraction part, jump to end.", NULL );
		goto end;
	}
	
	/* 3. Compute fractional part */

	FTDC_TRACE( "3. Compute fractional part of:  %llu / %llu", frac_num, frac_denom );

	cursor += ftdc_append_char( decimal_str, decimal_str_size, cursor, '.' );  // Append fractional part separator

	uint64_t remainder;
	uint64_t dec_frac_digits = 0; // Number of digits in fractional part:  123.[456] -> 3
	char digit;
	while ( dec_frac_digits < dec_frac_digits_max ) {
		dec_frac_digits += 1;

		// In short: Advance to the next order of magnitude.
		//   Try to imagine it like this:  1 / 10     ->  1 / 100
		//                                  0.4[2]85  ->   0.42[8]5
		// Arithmetically, on example of 3 / 7 (frac_num=3, frac_denom=7):
		//
		//  3 \ * 10     30     28    2     4 * 7 \ : 7    2      4     2            2
		//  -         =  --  =  -- + --  =  -----       + --  =  -- +  --  =  0.4 + --
		//  7            70     70   70       70          70     10    70           70
		frac_num *= 10;
		remainder = frac_num % frac_denom;
		digit = ( frac_num - remainder ) / frac_denom;
		FTDC_TRACE( "[%llu]  ( %llu + %llu ) / %llu  ->  %d",
			dec_frac_digits,
			frac_num - remainder, remainder, frac_denom, digit );

		cursor += ftdc_append_digit( decimal_str, decimal_str_size, cursor, digit );  // Append fractional digit
		
		if ( remainder == 0 ) {
			// Fraction numerator is evenly divisable by denominator,
			//   no more fractions left.
			FTDC_TRACE( "No more fractions left to compute, exit loop.", NULL );
			break;
		}
		frac_num = remainder;
	};

end:
	cursor += ftdc_append_char( decimal_str, decimal_str_size, cursor, '\0' );  // Null-terminate

	uint64_t unified_num = ( dec_int * frac_denom ) + simpled_num;
	FTDC_PRINT(
		"Given:  %llu / %llu\n"
		"Simplified:  %llu / %llu\n"
		"Result:  %s  ( %llu  +  %llu / %llu )\n",
		given_frac_num, given_frac_denom,
		unified_num, frac_denom,
		decimal_str, dec_int, simpled_num, frac_denom
	);

	FTDC_FREE( decimal_str );

	return 0;
}