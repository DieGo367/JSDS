/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "jerryscript.h"
#include "jerryscript-port-default.h"
#include <nds/ndstypes.h>
#include <nds/arm9/input.h>
#include <nds/arm9/video.h>
#include <nds/interrupts.h>


// Custom extension to the jerry port that allows a callback on promise rejections
void (*promiseRejectionOpCallback) (jerry_value_t, jerry_promise_rejection_operation_t);
void jerry_jsds_set_promise_rejection_op_callback(void (*callback) (jerry_value_t, jerry_promise_rejection_operation_t)) {
  promiseRejectionOpCallback = callback;
}

/**
 * Implementation of jerry_port_track_promise_rejection for JSDS.
 * Calls the promiseRejectionOpCallback on rejected promises.
 */
void
jerry_port_track_promise_rejection (const jerry_value_t promise, /**< rejected promise */
                                    const jerry_promise_rejection_operation_t operation) /**< operation */
{
  promiseRejectionOpCallback(promise, operation);
} /* jerry_port_track_promise_rejection */

#if !defined (_WIN32)
#include <libgen.h>
#endif /* !defined (_WIN32) */
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

/**
 * Determines the size of the given file.
 * @return size of the file
 */
static size_t
jerry_port_get_file_size (FILE *file_p) /**< opened file */
{
  fseek (file_p, 0, SEEK_END);
  long size = ftell (file_p);
  fseek (file_p, 0, SEEK_SET);

  return (size_t) size;
} /* jerry_port_get_file_size */

/**
 * Opens file with the given path and reads its source.
 * @return the source of the file
 */
uint8_t *
jerry_port_read_source (const char *file_name_p, /**< file name */
                        size_t *out_size_p) /**< [out] read bytes */
{
  struct stat stat_buffer;
  if (stat (file_name_p, &stat_buffer) == -1 || S_ISDIR (stat_buffer.st_mode))
  {
    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to open file: %s\n", file_name_p);
    return NULL;
  }

  FILE *file_p = fopen (file_name_p, "rb");

  if (file_p == NULL)
  {
    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to open file: %s\n", file_name_p);
    return NULL;
  }

  size_t file_size = jerry_port_get_file_size (file_p);
  uint8_t *buffer_p = (uint8_t *) malloc (file_size);

  if (buffer_p == NULL)
  {
    fclose (file_p);

    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to allocate memory for file: %s\n", file_name_p);
    return NULL;
  }

  size_t bytes_read = fread (buffer_p, 1u, file_size, file_p);

  if (bytes_read != file_size)
  {
    fclose (file_p);
    free (buffer_p);

    jerry_port_log (JERRY_LOG_LEVEL_ERROR, "Error: Failed to read file: %s\n", file_name_p);
    return NULL;
  }

  fclose (file_p);
  *out_size_p = bytes_read;

  return buffer_p;
} /* jerry_port_read_source */

/**
 * Release the previously opened file's content.
 */
void
jerry_port_release_source (uint8_t *buffer_p) /**< buffer to free */
{
  free (buffer_p);
} /* jerry_port_release_source */

/**
 * Normalize a file path
 *
 * @return length of the path written to the output buffer
 */
size_t
jerry_port_normalize_path (const char *in_path_p,   /**< input file path */
                           char *out_buf_p,         /**< output buffer */
                           size_t out_buf_size,     /**< size of output buffer */
                           char *base_file_p)       /**< base file path */
{
  size_t ret = 0;

#if defined (_WIN32)
  size_t base_drive_dir_len;
  const size_t in_path_len = strnlen (in_path_p, _MAX_PATH);
  char *path_p;

  if (base_file_p != NULL)
  {
    char drive[_MAX_DRIVE];
    char *dir_p = (char *) malloc (_MAX_DIR);

    _splitpath_s (base_file_p, drive, _MAX_DRIVE, dir_p, _MAX_DIR, NULL, 0, NULL, 0);
    const size_t drive_len = strnlen ((const char *) &drive, _MAX_DRIVE);
    const size_t dir_len = strnlen (dir_p, _MAX_DIR);
    base_drive_dir_len = drive_len + dir_len;
    path_p = (char *) malloc (base_drive_dir_len + in_path_len + 1);

    memcpy (path_p, &drive, drive_len);
    memcpy (path_p + drive_len, dir_p, dir_len);

    free (dir_p);
  }
  else
  {
    base_drive_dir_len = 0;
    path_p = (char *) malloc (in_path_len + 1);
  }

  memcpy (path_p + base_drive_dir_len, in_path_p, in_path_len + 1);

  char *norm_p = _fullpath (out_buf_p, path_p, out_buf_size);
  free (path_p);

  if (norm_p != NULL)
  {
    ret = strnlen (norm_p, out_buf_size);
  }
#elif defined (__unix__) || defined (__APPLE__)
  char *base_dir_p = dirname (base_file_p);
  const size_t base_dir_len = strnlen (base_dir_p, PATH_MAX);
  const size_t in_path_len = strnlen (in_path_p, PATH_MAX);
  char *path_p = (char *) malloc (base_dir_len + 1 + in_path_len + 1);

  memcpy (path_p, base_dir_p, base_dir_len);
  memcpy (path_p + base_dir_len, "/", 1);
  memcpy (path_p + base_dir_len + 1, in_path_p, in_path_len + 1);

  char *norm_p = realpath (path_p, NULL);
  free (path_p);

  if (norm_p != NULL)
  {
    const size_t norm_len = strnlen (norm_p, out_buf_size);
    if (norm_len < out_buf_size)
    {
      memcpy (out_buf_p, norm_p, norm_len + 1);
      ret = norm_len;
    }

    free (norm_p);
  }
#else
  (void) base_file_p; /* unused */

  /* Do nothing, just copy the input. */
  const size_t in_path_len = strnlen (in_path_p, out_buf_size);
  if (in_path_len < out_buf_size)
  {
    memcpy (out_buf_p, in_path_p, in_path_len + 1);
    ret = in_path_len;
  }
#endif

  return ret;
} /* jerry_port_normalize_path */

/**
 * Get the module object of a native module.
 *
 * @return Undefined, if 'name' is not a native module
 *         jerry_value_t containing the module object, otherwise
 */
jerry_value_t
jerry_port_get_native_module (jerry_value_t name) /**< module specifier */
{
  (void) name;
  return jerry_create_undefined ();
} /* jerry_port_get_native_module */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>


/**
 * Actual log level
 */
static jerry_log_level_t jerry_port_default_log_level = JERRY_LOG_LEVEL_ERROR;

/**
 * Get the log level
 *
 * @return current log level
 */
jerry_log_level_t
jerry_port_default_get_log_level (void)
{
  return jerry_port_default_log_level;
} /* jerry_port_default_get_log_level */

/**
 * Set the log level
 */
void
jerry_port_default_set_log_level (jerry_log_level_t level) /**< log level */
{
  jerry_port_default_log_level = level;
} /* jerry_port_default_set_log_level */

/**
 * Default implementation of jerry_port_log. Prints log message to the standard
 * error with 'vfprintf' if message log level is less than or equal to the
 * current log level.
 *
 * If debugger support is enabled, printing happens first to an in-memory buffer,
 * which is then sent both to the standard error and to the debugger client.
 */
void
jerry_port_log (jerry_log_level_t level, /**< message log level */
                const char *format, /**< format string */
                ...)  /**< parameters */
{
  if (level <= jerry_port_default_log_level)
  {
    va_list args;
    va_start (args, format);
#if defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1)
    int length = vsnprintf (NULL, 0, format, args);
    va_end (args);
    va_start (args, format);

    JERRY_VLA (char, buffer, length + 1);
    vsnprintf (buffer, (size_t) length + 1, format, args);

    fprintf (stderr, "%s", buffer);
    jerry_debugger_send_log (level, (jerry_char_t *) buffer, (jerry_size_t) length);
#else /* If jerry-debugger isn't defined, libc is turned on */
    putchar('\n');
    vfprintf (stderr, format, args);
#endif /* defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1) */
    va_end (args);
  }
} /* jerry_port_log */

#if defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1)

#define DEBUG_BUFFER_SIZE (256)
static char debug_buffer[DEBUG_BUFFER_SIZE];
static int debug_buffer_index = 0;

#endif /* defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1) */

/**
 * Default implementation of jerry_port_print_char. Uses 'putchar' to
 * print a single character to standard output.
 */
void
jerry_port_print_char (char c) /**< the character to print */
{
  putchar (c);

#if defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1)
  debug_buffer[debug_buffer_index++] = c;

  if ((debug_buffer_index == DEBUG_BUFFER_SIZE) || (c == '\n'))
  {
    jerry_debugger_send_output ((jerry_char_t *) debug_buffer, (jerry_size_t) debug_buffer_index);
    debug_buffer_index = 0;
  }
#endif /* defined (JERRY_DEBUGGER) && (JERRY_DEBUGGER == 1) */
} /* jerry_port_print_char */

#include <stdlib.h>

/**
 * Implementation of jerry_port_fatal for JSDS.
 * Waits for START button before calling 'abort' if code is
 * non-zero, calls 'exit' otherwise.
 */
void jerry_port_fatal (jerry_fatal_code_t code) /**< cause of error */
{
  if (code != 0
      /*&& code != ERR_OUT_OF_MEMORY*/)
  {
    BG_PALETTE_SUB[0] = 0x001F;
    while(true) {
      swiWaitForVBlank();
      scanKeys();
      if (keysDown() & KEY_START) break;
    }
    abort ();
  }

  exit ((int) code);
} /* jerry_port_fatal */


/**
 * Pointer to the current context.
 * Note that it is a global variable, and is not a thread safe implementation.
 */
static jerry_context_t *current_context_p = NULL;

/**
 * Set the current_context_p as the passed pointer.
 */
void
jerry_port_default_set_current_context (jerry_context_t *context_p) /**< points to the created context */
{
  current_context_p = context_p;
} /* jerry_port_default_set_current_context */

/**
 * Get the current context.
 *
 * @return the pointer to the current context
 */
jerry_context_t *
jerry_port_get_current_context (void)
{
  return current_context_p;
} /* jerry_port_get_current_context */

#if !defined (_XOPEN_SOURCE) || _XOPEN_SOURCE < 500
#undef _XOPEN_SOURCE
/* Required macro for sleep functions (nanosleep or usleep) */
#define _XOPEN_SOURCE 500
#endif

#ifdef _WIN32
#include <windows.h>
#elif defined (HAVE_TIME_H)
#include <time.h>
#elif defined (HAVE_UNISTD_H)
#include <unistd.h>
#endif /* _WIN32 */


/**
 * Default implementation of jerry_port_sleep. Uses 'nanosleep' or 'usleep' if
 * available on the system, does nothing otherwise.
 */
void jerry_port_sleep (uint32_t sleep_time) /**< milliseconds to sleep */
{
#ifdef _WIN32
  Sleep (sleep_time);
#elif defined (HAVE_TIME_H)
  struct timespec sleep_timespec;
  sleep_timespec.tv_sec = (time_t) sleep_time / 1000;
  sleep_timespec.tv_nsec = ((long int) sleep_time % 1000) * 1000000L;

  nanosleep (&sleep_timespec, NULL);
#elif defined (HAVE_UNISTD_H)
  usleep ((useconds_t) sleep_time * 1000);
#else
  (void) sleep_time;
#endif /* HAVE_TIME_H */
} /* jerry_port_sleep */

#ifdef HAVE_TM_GMTOFF
#include <time.h>
#endif /* HAVE_TM_GMTOFF */

#ifdef _WIN32
#include <windows.h>
#include <winbase.h>
#include <winnt.h>
#include <time.h>
#endif /* _WIN32 */

#ifdef __GNUC__
#include <sys/time.h>
#endif /* __GNUC__ */


#ifdef _WIN32
static const LONGLONG UnixEpochInTicks = 116444736000000000; /* difference between 1970 and 1601 */
static const LONGLONG TicksPerMs = 10000; /* 1 tick is 100 nanoseconds */

/* https://support.microsoft.com/en-us/help/167296/how-to-convert-a-unix-time-t-to-a-win32-filetime-or-systemtime */
static void UnixTimeMsToFileTime (double t, LPFILETIME pft)
{
  LONGLONG ll = (LONGLONG) t * TicksPerMs + UnixEpochInTicks;
  pft->dwLowDateTime = (DWORD) ll;
  pft->dwHighDateTime = (DWORD) (ll >> 32);
} /* UnixTimeMsToFileTime */

static double FileTimeToUnixTimeMs (FILETIME ft)
{
  ULARGE_INTEGER date;
  date.HighPart = ft.dwHighDateTime;
  date.LowPart = ft.dwLowDateTime;
  return (double) (((LONGLONG) date.QuadPart - UnixEpochInTicks) / TicksPerMs);
} /* FileTimeToUnixTimeMs */

#endif /* _WIN32 */

/**
 * Default implementation of jerry_port_get_local_time_zone_adjustment. Uses the 'tm_gmtoff' field
 * of 'struct tm' (a GNU extension) filled by 'localtime_r' if available on the
 * system, does nothing otherwise.
 *
 * @return offset between UTC and local time at the given unix timestamp, if
 *         available. Otherwise, returns 0, assuming UTC time.
 */
double jerry_port_get_local_time_zone_adjustment (double unix_ms,  /**< ms since unix epoch */
                                                  bool is_utc)  /**< is the time above in UTC? */
{
#ifdef HAVE_TM_GMTOFF
  struct tm tm;
  time_t now = (time_t) (unix_ms / 1000);
  localtime_r (&now, &tm);
  if (!is_utc)
  {
    now -= tm.tm_gmtoff;
    localtime_r (&now, &tm);
  }
  return ((double) tm.tm_gmtoff) * 1000;
#else /* !HAVE_TM_GMTOFF */
  (void) unix_ms;
  (void) is_utc;
#ifdef _WIN32
  FILETIME fileTime, localFileTime;
  SYSTEMTIME systemTime, localSystemTime;
  ULARGE_INTEGER time, localTime;

  UnixTimeMsToFileTime (unix_ms, &fileTime);

  if (FileTimeToSystemTime (&fileTime, &systemTime)
      && SystemTimeToTzSpecificLocalTime (0, &systemTime, &localSystemTime)
      && SystemTimeToFileTime (&localSystemTime, &localFileTime))
  {
    time.LowPart = fileTime.dwLowDateTime;
    time.HighPart = fileTime.dwHighDateTime;
    localTime.LowPart = localFileTime.dwLowDateTime;
    localTime.HighPart = localFileTime.dwHighDateTime;
    return (double) (((LONGLONG) localTime.QuadPart - (LONGLONG) time.QuadPart) / TicksPerMs);
  }
#endif /* _WIN32 */
  return 0.0;
#endif /* HAVE_TM_GMTOFF */
} /* jerry_port_get_local_time_zone_adjustment */

/**
 * Default implementation of jerry_port_get_current_time. Uses 'gettimeofday' if
 * available on the system, does nothing otherwise.
 *
 * @return milliseconds since Unix epoch - if 'gettimeofday' is available and
 *                                         executed successfully,
 *         0 - otherwise.
 */
double jerry_port_get_current_time (void)
{
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime (&ft);
  return FileTimeToUnixTimeMs (ft);
#elif __GNUC__
  struct timeval tv;

  if (gettimeofday (&tv, NULL) == 0)
  {
    return ((double) tv.tv_sec) * 1000.0 + ((double) tv.tv_usec) / 1000.0;
  }
#endif /* _WIN32 */

  return 0.0;
} /* jerry_port_get_current_time */
