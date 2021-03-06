/*
 * console.cpp
 *
 *  Created on: Apr 7, 2012
 *      Author: lion
 */

#include "ifs/console.h"
#include "ifs/assert.h"
#include "ifs/encoding.h"
#include "ifs/process.h"
#include <map>
#include "console.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

static LARGE_INTEGER systemFrequency;

inline int64_t Ticks()
{
    LARGE_INTEGER t;

    if (systemFrequency.QuadPart == 0)
        QueryPerformanceFrequency(&systemFrequency);

    QueryPerformanceCounter(&t);

    return t.QuadPart * 1000000 / systemFrequency.QuadPart;
}

#else
#include <dlfcn.h>
#include <sys/time.h>

inline int64_t Ticks()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
        return 0;
    return (tv.tv_sec * 1000000ll) + tv.tv_usec;
}

#endif

namespace fibjs
{

extern int32_t g_loglevel;

result_t console_base::get_loglevel(int32_t &retVal)
{
    retVal = g_loglevel;
    return 0;
}

result_t console_base::set_loglevel(int32_t newVal)
{
    g_loglevel = newVal;
    return 0;
}

result_t console_base::get_colors(obj_ptr<TextColor_base> &retVal)
{
    retVal = logger::get_std_color();
    return 0;
}

inline void newline(std::string &strBuffer, int padding)
{
    static char s_spc[] = "                                                                ";
    int n, n1;

    strBuffer.append("\n", 1);
    if (padding > 0)
    {
        n = padding;
        while (n)
        {
            n1 = n > 64 ? 64 : n;
            strBuffer.append(s_spc, n1);
            n -= n1;
        }
    }
}

inline std::string dir_format(v8::Local<v8::Value> obj)
{
    const char *p;
    int padding = 0;
    char ch;
    bool bStrMode = false;
    bool bNewLine = false;
    const int tab_size = 2;
    std::string strBuffer;
    std::string s;
    encoding_base::jsonEncode(obj, s);

    p = s.c_str();
    while ((ch = *p++) != 0)
    {
        if (bStrMode)
        {
            if (ch == '\\' && *p == '\"')
            {
                strBuffer.append(&ch, 1);
                ch = *p++;
            }
            else if (ch == '\"')
                bStrMode = false;
        }
        else
        {
            switch (ch)
            {
            case '[':
                if (*p == ']')
                {
                    strBuffer.append(&ch, 1);
                    ch = *p ++;
                    break;
                }

                bNewLine = true;
                padding += tab_size;
                break;
            case '{':
                if (*p == '}')
                {
                    strBuffer.append(&ch, 1);
                    ch = *p ++;
                    break;
                }

                bNewLine = true;
                padding += tab_size;
                break;
            case '}':
            case ']':
                padding -= tab_size;
                newline(strBuffer, padding);
                break;
            case ',':
                bNewLine = true;
                break;
            case ':':
                strBuffer.append(&ch, 1);
                ch = ' ';
                break;
            case '\"':
                bStrMode = true;
                break;
            }
        }

        strBuffer.append(&ch, 1);

        if (bNewLine)
        {
            newline(strBuffer, padding);
            bNewLine = false;
        }
    }

    return strBuffer;
}

std::string Format(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args, int idx = 1)
{
    const char *s = fmt;
    const char *s1;
    char ch;
    int argc = args.Length();
    std::string strBuffer;

    if (argc == 1)
        return fmt;

    while (1)
    {
        if (idx >= argc)
        {
            strBuffer.append(s);
            break;
        }

        s1 = s;
        while ((ch = *s++) && (ch != '%'))
            ;

        strBuffer.append(s1, s - s1 - 1);

        if (ch == '%')
        {
            switch (ch = *s++)
            {
            case 's':
            case 'd':
            {
                v8::String::Utf8Value s(args[idx++]);
                if (*s)
                    strBuffer.append(*s);
            }
            break;
            case 'j':
            {
                std::string s;
                s = dir_format(args[idx++]);
                strBuffer.append(s);
            }
            break;
            default:
                strBuffer.append("%", 1);
            case '%':
                strBuffer.append(&ch, 1);
                break;
            }
        }
        else
            break;
    }

    while (idx < argc)
    {
        v8::String::Utf8Value s(args[idx++]);
        if (*s)
        {
            strBuffer.append(" ", 1);
            strBuffer.append(*s);
        }
    }

    return strBuffer;
}

result_t console_base::log(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    asyncLog(_INFO, Format(fmt, args));
    return 0;
}

result_t console_base::info(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    asyncLog(_INFO, Format(fmt, args));
    return 0;
}

result_t console_base::notice(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    asyncLog(_NOTICE, Format(fmt, args));
    return 0;
}

result_t console_base::warn(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    asyncLog(_WARN, Format(fmt, args));
    return 0;
}

result_t console_base::error(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    asyncLog(_ERROR, Format(fmt, args));
    return 0;
}

result_t console_base::dir(v8::Local<v8::Object> obj)
{
    std::string strBuffer = dir_format(obj);
    asyncLog(_INFO, strBuffer);
    return 0;
}

static std::map<std::string, int64_t> s_timers;

result_t console_base::time(const char *label)
{
    s_timers[std::string(label)] = Ticks();

    return 0;
}

result_t console_base::timeEnd(const char *label)
{
    long t = (long) (Ticks() - s_timers[label]);

    s_timers.erase(label);

    std::string strBuffer;
    char numStr[64];

    sprintf(numStr, "%.3g", t / 1000.0);

    strBuffer.append(label);
    strBuffer.append(": ", 2);
    strBuffer.append(numStr);
    strBuffer.append("ms", 2);

    asyncLog(_INFO, strBuffer);
    return 0;
}

inline const char *ToCString(const v8::String::Utf8Value &value)
{
    return *value ? *value : "<string conversion failed>";
}

result_t console_base::trace(const char *label)
{
    std::string strBuffer;

    strBuffer.append("console.trace: ", 15);
    strBuffer.append(label);
    strBuffer.append(traceInfo());

    asyncLog(_WARN, strBuffer);
    return 0;
}

#ifdef assert
#undef assert
#endif

result_t console_base::assert(v8::Local<v8::Value> value, const char *msg)
{
    return assert_base::ok(value, msg);
}

result_t console_base::print(const char *fmt, const v8::FunctionCallbackInfo<v8::Value> &args)
{
    flushLog();
    logger::std_out(Format(fmt, args).c_str());

    return 0;
}

char *read_line()
{
    char *text = (char *)malloc(1024);

    if (fgets(text, 1024, stdin) != NULL)
    {
        int textLen = (int)qstrlen(text);
        if (textLen > 0 && text[textLen - 1] == '\n')
            text[textLen - 1] = '\0';     // getting rid of newline character
        return text;
    }

    free(text);
    return NULL;
}

result_t console_base::readLine(const char *msg, std::string &retVal,
                                exlib::AsyncEvent *ac)
{
#ifndef _WIN32
    static bool _init = false;
    static char *(*_readline)(const char *);
    static void (*_add_history)(char *);

    if (!_init)
    {
        _init = true;

#ifdef MacOS
        void *handle = dlopen("libreadline.dylib", RTLD_LAZY);
#else
        void *handle = dlopen("libreadline.so", RTLD_LAZY);
#endif

        if (handle)
        {
            _readline = (char *(*)(const char *))dlsym(handle, "readline");
            _add_history = (void (*)(char *))dlsym(handle, "add_history");
        }
    }
#endif

    if (!ac)
    {
        flushLog();
        return CALL_E_NOSYNC;
    }

#ifndef _WIN32
    if (_readline && _add_history)
    {
        char *line;

        if ((line = _readline(msg)) != NULL)
        {
            if (*line)
            {
                _add_history(line);
                retVal = line;
            }
            free(line);
        }
    }
    else
#endif
    {
        char *line;

        logger::std_out(msg);

        if ((line = read_line()) != NULL)
        {
            retVal = line;
            free(line);
        }
    }

    return 0;
}

}
;
