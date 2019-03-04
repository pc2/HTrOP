#ifndef _MOTION_H
#define _MOTION_H

#ifdef MAKE_STRING
#undef MAKE_STRING
#endif
#define MAKE_STRING( msg )  ( ((std::ostringstream&)((std::ostringstream() << '\x0') << msg)).str().substr(1) )


#define DEPTH 3

//motion detection macros
#define MD_THRESHOLD 10
#define EROSION_MAP_SIZE 7      /*must be an odd number */
#define FG 0
#define BG 255
#define ER_THRESHOLD 0.7

// Pragma helper, if LLVMIR is generated.
#ifdef LLVMIR
void *__dso_handle = NULL;
#endif

// Default to LARGE_DATASET
#if !defined(MINI_DATASET) && !defined(SMALL_DATASET) && !defined(LARGE_DATASET) && !defined(EXTRALARGE_DATASET)
#define LARGE_DATASET
#endif

// Do not define anything if the user manually defines the size.
#if !defined(ROWS) && !defined(COLS)

// Define the possible dataset sizes.
#ifdef MINI_DATASET
#define ROWS 720
#define COLS 1280
#endif

#ifdef SMALL_DATASET
#define ROWS 2048
#define COLS 1080
#endif

// 4k
#ifdef STANDARD_DATASET
#define ROWS 4096
#define COLS 2160
#endif

// 8k
#ifdef LARGE_DATASET            /* Default if unspecified. */
#define ROWS 7680
#define COLS 4320
#endif

// 16k
#ifdef EXTRALARGE_DATASET
#define ROWS 15360
#define COLS 8640
#endif

#endif

#endif                          //_MOTION_H
