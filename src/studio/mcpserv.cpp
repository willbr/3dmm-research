/* Copyright (c) 3DMMForever contributors.
   Licensed under the MIT License. */

/***************************************************************************
    mcpserv.cpp: embedded MCP server. See mcpserv.h.

    Wire protocol: newline-delimited JSON-RPC 2.0 over stdin/stdout.

    Threading model:
    - One worker thread polls inherited stdin, parses line-by-line
      JSON-RPC, and pushes Request objects onto a queue protected by a mutex.
    - The main GUI thread calls mcp::Drain() once per kauai event-loop tick
      (from Application::TopOfLoop). Drain pops requests, dispatches synchronously
      on the GUI thread (so it is safe to touch SDL, Kauai and vpcex), and
      writes responses back to stdout.
    - Stdout writes are mutex-guarded so worker-safe tools and main-thread
      responses cannot interleave message bytes.
***************************************************************************/

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)

// IMPORTANT: pull in the C++ standard library and Windows headers BEFORE any
// kauai header. `kauai/src/util.h` defines `#define size(x) sizeof(x)` (a
// legacy 3DMM convenience macro) which clobbers `std::vector::size()`,
// `std::string::size()`, and friends if STL headers are processed under it.
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include "mcp_io.h"
#include "platform_files.h"
#include "launch_context.h"
#include "studio_preferences.h"
#ifdef KAUAI_SDL3
#include "studio_settings.h"
#include "studio_pack_runtime.h"
#include "studio_vxp_store.h"
#endif
#include "font_names.h"
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "sdl_dialog.h"
#include "sdl_sound.h"
#include "video_sdl.h"
#include "midi_sdl.h"
#include "sdl_recording.h"
#include "pixel_buffer.h"
#include "font_host.h"
#include "file_browser.h"
#endif

#ifdef _WIN32
#include <windows.h>
#ifndef KAUAI_SDL3
#include <objidl.h> // IStream
#include <gdiplus.h>
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif
#endif
#ifdef _MSC_VER
#include <rtcapi.h> // Optional MSVC runtime-check diagnostics
#endif
#endif // _WIN32

#include "studio.h"
#include "mcpserv.h"
#include "studio_iso.h"
#ifdef KAUAI_SDL3
#include "studio_pack_options.h"
#include "studio_v3dmm.h"
#endif

// kauai/src/util.h defines `size(x)` as a function-like macro. Once studio.h
// has been processed we no longer need it; undefine so our own code can call
// `std::string::size()` etc. without the preprocessor mangling them.
#ifdef size
#undef size
#endif

ASSERTNAME

namespace mcp
{

// ============================================================
// Mini JSON: just enough for MCP messages. No streaming, no
// numeric edge cases beyond int/double. Inputs from MCP hosts
// are well-formed; on parse failure we report an error.
// ============================================================

struct JVal;
using JArr = std::vector<JVal>;
using JObj = std::map<std::string, JVal>;

enum class JT
{
    Null,
    Bool,
    Int,
    Dbl,
    Str,
    Arr,
    Obj
};

struct JVal
{
    JT t = JT::Null;
    bool b = false;
    long long i = 0;
    double d = 0;
    std::string s;
    std::shared_ptr<JArr> a;
    std::shared_ptr<JObj> o;

    static JVal MkNull() { return {}; }
    static JVal MkBool(bool v) { JVal r; r.t = JT::Bool; r.b = v; return r; }
    static JVal MkInt(long long v) { JVal r; r.t = JT::Int; r.i = v; return r; }
    static JVal MkDbl(double v) { JVal r; r.t = JT::Dbl; r.d = v; return r; }
    static JVal MkStr(std::string v) { JVal r; r.t = JT::Str; r.s = std::move(v); return r; }
    static JVal MkArr() { JVal r; r.t = JT::Arr; r.a = std::make_shared<JArr>(); return r; }
    static JVal MkObj() { JVal r; r.t = JT::Obj; r.o = std::make_shared<JObj>(); return r; }

    bool IsObj() const { return t == JT::Obj && o; }
    bool IsArr() const { return t == JT::Arr && a; }
    bool IsStr() const { return t == JT::Str; }
    bool IsInt() const { return t == JT::Int; }
    bool IsBool() const { return t == JT::Bool; }
    bool IsNull() const { return t == JT::Null; }

    long long AsInt(long long def = 0) const
    {
        if (t == JT::Int) return i;
        if (t == JT::Dbl) return (long long)d;
        return def;
    }
    const std::string &AsStr() const { static const std::string e; return t == JT::Str ? s : e; }
    bool AsBool(bool def = false) const { return t == JT::Bool ? b : def; }

    const JVal *Get(const std::string &key) const
    {
        if (!IsObj()) return nullptr;
        auto it = o->find(key);
        return it == o->end() ? nullptr : &it->second;
    }
    JVal &Set(const std::string &key, JVal v)
    {
        if (!IsObj()) *this = MkObj();
        return (*o)[key] = std::move(v);
    }
};

class JParser
{
  public:
    JParser(const char *p, size_t n) : _p(p), _e(p + n) {}
    bool FParse(JVal *pv)
    {
        // Tolerate a leading UTF-8 BOM. Some hosts (.NET StreamWriter on
        // PowerShell, depending on console encoding) prepend EF BB BF to the
        // first stdin write; without this skip we'd reject their initialize.
        if (_e - _p >= 3 && (unsigned char)_p[0] == 0xEF && (unsigned char)_p[1] == 0xBB && (unsigned char)_p[2] == 0xBF)
            _p += 3;
        _SkipWs();
        if (!_FVal(pv)) return false;
        _SkipWs();
        return true;
    }

  private:
    const char *_p;
    const char *_e;

    void _SkipWs()
    {
        while (_p < _e && (*_p == ' ' || *_p == '\t' || *_p == '\n' || *_p == '\r')) _p++;
    }

    bool _FVal(JVal *pv)
    {
        _SkipWs();
        if (_p >= _e) return false;
        char c = *_p;
        if (c == '{') return _FObj(pv);
        if (c == '[') return _FArr(pv);
        if (c == '"') return _FStr(pv);
        if (c == 't' || c == 'f') return _FBool(pv);
        if (c == 'n') return _FNull(pv);
        return _FNum(pv);
    }

    bool _FObj(JVal *pv)
    {
        if (*_p != '{') return false;
        _p++;
        *pv = JVal::MkObj();
        _SkipWs();
        if (_p < _e && *_p == '}') { _p++; return true; }
        while (_p < _e)
        {
            _SkipWs();
            JVal key;
            if (!_FStr(&key)) return false;
            _SkipWs();
            if (_p >= _e || *_p != ':') return false;
            _p++;
            JVal val;
            if (!_FVal(&val)) return false;
            (*pv->o)[key.s] = std::move(val);
            _SkipWs();
            if (_p >= _e) return false;
            if (*_p == ',') { _p++; continue; }
            if (*_p == '}') { _p++; return true; }
            return false;
        }
        return false;
    }

    bool _FArr(JVal *pv)
    {
        if (*_p != '[') return false;
        _p++;
        *pv = JVal::MkArr();
        _SkipWs();
        if (_p < _e && *_p == ']') { _p++; return true; }
        while (_p < _e)
        {
            JVal v;
            if (!_FVal(&v)) return false;
            pv->a->push_back(std::move(v));
            _SkipWs();
            if (_p >= _e) return false;
            if (*_p == ',') { _p++; continue; }
            if (*_p == ']') { _p++; return true; }
            return false;
        }
        return false;
    }

    bool _FStr(JVal *pv)
    {
        if (*_p != '"') return false;
        _p++;
        std::string out;
        while (_p < _e && *_p != '"')
        {
            if (*_p == '\\' && _p + 1 < _e)
            {
                _p++;
                char c = *_p++;
                switch (c)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (_p + 4 > _e) return false;
                    unsigned u = 0;
                    for (int k = 0; k < 4; k++)
                    {
                        char h = _p[k];
                        u <<= 4;
                        if (h >= '0' && h <= '9') u |= (h - '0');
                        else if (h >= 'a' && h <= 'f') u |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') u |= (h - 'A' + 10);
                        else return false;
                    }
                    _p += 4;
                    // Naive UTF-8 emission; surrogates not handled (MCP rarely uses them)
                    if (u < 0x80) out += (char)u;
                    else if (u < 0x800)
                    {
                        out += (char)(0xc0 | (u >> 6));
                        out += (char)(0x80 | (u & 0x3f));
                    }
                    else
                    {
                        out += (char)(0xe0 | (u >> 12));
                        out += (char)(0x80 | ((u >> 6) & 0x3f));
                        out += (char)(0x80 | (u & 0x3f));
                    }
                    break;
                }
                default: return false;
                }
            }
            else
            {
                out += *_p++;
            }
        }
        if (_p >= _e) return false;
        _p++; // closing "
        *pv = JVal::MkStr(std::move(out));
        return true;
    }

    bool _FBool(JVal *pv)
    {
        if (_e - _p >= 4 && std::memcmp(_p, "true", 4) == 0) { _p += 4; *pv = JVal::MkBool(true); return true; }
        if (_e - _p >= 5 && std::memcmp(_p, "false", 5) == 0) { _p += 5; *pv = JVal::MkBool(false); return true; }
        return false;
    }

    bool _FNull(JVal *pv)
    {
        if (_e - _p >= 4 && std::memcmp(_p, "null", 4) == 0) { _p += 4; *pv = JVal::MkNull(); return true; }
        return false;
    }

    bool _FNum(JVal *pv)
    {
        const char *start = _p;
        if (_p < _e && (*_p == '-' || *_p == '+')) _p++;
        bool isDouble = false;
        while (_p < _e && (std::isdigit((unsigned char)*_p) || *_p == '.' || *_p == 'e' || *_p == 'E' || *_p == '+' || *_p == '-'))
        {
            if (*_p == '.' || *_p == 'e' || *_p == 'E') isDouble = true;
            _p++;
        }
        std::string ns(start, _p - start);
        if (ns.empty()) return false;
        if (isDouble)
        {
            *pv = {};
            pv->t = JT::Dbl;
            pv->d = std::strtod(ns.c_str(), nullptr);
        }
        else
        {
            *pv = JVal::MkInt(std::strtoll(ns.c_str(), nullptr, 10));
        }
        return true;
    }
};

static void JEscape(std::string &out, const std::string &s)
{
    for (char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if ((unsigned char)c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
}

static void JWrite(std::string &out, const JVal &v)
{
    switch (v.t)
    {
    case JT::Null: out += "null"; break;
    case JT::Bool: out += v.b ? "true" : "false"; break;
    case JT::Int: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", v.i);
        out += buf;
        break;
    }
    case JT::Dbl: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v.d);
        out += buf;
        break;
    }
    case JT::Str:
        out += '"';
        JEscape(out, v.s);
        out += '"';
        break;
    case JT::Arr: {
        out += '[';
        bool first = true;
        for (const auto &e : *v.a)
        {
            if (!first) out += ',';
            JWrite(out, e);
            first = false;
        }
        out += ']';
        break;
    }
    case JT::Obj: {
        out += '{';
        bool first = true;
        for (const auto &kv : *v.o)
        {
            if (!first) out += ',';
            out += '"';
            JEscape(out, kv.first);
            out += "\":";
            JWrite(out, kv.second);
            first = false;
        }
        out += '}';
        break;
    }
    }
}

// ============================================================
// Server state.
// ============================================================

struct Request
{
    JVal id;       // can be int/string/null
    std::string method;
    JVal params;   // object or absent
    bool hasId = false; // notifications have no id
};

static std::atomic<bool> _enabled{false};
static std::atomic<bool> _initialized{false};
static std::atomic<bool> _shouldExit{false};
static std::thread _readerThread;
static std::mutex _qMutex;
static std::deque<Request> _queue;
static std::mutex _writeMutex;
#ifdef _WIN32
#ifndef KAUAI_SDL3
static ULONG_PTR _gdiplusToken = 0;
#endif
static HWND _nativeMainWindow = nullptr; // Snapshot before starting the reader.
#endif
static bool _quitPosted = false;

// ============================================================
// I/O helpers
// ============================================================

static void WriteRaw(const char *p, size_t n)
{
    std::lock_guard<std::mutex> lk(_writeMutex);
    if (!WriteStdout(p, n)) _shouldExit.store(true);
}

static void WriteMessage(const JVal &msg)
{
    std::string s;
    JWrite(s, msg);
    s += '\n';
    WriteRaw(s.data(), s.size());
}

// Custom MSVC /RTC error handler. The platform default would just show a
// modal Debug Error dialog with file/line, but in practice the dialog often
// shows an empty File: field. Capture the structured error here so we can
// see exactly which `thd` (or `tag`, etc.) site fired. We log to
// AppendCrashLog (drains via the existing read_crash_log MCP tool) and then
// continue after logging, as this Debug server did before the SDL port.
#ifdef _MSC_VER
static int __cdecl MyRtcErrorFunc(int errType, const wchar_t *file, int line, const wchar_t *module, const wchar_t *format, ...)
{
    char body[2048];
    char filebuf[512] = "(no file)";
    char modbuf[256] = "";
    char fmtbuf[512] = "";
    if (file && *file) WideCharToMultiByte(CP_UTF8, 0, file, -1, filebuf, sizeof(filebuf), NULL, NULL);
    if (module && *module) WideCharToMultiByte(CP_UTF8, 0, module, -1, modbuf, sizeof(modbuf), NULL, NULL);
    if (format && *format) WideCharToMultiByte(CP_UTF8, 0, format, -1, fmtbuf, sizeof(fmtbuf), NULL, NULL);
    char msgbuf[1024] = "";
    if (format)
    {
        va_list ap;
        va_start(ap, format);
        // /RTC errors pass plain ASCII args (variable name etc.), so a narrow
        // vsnprintf with the narrow format works fine here.
        std::vsnprintf(msgbuf, sizeof(msgbuf), fmtbuf, ap);
        va_end(ap);
    }
    std::snprintf(body, sizeof(body), "errType=%d  module=%s  file=%s:%d\n%s\n", errType, modbuf, filebuf, line,
                  msgbuf[0] ? msgbuf : fmtbuf);
    AppendCrashLog("RTC", body);
    // Return 0 = continue execution silently. The warning is logged so we can
    // diagnose; downstream code may or may not crash depending on whether
    // this was a spurious /RTCu (value-init would suffice) or a real
    // read-before-write bug. If it crashes downstream, that's where the real
    // bug lives. Returning 1 invokes the debugger (breakpoint exception);
    // -1 takes default action (modal dialog), but the dialog's File: field
    // is empty in our environment so it's not more informative than this log.
    return 0;
}

#endif // _MSC_VER

static JVal MakeError(const JVal &id, int code, const std::string &message)
{
    JVal r = JVal::MkObj();
    r.Set("jsonrpc", JVal::MkStr("2.0"));
    r.Set("id", id);
    JVal err = JVal::MkObj();
    err.Set("code", JVal::MkInt(code));
    err.Set("message", JVal::MkStr(message));
    r.Set("error", err);
    return r;
}

static JVal MakeResult(const JVal &id, JVal result)
{
    JVal r = JVal::MkObj();
    r.Set("jsonrpc", JVal::MkStr("2.0"));
    r.Set("id", id);
    r.Set("result", std::move(result));
    return r;
}

// ============================================================
// Reader thread: bounded stdio polling with line splitting.
// ============================================================

// Forward decl; defined after kTools because it walks that table.
static bool TryDispatchWorkerDirect(const Request &req);

static void ReaderThreadProc()
{
    std::string buf;
    char chunk[4096];
    while (!_shouldExit.load())
    {
        std::size_t got = 0;
        const auto status = ReadStdin(chunk, sizeof(chunk), got);
        if (status == ReadStatus::timeout) continue;
        if (status == ReadStatus::end)
        {
            // EOF / pipe closed: tell main thread to start shutting down.
            _shouldExit.store(true);
            break;
        }
        buf.append(chunk, chunk + got);
        for (;;)
        {
            auto nl = buf.find('\n');
            if (nl == std::string::npos) break;
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            JParser p(line.data(), line.size());
            JVal msg;
            if (!p.FParse(&msg) || !msg.IsObj())
            {
                WriteMessage(MakeError(JVal::MkNull(), -32700, "parse error"));
                continue;
            }
            Request req;
            if (auto pid = msg.Get("id"))
            {
                req.id = *pid;
                req.hasId = true;
            }
            else
            {
                req.id = JVal::MkNull();
            }
            if (auto pm = msg.Get("method")) req.method = pm->AsStr();
            if (auto pp = msg.Get("params")) req.params = *pp;

            // Worker-direct fast path: tools marked worker_safe (currently
            // list_dialogs and dismiss_dialog) only touch Win32 APIs that
            // are safe across threads, so we dispatch them inline from the
            // reader thread. This is what lets us see and dismiss a modal
            // dialog (MSVC /RTCu Debug Error, kauai assert) while the main
            // thread is blocked inside it -- Drain() can't run during a
            // modal pump.
            if (TryDispatchWorkerDirect(req))
            {
                continue;
            }
            std::lock_guard<std::mutex> lk(_qMutex);
            _queue.push_back(std::move(req));
        }
    }
}

// ============================================================
// Tools.
// ============================================================

#ifdef _WIN32
using McpWindow = HWND;
#else
using McpWindow = void *;
#endif
static McpWindow _GetMainWindow();
static std::uint64_t TickCount()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
static JVal Tool_Initialize(const JVal &params);
static JVal Tool_ListTools();
static JVal Tool_CallTool(const JVal &params);

// Pump host events AND drain Kauai's command queue for `ms`
// milliseconds. Used by `wait_ms`, `wait_for_gob`, and after click/key
// tools so the GUI has a chance to settle before the next request runs.
//
// Pumping Win32 messages alone isn't enough: kauai-level commands
// (vpcex->EnqueueCid, e.g. cidBrowserReady that a click script enqueues)
// only execute via CommandExecutionManager::FDispatchNextCmd, which kauai
// calls from its main loop -- not from DispatchMessage. While we're
// running inside Drain (called from TopOfLoop), kauai's main loop is
// suspended, so click->EnqueueCid->browser-opens never advances unless
// we drain commands here too.
static void PumpForMs(std::uint32_t ms)
{
    const auto start = TickCount();
    for (;;)
    {
        const auto elapsed = TickCount() - start;
        if (elapsed >= ms) return;
#ifdef KAUAI_SDL3
        if (KauaiSDL::Main().Active())
        {
            if (vpappb && (!vpcex || !vpcex->PgobTracking())) vpappb->PumpHostEvent();
            if (vpcex)
                for (int i = 0; i < 8 && vpcex->FDispatchNextCmd(); ++i) {}
            if (vpappb) vpappb->UpdateMarked();
            GraphicsPort::Flush();
            KauaiSDL::Main().Present();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
#endif
#ifdef _WIN32
        const auto remaining = ms - elapsed;
        MsgWaitForMultipleObjects(0, NULL, FALSE, remaining > 16 ? 16 : remaining, QS_ALLINPUT);
        MSG m;
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        // Drain pending kauai commands. Cap at 8 per pump cycle so a
        // recursive enqueue storm can't starve the timeout. FDispatchNextCmd
        // returns false when the queue is empty.
        if (vpcex)
        {
            for (int i = 0; i < 8; i++)
                if (!vpcex->FDispatchNextCmd()) break;
        }
    }
}

static McpWindow _GetMainWindow()
{
#ifdef KAUAI_SDL3
    return static_cast<McpWindow>(KauaiSDL::Main().WindowToken());
#elif defined(WIN)
    return vwig.hwndApp;
#else
    return nullptr;
#endif
}

#if defined(_WIN32) && !defined(KAUAI_SDL3)
static bool _GetClipboardEncoderClsid(const wchar_t *mime, CLSID *pClsid)
{
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<unsigned char> buf(size);
    auto codecs = (Gdiplus::ImageCodecInfo *)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; i++)
    {
        if (wcscmp(codecs[i].MimeType, mime) == 0)
        {
            *pClsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

#endif

static const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string Base64Encode(const unsigned char *data, size_t n)
{
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    for (size_t i = 0; i < n; i += 3)
    {
        unsigned long v = (unsigned long)data[i] << 16;
        if (i + 1 < n) v |= (unsigned long)data[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned long)data[i + 2];
        out += kBase64Alphabet[(v >> 18) & 0x3f];
        out += kBase64Alphabet[(v >> 12) & 0x3f];
        out += (i + 1 < n) ? kBase64Alphabet[(v >> 6) & 0x3f] : '=';
        out += (i + 2 < n) ? kBase64Alphabet[v & 0x3f] : '=';
    }
    return out;
}

// Capture main window client area into an in-memory PNG, return base64.
// Returns empty string on failure.
static std::string CaptureWindowPngBase64(McpWindow hwnd)
{
    if (!hwnd) return {};
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(hwnd))
    {
        GraphicsPort::Flush();
        std::vector<std::uint32_t> pixels;
        int width, height;
        if (!KauaiSDL::Main().CopyPixels(pixels, width, height)) return {};
        const auto png = EncodePng(pixels, width, height);
        return Base64Encode(png.data(), png.size());
    }
#endif
#if defined(_WIN32) && !defined(KAUAI_SDL3)
    RECT rcClient;
    if (!GetClientRect(hwnd, &rcClient)) return {};
    int w = rcClient.right - rcClient.left;
    int h = rcClient.bottom - rcClient.top;
    if (w <= 0 || h <= 0) return {};

    HDC hdcWin = GetDC(hwnd);
    if (!hdcWin) return {};
    HDC hdcMem = CreateCompatibleDC(hdcWin);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWin, w, h);
    HGDIOBJ oldbm = SelectObject(hdcMem, hbm);

    // PrintWindow with PW_CLIENTONLY most reliably grabs kauai/GDI windows
    // even when occluded; falls back to BitBlt if it fails.
    BOOL ok = PrintWindow(hwnd, hdcMem, PW_CLIENTONLY);
    if (!ok)
    {
        BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
    }

    std::string b64;
    {
        Gdiplus::Bitmap bmp(hbm, NULL);
        IStream *pStream = NULL;
        if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK)
        {
            CLSID clsidPng;
            if (_GetClipboardEncoderClsid(L"image/png", &clsidPng))
            {
                if (bmp.Save(pStream, &clsidPng, NULL) == Gdiplus::Ok)
                {
                    HGLOBAL hg = NULL;
                    GetHGlobalFromStream(pStream, &hg);
                    SIZE_T sz = GlobalSize(hg);
                    void *pData = GlobalLock(hg);
                    if (pData && sz > 0) b64 = Base64Encode((unsigned char *)pData, sz);
                    if (pData) GlobalUnlock(hg);
                }
            }
            pStream->Release();
        }
    }

    SelectObject(hdcMem, oldbm);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWin);
    return b64;
#else
    return {};
#endif
}

// Build an MCP `content` array from a single text item.
static JVal ContentText(const std::string &text)
{
    JVal arr = JVal::MkArr();
    JVal item = JVal::MkObj();
    item.Set("type", JVal::MkStr("text"));
    item.Set("text", JVal::MkStr(text));
    arr.a->push_back(std::move(item));
    JVal res = JVal::MkObj();
    res.Set("content", std::move(arr));
    return res;
}

static JVal ContentImage(const std::string &b64png)
{
    JVal arr = JVal::MkArr();
    JVal item = JVal::MkObj();
    item.Set("type", JVal::MkStr("image"));
    item.Set("data", JVal::MkStr(b64png));
    item.Set("mimeType", JVal::MkStr("image/png"));
    arr.a->push_back(std::move(item));
    JVal res = JVal::MkObj();
    res.Set("content", std::move(arr));
    return res;
}

static JVal ContentError(const std::string &text)
{
    JVal r = ContentText(text);
    r.Set("isError", JVal::MkBool(true));
    return r;
}

// --- screenshot -----------------------------------------------------------

static JVal Tool_Screenshot(const JVal &args)
{
#ifdef KAUAI_SDL3
    if (args.Get("presentation") && args.Get("presentation")->AsBool())
    {
        std::vector<std::uint32_t> pixels;
        int width, height;
        if (!KauaiSDL::Main().CopyPresentationPixels(pixels, width, height))
            return ContentError("presentation pixels unavailable");
        const auto png = EncodePng(pixels, width, height);
        return ContentImage(Base64Encode(png.data(), png.size()));
    }
#endif
    const auto hwnd = _GetMainWindow();
    if (!hwnd) return ContentError("main window not available");
    std::string b64 = CaptureWindowPngBase64(hwnd);
    if (b64.empty()) return ContentError("screenshot capture failed");
    return ContentImage(b64);
}

// --- click ----------------------------------------------------------------

static JVal Tool_Click(const JVal &args)
{
    const auto hwnd = _GetMainWindow();
    if (!hwnd) return ContentError("main window not available");
    long x = (long)(args.Get("x") ? args.Get("x")->AsInt(-1) : -1);
    long y = (long)(args.Get("y") ? args.Get("y")->AsInt(-1) : -1);
    if (x < 0 || y < 0) return ContentError("missing or negative x/y");
    std::string button = args.Get("button") ? args.Get("button")->AsStr() : "left";
    bool isRight = (button == "right");
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(hwnd))
    {
        const unsigned which = isRight ? 3 : 1;
        const auto shift = args.Get("shift");
        const bool shifted = shift && shift->AsBool();
        if ((shifted && !KauaiSDL::Main().PushKey(16, true, KauaiSDL::shift)) ||
            !KauaiSDL::Main().PushPointer(static_cast<float>(x), static_cast<float>(y), true, which) ||
            !KauaiSDL::Main().PushPointer(static_cast<float>(x), static_cast<float>(y), false, which))
            return ContentError(KauaiSDL::Main().Error());
        if (shifted)
        {
            // Let the click's script dispatch its command while Shift is held.
            // Releasing in the same batch could turn Shift+Play into Play.
            PumpForMs(50);
            if (!KauaiSDL::Main().PushKey(16, false))
                return ContentError(KauaiSDL::Main().Error());
        }
        return ContentText("Queued click through SDL input.");
    }
#endif

#ifdef _WIN32
    POINT p = {static_cast<LONG>(x), static_cast<LONG>(y)};
    ClientToScreen(hwnd, &p);

    // Force the window foreground so SendInput is delivered to it. Win32
    // blocks foreground-stealing when our process didn't generate the last
    // user input, even for our own hwnd. The standard workaround is to
    // attach our thread's input state to the foreground thread's, then
    // SetForegroundWindow proceeds, then detach.
    HWND hwndFG = GetForegroundWindow();
    DWORD tidUs = GetCurrentThreadId();
    DWORD tidFG = hwndFG ? GetWindowThreadProcessId(hwndFG, NULL) : 0;
    bool attached = false;
    if (tidFG != 0 && tidFG != tidUs)
        attached = AttachThreadInput(tidFG, tidUs, TRUE) ? true : false;
    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    if (attached) AttachThreadInput(tidFG, tidUs, FALSE);
    PumpForMs(80);

    int cxScreen = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int cyScreen = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int xVirt = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int yVirt = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (cxScreen <= 0) cxScreen = 1;
    if (cyScreen <= 0) cyScreen = 1;
    LONG nx = (LONG)((((LONGLONG)(p.x - xVirt)) * 65535) / cxScreen);
    LONG ny = (LONG)((((LONGLONG)(p.y - yVirt)) * 65535) / cyScreen);

    // Use SendInput so the OS updates VK_LBUTTON state -- kauai's
    // GrfcustCur() reads GetKeyState(VK_LBUTTON) to set fcustMouse, and
    // GraphicsObject::MouseDown asserts that flag. PostMessage(WM_LBUTTONDOWN)
    // alone bypasses the OS state and trips the assertion.
    //
    // Move, down, and up are sent in three separate batches with message-pump
    // cycles between them so kidspace's mouse-state machine has time to
    // transition through hover -> press -> click. Sending them as one batch
    // produced only the hover tooltip (down/up never registered as a click).
    //
    // Exception: if a kauai mouse-tracker is active (e.g. MovieView is in
    // place-actor mode after StartPlaceActor), do NOT pump messages here.
    // ApplicationBase::TrackMouse (appbwin.cpp:204) consumes mouse messages
    // out of the queue without dispatching them, converting WM_LBUTTONDOWN/UP
    // into a polled cidTrackMouse with fcustMouse set. PumpForMs would
    // dispatch the WM_LBUTTONDOWN through WndProc instead, enqueueing a
    // cidMouseDown to the tracker gob -- which trips the
    // "mouse already being tracked!" assert at movie.cpp:6805 the moment
    // FDispatchNextCmd pops it. Skip the pump so the messages sit in the
    // queue until our handler returns and kauai's main loop polls TrackMouse.
    bool tracker_active = (vpcex && vpcex->PgobTracking() != pvNil);
    auto pump = [&](DWORD ms) {
        if (tracker_active)
            Sleep(ms);
        else
            PumpForMs(ms);
    };

    INPUT mv = {};
    mv.type = INPUT_MOUSE;
    mv.mi.dx = nx;
    mv.mi.dy = ny;
    mv.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &mv, sizeof(INPUT));
    pump(40);

    INPUT dn = {};
    dn.type = INPUT_MOUSE;
    dn.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &dn, sizeof(INPUT));
    pump(60);

    INPUT up = {};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    SendInput(1, &up, sizeof(INPUT));
    pump(100);

    char msg[160];
    std::snprintf(msg, sizeof(msg), "clicked %s at client (%ld,%ld) screen (%ld,%ld)",
                  isRight ? "right" : "left", x, y, p.x, p.y);
    return ContentText(msg);
#else
    return ContentError("SDL window not available");
#endif
}

// --- key -----------------------------------------------------------------

static JVal Tool_Key(const JVal &args)
{
    const auto hwnd = _GetMainWindow();
    if (!hwnd) return ContentError("main window not available");
    int vk = (int)(args.Get("vk") ? args.Get("vk")->AsInt(0) : 0);
    if (vk == 0) return ContentError("missing vk");
#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(hwnd))
    {
        if (!KauaiSDL::Main().PushKey(vk, true) || !KauaiSDL::Main().PushKey(vk, false))
            return ContentError(KauaiSDL::Main().Error());
        return ContentText("Queued key through SDL input.");
    }
#endif

#ifdef _WIN32
    SetForegroundWindow(hwnd);
    PumpForMs(20);

    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = (WORD)vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = (WORD)vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    PumpForMs(50);

    char msg[64];
    std::snprintf(msg, sizeof(msg), "sent vk=0x%02x", vk);
    return ContentText(msg);
#else
    return ContentError("SDL window not available");
#endif
}

// --- send_command --------------------------------------------------------
#ifdef KAUAI_SDL3
static JVal Tool_TypeText(const JVal &args)
{
    const auto text = args.Get("text");
    if (!text || !KauaiSDL::Main().PushText(text->AsStr())) return ContentError(KauaiSDL::Main().Error());
    return ContentText("Queued UTF-8 text through SDL input.");
}
#endif

static JVal Tool_SendCommand(const JVal &args)
{
    long cid = (long)(args.Get("cid") ? args.Get("cid")->AsInt(0) : 0);
    if (cid == 0) return ContentError("missing cid");
    if (!vpcex) return ContentError("command execution manager not available");
    long lw0 = (long)(args.Get("lw0") ? args.Get("lw0")->AsInt(0) : 0);
    long lw1 = (long)(args.Get("lw1") ? args.Get("lw1")->AsInt(0) : 0);
    long lw2 = (long)(args.Get("lw2") ? args.Get("lw2")->AsInt(0) : 0);
    long lw3 = (long)(args.Get("lw3") ? args.Get("lw3")->AsInt(0) : 0);
    // Optional target gob hid: kidspace commands route by gob hid, and many
    // cids (cidBrowserReady, cidSceneSortInit, ...) route to khidStudio etc.
    PCommandHandler pcmh = pvNil;
    long hid = (long)(args.Get("hid") ? args.Get("hid")->AsInt(0) : 0);
    if (hid != 0)
    {
        PGraphicsObject pgob = GraphicsObject::PgobFromHidScr(hid);
        if (pgob != pvNil) pcmh = pgob;
    }
    vpcex->EnqueueCid(cid, pcmh, pvNil, lw0, lw1, lw2, lw3);
    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "enqueued cid=0x%lx hid=0x%lx (handler=%s) lw0=0x%lx lw1=0x%lx lw2=0x%lx lw3=0x%lx",
                  (unsigned long)cid, (unsigned long)hid,
                  pcmh ? "found" : "nil",
                  (unsigned long)lw0, (unsigned long)lw1,
                  (unsigned long)lw2, (unsigned long)lw3);
    return ContentText(msg);
}

// --- find_gob ------------------------------------------------------------
// Given a kidspace gob hid, report its on-screen rect (client coords of the
// containing HWND) so the agent can target a click at its center without
// guessing pixel positions from a screenshot.

// Build the find_gob result object (no MCP wrapping). Used directly by
// Tool_FindGob and the wait/click tools so they can poll without going
// through JSON twice.
static JVal _GobInfo(long hid)
{
    JVal o = JVal::MkObj();
    o.Set("hid", JVal::MkInt(hid));
    PGraphicsObject pgob = GraphicsObject::PgobFromHidScr(hid);
    if (!pgob)
    {
        o.Set("found", JVal::MkBool(false));
        return o;
    }
    o.Set("found", JVal::MkBool(true));
    // Bounding rect in hwnd-client coords -- this is what kauai's WndProc
    // sees in WM_LBUTTONDOWN's lParam, so SendInput at this position lands
    // exactly on the gob.
    RC rc;
    pgob->GetRc(&rc, cooHwnd);
    long w = rc.xpRight - rc.xpLeft;
    long h = rc.ypBottom - rc.ypTop;
    o.Set("x", JVal::MkInt(rc.xpLeft));
    o.Set("y", JVal::MkInt(rc.ypTop));
    o.Set("w", JVal::MkInt(w));
    o.Set("h", JVal::MkInt(h));
    o.Set("center_x", JVal::MkInt(rc.xpLeft + w / 2));
    o.Set("center_y", JVal::MkInt(rc.ypTop + h / 2));
    // Visible rect: zero-area means the gob is hidden / not yet laid out.
    // Browsers create their child frames at startup but only position them
    // when the browser opens, so a non-empty rcVis is the "actually on
    // screen and clickable" signal.
    RC rcVis;
    pgob->GetRcVis(&rcVis, cooHwnd);
    long wv = rcVis.xpRight - rcVis.xpLeft;
    long hv = rcVis.ypBottom - rcVis.ypTop;
    o.Set("vis_w", JVal::MkInt(wv));
    o.Set("vis_h", JVal::MkInt(hv));
    o.Set("visible", JVal::MkBool(wv > 0 && hv > 0));
    return o;
}

static JVal _WrapInfoAsContent(const JVal &info)
{
    JVal arr = JVal::MkArr();
    JVal item = JVal::MkObj();
    item.Set("type", JVal::MkStr("text"));
    std::string s;
    JWrite(s, info);
    item.Set("text", JVal::MkStr(s));
    arr.a->push_back(std::move(item));
    JVal res = JVal::MkObj();
    res.Set("content", std::move(arr));
    return res;
}

#ifdef KAUAI_SDL3
#include "mcp_authoring.h"
#endif

static JVal Tool_FindGob(const JVal &args)
{
    long hid = (long)(args.Get("hid") ? args.Get("hid")->AsInt(0) : 0);
    if (hid == 0) return ContentError("missing hid");
    // "Not found" is an expected query result, not an MCP-level error -- the
    // info object already carries {found: false}. Returning ContentError
    // forces every caller to wrap the call in try/except just to discover a
    // gob is missing; the wait_for_gob and click_gob script paths in
    // particular were treating an empty error result as a JSON parse failure.
    return _WrapInfoAsContent(_GobInfo(hid));
}

// Poll find_gob until the gob exists and meets the requested readiness
// signal, or until timeout. Modes:
//   default ("visible"): wait for non-empty rcVis -- the gob is actually
//     painted on screen and clickable. Use for top-level windows that fade
//     in or popups that appear from off-screen.
//   "exists": wait for the gob to be created (found:true). Some kidspace
//     children are created with valid rcCur but the GetRcVis pipeline only
//     resolves on the next paint -- waiting on rcVis would time out even
//     though the gob is positioned and ready. Browser frames behave this
//     way: they're MoveAbsGob'd into place immediately but rcVis lags. For
//     those, poll the parent for visible, then use mode=exists on the
//     child to confirm it was created.
//   "positioned": wait for found:true AND rcCur not at (0,0,0,0). Catches
//     the "created but not yet placed" interim state that mode=exists
//     would accept.
// Pumps Windows messages between probes so the GUI thread can finish work.
static JVal Tool_WaitForGob(const JVal &args)
{
    long hid = (long)(args.Get("hid") ? args.Get("hid")->AsInt(0) : 0);
    long timeoutMs = (long)(args.Get("timeout_ms") ? args.Get("timeout_ms")->AsInt(5000) : 5000);
    std::string mode = args.Get("mode") ? args.Get("mode")->AsStr() : "visible";
    if (hid == 0) return ContentError("missing hid");
    if (timeoutMs <= 0) timeoutMs = 5000;
    if (timeoutMs > 30000) timeoutMs = 30000;
    const auto start = TickCount();
    while (true)
    {
        JVal info = _GobInfo(hid);
        bool found = info.Get("found")->AsBool(false);
        bool ready = false;
        if (found)
        {
            if (mode == "exists")
            {
                ready = true;
            }
            else if (mode == "positioned")
            {
                long w = info.Get("w") ? info.Get("w")->AsInt(0) : 0;
                long h = info.Get("h") ? info.Get("h")->AsInt(0) : 0;
                ready = (w > 0 && h > 0);
            }
            else
            {
                // "visible" (default)
                ready = info.Get("visible") && info.Get("visible")->AsBool(false);
            }
        }
        if (ready)
        {
            info.Set("waited_ms", JVal::MkInt((long)(TickCount() - start)));
            return _WrapInfoAsContent(info);
        }
        const auto elapsed = TickCount() - start;
        if (elapsed >= static_cast<std::uint32_t>(timeoutMs))
        {
            info.Set("timed_out", JVal::MkBool(true));
            info.Set("waited_ms", JVal::MkInt((long)elapsed));
            return _WrapInfoAsContent(info);
        }
        // 50ms poll period -- fast enough to catch typical layout settling
        // (~100-300ms after a click) without spinning the CPU.
        PumpForMs(50);
    }
}

// --- get_state -----------------------------------------------------------

static JVal Tool_TestMoviePlayback(const JVal &args)
{
    const auto enabled = std::getenv("THREEDMM_BATCH_TEST");
    if (!enabled || std::strcmp(enabled, "1"))
        return ContentError("Requires THREEDMM_BATCH_TEST=1 in an isolated test process");
    const auto studio = vpapp->Pstdio();
    const auto movie = studio ? studio->Pmvie() : nullptr;
    if (!movie)
        return ContentError("No movie is open");
    const auto actionValue = args.Get("action");
    const auto action = actionValue ? actionValue->AsStr() : "step";
    if (action == "start")
    {
        if (!movie->FBeginPlaybackTest() && !movie->PlaybackTestProgress().failed)
            return ContentError("Movie is already playing");
    }
    else if (action == "stop")
        movie->StopPlaybackTest();
    else if (action == "step")
    {
        const auto framesValue = args.Get("max_frames"), timeValue = args.Get("max_ms");
        const auto frames = framesValue ? framesValue->AsInt() : 256;
        const auto time = timeValue ? timeValue->AsInt() : 200;
        if (frames < 1 || frames > 10000 || time < 1 || time > 1000)
            return ContentError("max_frames must be 1..10000 and max_ms 1..1000");
        movie->StepPlaybackTest(static_cast<LegacyLong>(frames), static_cast<LegacyUlong>(time));
    }
    else
        return ContentError("action must be start, step or stop");
    const auto &test = movie->PlaybackTestProgress();
    JVal result = JVal::MkObj();
    result.Set("active", JVal::MkBool(test.active));
    result.Set("complete", JVal::MkBool(test.complete));
    result.Set("failed", JVal::MkBool(test.failed));
    result.Set("error", JVal::MkStr(test.error));
    result.Set("frames", JVal::MkInt(static_cast<long long>(test.frames)));
    result.Set("expected_frames", JVal::MkInt(static_cast<long long>(test.expectedFrames)));
    result.Set("scroll_steps", JVal::MkInt(static_cast<long long>(test.scrollSteps)));
    result.Set("steps", JVal::MkInt(static_cast<long long>(test.steps)));
    result.Set("scenes", JVal::MkInt(test.scenes));
    result.Set("scene_count", JVal::MkInt(movie->Cscen()));
    result.Set("scene", JVal::MkInt(test.scene));
    result.Set("frame", JVal::MkInt(test.frame));
    result.Set("first_frame", JVal::MkInt(test.first));
    result.Set("last_frame", JVal::MkInt(test.last));
    result.Set("elapsed_ms", JVal::MkInt(test.elapsed));
    result.Set("failure_command", JVal::MkInt(test.failureCommand));
    result.Set("skipped_click_pauses", JVal::MkInt(movie->TestSkippedClickPauses()));
    result.Set("skipped_timed_pauses", JVal::MkInt(movie->TestSkippedTimedPauses()));
    result.Set("skipped_sound_pauses", JVal::MkInt(movie->TestSkippedSoundPauses()));
#ifdef DEBUG
    result.Set("assertions_enabled", JVal::MkBool(true));
#else
    result.Set("assertions_enabled", JVal::MkBool(false));
#endif
    return _WrapInfoAsContent(result);
}

static std::uint64_t HashMovieBytes(std::uint64_t hash, const void *data, std::size_t count)
{
    const auto bytes = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < count; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

// Explicit opt-in keeps ordinary state polling cheap for large user movies.
static JVal MovieDetails(PScene scene)
{
    JVal actors = JVal::MkArr();
    auto roll = scene ? scene->PglRollCall() : nullptr; // Borrowed, read-only.
    for (long i = 0; roll && i < roll->IvMac(); ++i)
    {
        PActor actor;
        roll->Get(i, &actor);
        auto body = actor->Pbody();
        auto tmpl = actor->Ptmpl();
        JVal item = JVal::MkObj(), textures = JVal::MkArr();
        item.Set("id", JVal::MkInt(actor->Arid()));
        item.Set("kind", JVal::MkStr(tmpl->FIsTdt() ? "3d_text" : tmpl->FIsProp() ? "prop" : "actor"));
        item.Set("on_stage", JVal::MkBool(actor->FOnStage()));
        item.Set("action", JVal::MkInt(actor->AnidCur()));
        item.Set("cel", JVal::MkInt(actor->CelnCur()));
        const auto &evaluated = actor->EvaluatedFrameState();
        item.Set("frame", JVal::MkInt(evaluated.frame));
        item.Set("next_event", JVal::MkInt(evaluated.nextEvent));
        JVal position = JVal::MkArr();
        position.a->push_back(JVal::MkDbl(BrScalarToFloat(evaluated.position.dxr)));
        position.a->push_back(JVal::MkDbl(BrScalarToFloat(evaluated.position.dyr)));
        position.a->push_back(JVal::MkDbl(BrScalarToFloat(evaluated.position.dzr)));
        item.Set("position", std::move(position));
        TAG templateTag;
        actor->GetTagTmpl(&templateTag);
        item.Set("template_sid", JVal::MkInt(templateTag.sid));
        item.Set("template_cno", JVal::MkInt(templateTag.cno));
        String name;
        actor->GetName(&name);
        std::string utf8;
        KauaiFonts::NameToUtf8({name.Prgch(), static_cast<std::size_t>(name.Cch()),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               utf8);
        item.Set("name", JVal::MkStr(utf8));
        std::uint64_t pose = UINT64_C(14695981039346656037), appearance = pose;
        const int parts = body ? body->Cbact() - 2 : 0;
        item.Set("parts", JVal::MkInt(parts));
        for (int part = 0; part < parts; ++part)
        {
            auto bact = body->PbactPart(part);
            pose = HashMovieBytes(pose, &bact->t.t.mat, sizeof(bact->t.t.mat));
            if (bact->model)
            {
                auto number = Model::PmodlFromBmdl(bact->model)->Cno();
                appearance = HashMovieBytes(appearance, &number, sizeof(number));
            }
            if (bact->material)
            {
                auto material = bact->material;
                appearance = HashMovieBytes(appearance, &material->colour, sizeof(material->colour));
                appearance = HashMovieBytes(appearance, &material->index_base, sizeof(material->index_base));
                auto texture = material->colour_map;
                if (texture && texture->pixels && texture->type == BR_PMT_INDEX_8)
                {
                    std::uint64_t hash = UINT64_C(14695981039346656037);
                    for (int row = 0; row < texture->height; ++row)
                        hash =
                            HashMovieBytes(hash, static_cast<const byte *>(texture->pixels) + row * texture->row_bytes,
                                           texture->width);
                    textures.a->push_back(JVal::MkStr(std::to_string(hash)));
                    appearance = HashMovieBytes(appearance, &hash, sizeof(hash));
                }
            }
        }
        item.Set("pose_hash", JVal::MkStr(std::to_string(pose)));
        item.Set("appearance_hash", JVal::MkStr(std::to_string(appearance)));
        item.Set("texture_hashes", std::move(textures));
        if (body)
            item.Set("position_hash", JVal::MkStr(std::to_string(HashMovieBytes(UINT64_C(14695981039346656037),
                                                                                &body->PbactRoot()->t.t.mat,
                                                                                sizeof(body->PbactRoot()->t.t.mat)))));
        actors.a->push_back(std::move(item));
    }
    return actors;
}

static JVal Tool_GetState(const JVal &args)
{
    const auto hwnd = _GetMainWindow();
    JVal o = JVal::MkObj();
    o.Set("alive", JVal::MkBool(true));
    o.Set("application_foreground", JVal::MkBool(vpappb && vpappb->FForeground()));
#ifdef KAUAI_SDL3
    o.Set("settings_file", JVal::MkStr(StudioSettings::IniPath()));
#endif
    {
        const auto &preferences = CurrentStudioPreferences();
        JVal settings = JVal::MkObj();
        settings.Set("allow_multiple_instances", JVal::MkBool(preferences.allowMultipleInstances));
        settings.Set("background_playback", JVal::MkBool(preferences.backgroundPlayback));
        settings.Set("flat_shading", JVal::MkBool(World::FlatShading()));
        settings.Set("skip_startup_screens", JVal::MkBool(preferences.skipStartupScreens));
        settings.Set("mute_startup_sound", JVal::MkBool(preferences.muteStartupSound));
        settings.Set("mute_studio_sounds", JVal::MkBool(preferences.muteStudioSounds));
        settings.Set("cut_transitions", JVal::MkBool(preferences.cutTransitions));
        settings.Set("normal_key_repeat", JVal::MkBool(preferences.normalKeyRepeat));
        settings.Set("disable_audio_fades", JVal::MkBool(preferences.disableAudioFades));
        o.Set("preferences", std::move(settings));
        JVal timing = JVal::MkObj();
        timing.Set("rate_choice", JVal::MkInt(preferences.playbackRate));
        timing.Set("modifier_choice", JVal::MkInt(preferences.playbackModifier));
        timing.Set("target_fps", JVal::MkDbl(preferences.PlaybackFps()));
        o.Set("playback_timing", std::move(timing));
    }
#ifdef KAUAI_SDL3
    o.Set("font_provider", JVal::MkStr(KauaiFonts::StudioFontProvider()));
#if defined(KAUAI_PORTABLE_RUNTIME) || !defined(WIN)
    o.Set("portable_runtime", JVal::MkBool(true));
#endif
#ifdef WIN
    o.Set("native_screen_context", JVal::MkBool(vwig.hdcApp != nullptr));
#else
    o.Set("native_screen_context", JVal::MkBool(false));
#endif
    const auto midi = KauaiMidi::StudioMidiStats();
    const auto video = KauaiVideo::SDLVideoStats();
    o.Set("sdl_video_opened", JVal::MkInt(static_cast<long long>(video.opened)));
    const auto &iso = StudioContent::CurrentISO();
    o.Set("iso_content", JVal::MkBool(iso.used));
    o.Set("iso_cache_reused", JVal::MkBool(iso.reused));
    o.Set("iso_source", JVal::MkStr(iso.image));
    o.Set("iso_cache_root", JVal::MkStr(iso.root));
    JVal packs = JVal::MkArr();
    const auto &activePacks = StudioPacks::ActivePreferences();
    for (std::size_t i = 0; i < StudioPacks::Definitions().size(); ++i)
    {
        const auto &definition = StudioPacks::Definitions()[i];
        std::string directory;
        auto access = StudioPacks::Resolve(definition.sourceTitle, definition.sourceFolder, directory);
        JVal pack = JVal::MkObj();
        pack.Set("key", JVal::MkStr(definition.key));
        pack.Set("enabled", JVal::MkBool(activePacks[i].enabled));
        pack.Set("managed", JVal::MkBool(!activePacks[i].generation.empty()));
        pack.Set("available", JVal::MkBool(access == StudioPacks::Access::Available));
        pack.Set("path", JVal::MkStr(directory));
        packs.a->push_back(std::move(pack));
    }
    o.Set("expansions", std::move(packs));
    JVal vxps = JVal::MkArr();
    for (const auto &entry : StudioV3dmm::Packages().Entries())
    {
        JVal package = JVal::MkObj();
        package.Set("name", JVal::MkStr(StudioV3dmm::Display(entry.name)));
        package.Set("sha256", JVal::MkStr(entry.hash));
        package.Set("enabled", JVal::MkBool(entry.enabled));
        vxps.a->push_back(std::move(package));
    }
    o.Set("v3dmm_packages", std::move(vxps));
    JVal thumbnails = JVal::MkArr();
    ThumbnailFileEnumerator files;
    Filename thumbnail;
    long thumbnailSource;
    if (files.FInit())
        while (files.FNext(&thumbnail, &thumbnailSource))
        {
            String path;
            thumbnail.GetStnPath(&path);
            JVal item = JVal::MkObj();
            item.Set("source_id", JVal::MkInt(thumbnailSource));
            item.Set("path", JVal::MkStr(StudioPacks::UTF8(&path)));
            thumbnails.a->push_back(std::move(item));
        }
    o.Set("thumbnail_files", std::move(thumbnails));
    o.Set("sdl_video_started", JVal::MkInt(static_cast<long long>(video.started)));
    o.Set("sdl_video_completed", JVal::MkInt(static_cast<long long>(video.completed)));
    o.Set("sdl_video_decoded", JVal::MkInt(static_cast<long long>(video.decoded)));
    o.Set("sdl_video_presented", JVal::MkInt(static_cast<long long>(video.presented)));
    o.Set("sdl_video_audio_queued", JVal::MkInt(static_cast<long long>(video.queuedAudioBytes)));
    o.Set("sdl_video_audio_consumed", JVal::MkInt(static_cast<long long>(video.consumedAudioBytes)));
    o.Set("sdl_video_audible_frames", JVal::MkInt(static_cast<long long>(video.audibleAudioFrames)));
    o.Set("midi_provider", JVal::MkStr(midi.loaded ? "sdl-soundfont" : midi.attempted ? "unavailable" : "winmm"));
    o.Set("sdl_midi_active", JVal::MkBool(midi.active));
    o.Set("sdl_midi_queued", JVal::MkInt(static_cast<long long>(midi.rendering.queued)));
    o.Set("sdl_midi_completed", JVal::MkInt(static_cast<long long>(midi.rendering.completed)));
    o.Set("sdl_midi_notes", JVal::MkInt(static_cast<long long>(midi.rendering.notes)));
    o.Set("sdl_midi_audible_frames", JVal::MkInt(static_cast<long long>(midi.rendering.audibleFrames)));
    const auto cursors = KauaiSDL::Main().Cursors();
    const auto buffers = KauaiPixels::PixelBuffers();
    o.Set("portable_pixel_buffers", JVal::MkInt(static_cast<long long>(buffers.liveBuffers)));
    o.Set("portable_pixel_bytes", JVal::MkInt(static_cast<long long>(buffers.liveBytes)));
    auto root = GraphicsObject::PgobFromHwnd(static_cast<CoreNative::WindowHandle>(KauaiSDL::Main().WindowToken()));
    auto framebuffer = root ? root->Pgpt() : nullptr;
    o.Set("sdl_native_framebuffer_mirror",
          framebuffer ? JVal::MkBool(framebuffer->FHasNativeMirror()) : JVal::MkNull());
    o.Set("sdl_native_framebuffer_context",
          framebuffer ? JVal::MkBool(framebuffer->FHasNativeContext()) : JVal::MkNull());
    o.Set("indexed_pixel_ports_created", JVal::MkInt(GraphicsPort::CIndexedPortsCreated()));
    o.Set("native_indexed_contexts_created", JVal::MkInt(GraphicsPort::CIndexedContextsCreated()));
    o.Set("native_indexed_mirrors_created", JVal::MkInt(GraphicsPort::CIndexedMirrorsCreated()));
    o.Set("sdl_cursors_created", JVal::MkInt(static_cast<long long>(cursors.created)));
    o.Set("sdl_cursors_selected", JVal::MkInt(static_cast<long long>(cursors.selected)));
    o.Set("sdl_cursors_live", JVal::MkInt(static_cast<long long>(cursors.live)));
    const auto pointer = KauaiSDL::Main().Pointer();
    o.Set("sdl_capture_requested", JVal::MkBool(pointer.captureRequested));
    o.Set("sdl_capture_active", JVal::MkBool(pointer.captureActive));
    o.Set("sdl_confinement_requested", JVal::MkBool(pointer.confinementRequested));
    o.Set("sdl_confinement_active", JVal::MkBool(pointer.confinementActive));
    o.Set("sdl_pointer_suspended", JVal::MkBool(pointer.suspended));
    o.Set("sdl_slow_key_repeat", JVal::MkBool(KauaiSDL::Main().SlowKeyRepeat()));
    const auto audio = KauaiAudio::StudioStats();
    o.Set("sdl_audio_device", JVal::MkBool(audio.active));
    o.Set("sdl_audio_decoded", JVal::MkInt(static_cast<long long>(audio.decoded)));
    o.Set("sdl_audio_started", JVal::MkInt(static_cast<long long>(audio.started)));
    o.Set("sdl_audio_completed", JVal::MkInt(static_cast<long long>(audio.completed)));
    const auto recording = KauaiAudio::RecorderStats();
    o.Set("sdl_recording_opened", JVal::MkInt(static_cast<long long>(recording.opened)));
    o.Set("sdl_recording_started", JVal::MkInt(static_cast<long long>(recording.started)));
    o.Set("sdl_recording_completed", JVal::MkInt(static_cast<long long>(recording.completed)));
    o.Set("sdl_recording_previewed", JVal::MkInt(static_cast<long long>(recording.previewed)));
    o.Set("sdl_recording_frames", JVal::MkInt(static_cast<long long>(recording.capturedFrames)));
    KauaiSDL::PresentationStatus presentation;
    if (KauaiSDL::Main().Presentation(presentation))
    {
        JVal view = JVal::MkObj();
        view.Set("width", JVal::MkInt(static_cast<long long>(presentation.width)));
        view.Set("height", JVal::MkInt(static_cast<long long>(presentation.height)));
        view.Set("output_width", JVal::MkInt(presentation.outputWidth));
        view.Set("output_height", JVal::MkInt(presentation.outputHeight));
        view.Set("source_x", JVal::MkInt(presentation.sourceX));
        view.Set("source_y", JVal::MkInt(presentation.sourceY));
        view.Set("source_width", JVal::MkInt(presentation.sourceWidth));
        view.Set("source_height", JVal::MkInt(presentation.sourceHeight));
        view.Set("frame_width", JVal::MkInt(presentation.frameWidth));
        view.Set("frame_height", JVal::MkInt(presentation.frameHeight));
        o.Set("sdl_presentation", view);
    }
    const auto fonts = KauaiFonts::FontProviderStats();
    o.Set("portable_font_faces", JVal::MkInt(static_cast<long long>(fonts.portable)));
    o.Set("native_font_faces", JVal::MkInt(static_cast<long long>(fonts.native)));
    o.Set("font_fallbacks", JVal::MkInt(static_cast<long long>(fonts.fallback)));
    o.Set("native_font_draws", JVal::MkInt(static_cast<long long>(fonts.nativeDraws)));
    o.Set("native_font_measurements", JVal::MkInt(static_cast<long long>(fonts.nativeMeasurements)));
    o.Set("native_font_bootstraps", JVal::MkInt(static_cast<long long>(fonts.nativeBootstraps)));
    o.Set("portable_font_enumerations", JVal::MkInt(static_cast<long long>(fonts.portableEnumerations)));
    o.Set("native_font_enumerations", JVal::MkInt(static_cast<long long>(fonts.nativeEnumerations)));
    JVal selected = JVal::MkObj();
    auto studio = vpapp->Pstdio();
    auto movie = studio ? studio->Pmvie() : nullptr;
    o.Set("movie_only", JVal::MkBool(vpapp->FMovieOnlyPlayback()));
    o.Set("movie_paused", JVal::MkBool(movie && movie->FUserPaused()));
    o.Set("render_scale", JVal::MkInt(World::DefaultRenderScale()));
    JVal rendering = JVal::MkObj();
    rendering.Set("wireframe", JVal::MkBool(World::RenderWireframe()));
    rendering.Set("flat_shading", JVal::MkBool(World::FlatShading()));
    rendering.Set("no_textures", JVal::MkBool(World::NoTexture()));
    rendering.Set("show_actor_pose", JVal::MkBool(MovieView::FShowPoseReadout()));
    o.Set("rendering", std::move(rendering));
    o.Set("render_reduced", JVal::MkBool(movie && movie->Pbwld()->RenderScale() == 1 && movie->Pbwld()->FHalfX() &&
                                         movie->Pbwld()->FHalfY()));
    if (movie)
    {
        long width, height;
        movie->Pbwld()->GetRenderSize(&width, &height);
        o.Set("render_width", JVal::MkInt(width));
        o.Set("render_height", JVal::MkInt(height));
    }
    auto playbackView = movie ? static_cast<PMovieView>(movie->PddgGet(0)) : nullptr;
    o.Set("movie_waiting_for_click",
          JVal::MkBool(movie && movie->FPlaying() && playbackView && playbackView->FPausing()));
    auto scene = movie ? movie->Pscen() : nullptr;
    o.Set("movie_scene", JVal::MkInt(movie ? movie->Iscen() : -1));
    o.Set("scene_camera", JVal::MkInt(scene && scene->Pbkgd() ? scene->Pbkgd()->Icam() : -1));
    o.Set("scene_transition", JVal::MkInt(scene ? scene->Trans() : transNil));
    o.Set("scene_camera_count", JVal::MkInt(scene && scene->Pbkgd() ? scene->Pbkgd()->Ccam() : 0));
    if (const auto details = args.Get("movie_details"))
        if (details->AsBool())
            o.Set("scene_actors", MovieDetails(scene));
    o.Set("scene_actor_count", JVal::MkInt(scene ? scene->Cactr() : 0));
    o.Set("scene_textbox_count", JVal::MkInt(scene ? scene->Ctbox() : 0));
    o.Set("movie_scene_count", JVal::MkInt(movie ? movie->Cscen() : 0));
    o.Set("movie_playing", JVal::MkBool(movie && movie->FPlaying()));
    o.Set("scene_frame", JVal::MkInt(scene ? scene->Nfrm() : 0));
    o.Set("scene_first_frame", JVal::MkInt(scene ? scene->NfrmFirst() : 0));
    o.Set("scene_last_frame", JVal::MkInt(scene ? scene->NfrmLast() : 0));
    JVal captions = JVal::MkArr();
    for (long index = 0; scene && index < scene->Ctbox(); ++index)
    {
        auto caption = scene->PtboxFromItbox(index);
        JVal item = JVal::MkObj();
        const auto count = std::min<long>(caption->CpMac(), 1024);
        std::vector<achar> content(count);
        if (count)
            caption->FetchRgch(0, count, content.data());
        std::string utf8;
        const auto encoding = sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes;
        KauaiFonts::TextToUtf8({content.data(), static_cast<std::size_t>(count), encoding}, utf8);
        item.Set("text", JVal::MkStr(utf8));
        long firstFrame, lastFrame;
        if (caption->FGetLifetime(&firstFrame, &lastFrame))
        {
            item.Set("first_frame", JVal::MkInt(firstFrame - scene->NfrmFirst() + 1));
            item.Set("last_frame", JVal::MkInt(lastFrame - scene->NfrmFirst() + 1));
        }
        item.Set("visible", JVal::MkBool(caption->FIsVisible()));
        PAP paragraph;
        caption->FetchPap(0, &paragraph);
        item.Set("align", JVal::MkStr(paragraph.jc == jcCenter  ? "center"
                                      : paragraph.jc == jcRight ? "right"
                                                                : "left"));
        for (const auto last : {false, true})
        {
            CHP properties;
            properties.Clear();
            caption->FetchChp(last ? std::max<long>(0, caption->CpMac() - 2) : 0, &properties);
            if (!last)
                item.Set("font_size", JVal::MkInt(properties.dypFont));
            String font;
            vntl.GetStn(properties.onn, &font);
            KauaiFonts::NameToUtf8({font.Prgch(), static_cast<std::size_t>(font.Cch()), encoding}, utf8);
            item.Set(last ? "last_font" : "first_font", JVal::MkStr(utf8));
        }
        captions.a->push_back(std::move(item));
    }
    o.Set("scene_textboxes", std::move(captions));
    auto activeView = movie ? movie->PddgActive() : nullptr;
    if (activeView && activeView->FIs(kclsMovieView))
    {
        String current;
        vntl.GetStn(static_cast<PMovieView>(activeView)->OnnTextCur(), &current);
        std::string name;
        KauaiFonts::NameToUtf8({current.Prgch(), static_cast<std::size_t>(current.Cch()),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               name);
        o.Set("current_text_font", JVal::MkStr(name));
    }
    auto textbox = scene ? scene->PtboxSelected() : nullptr;
    selected.Set("present", JVal::MkBool(textbox != nullptr));
    if (textbox)
    {
        const auto count = std::min<long>(textbox->CpMac(), 1024);
        std::vector<achar> content(count);
        if (count)
            textbox->FetchRgch(0, count, content.data());
        std::string utf8;
        KauaiFonts::TextToUtf8({content.data(), static_cast<std::size_t>(count),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               utf8);
        selected.Set("text", JVal::MkStr(utf8));
        CHP chp;
        chp.Clear();
        textbox->FetchChp(0, &chp);
        ulong properties;
        textbox->FetchChpSel(&chp, &properties);
        selected.Set("selection_properties", JVal::MkInt(properties));
        String name;
        vntl.GetStn(chp.onn, &name);
        KauaiFonts::NameToUtf8({name.Prgch(), static_cast<std::size_t>(name.Cch()),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               utf8);
        selected.Set("font", JVal::MkStr(utf8));
        textbox->FetchChp(std::max<long>(0, textbox->CpMac() - 2), &chp);
        vntl.GetStn(chp.onn, &name);
        KauaiFonts::NameToUtf8({name.Prgch(), static_cast<std::size_t>(name.Cch()),
                                sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                               utf8);
        selected.Set("last_character_font", JVal::MkStr(utf8));
    }
    o.Set("selected_textbox", selected);
#endif
    o.Set("hwnd_present", JVal::MkBool(hwnd != NULL));
#ifdef KAUAI_SDL3
    if (hwnd)
    {
        KauaiSDL::WindowStatus state;
        KauaiSDL::Main().WindowState(state);
        o.Set("hwnd_visible", JVal::MkBool(state.shown));
        o.Set("hwnd_iconic", JVal::MkBool(state.minimized));
        o.Set("sdl_fullscreen", JVal::MkBool(state.fullscreen));
        o.Set("sdl_window_id", JVal::MkInt(KauaiSDL::Main().WindowID()));
        o.Set("sdl_integer_scaling", JVal::MkBool(KauaiSDL::Main().IntegerScaling()));
        o.Set("foreground", JVal::MkBool(state.keyboardFocus));
        std::string title;
        KauaiSDL::Main().Title(title);
        o.Set("title", JVal::MkStr(title));
        JVal dimensions = JVal::MkObj();
        int width = 0, height = 0;
        if (KauaiSDL::Main().WindowSize(width, height))
        {
            dimensions.Set("w", JVal::MkInt(width));
            dimensions.Set("h", JVal::MkInt(height));
            o.Set("client_size", std::move(dimensions));
        }
    }
    const auto dialog = KauaiSDL::CurrentDialog();
    JVal dlg = JVal::MkObj();
    dlg.Set("present", JVal::MkBool(dialog.active));
    if (dialog.active)
    {
        dlg.Set("title", JVal::MkStr(dialog.title));
        dlg.Set("class", JVal::MkStr("SDL"));
    }
    o.Set("dialog", std::move(dlg));
#elif defined(_WIN32)
    if (hwnd)
    {
        o.Set("hwnd_visible", JVal::MkBool(IsWindowVisible(hwnd) ? true : false));
        o.Set("hwnd_iconic", JVal::MkBool(IsIconic(hwnd) ? true : false));
        o.Set("foreground", JVal::MkBool(GetForegroundWindow() == hwnd));
        char title[256];
        title[0] = 0;
        GetWindowTextA(hwnd, title, sizeof(title));
        o.Set("title", JVal::MkStr(title));
        RECT rc;
        if (GetClientRect(hwnd, &rc))
        {
            JVal sz = JVal::MkObj();
            sz.Set("w", JVal::MkInt(rc.right - rc.left));
            sz.Set("h", JVal::MkInt(rc.bottom - rc.top));
            o.Set("client_size", std::move(sz));
        }
    }
    // Look for any modal popup owned by our process. Reports the first one.
    struct EnumCtx
    {
        DWORD pid;
        HWND found;
        std::string text;
        std::string cls;
    } ctx{GetCurrentProcessId(), NULL, {}, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto *c = (EnumCtx *)lp;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != c->pid) return TRUE;
            if (h == _GetMainWindow()) return TRUE;
            if (!IsWindowVisible(h)) return TRUE;
            char cls[64];
            cls[0] = 0;
            GetClassNameA(h, cls, sizeof(cls));
            if (std::strcmp(cls, "#32770") != 0) return TRUE; // Win32 dialog class
            char text[512];
            text[0] = 0;
            GetWindowTextA(h, text, sizeof(text));
            c->found = h;
            c->text = text;
            c->cls = cls;
            return FALSE;
        },
        (LPARAM)&ctx);
    JVal dlg = JVal::MkObj();
    dlg.Set("present", JVal::MkBool(ctx.found != NULL));
    if (ctx.found)
    {
        dlg.Set("title", JVal::MkStr(ctx.text));
        dlg.Set("class", JVal::MkStr(ctx.cls));
    }
    o.Set("dialog", std::move(dlg));
#endif
    if (vpers)
    {
        o.Set("error_stack_depth", JVal::MkInt(vpers->Cerc()));
        JVal codes = JVal::MkArr();
        for (LegacyLong i = 0; i < vpers->Cerc(); ++i)
            codes.a->push_back(JVal::MkInt(vpers->ErcGet(i)));
        o.Set("error_codes", std::move(codes));
    }
    JVal arr = JVal::MkArr();
    JVal item = JVal::MkObj();
    item.Set("type", JVal::MkStr("text"));
    std::string s;
    JWrite(s, o);
    item.Set("text", JVal::MkStr(s));
    arr.a->push_back(std::move(item));
    JVal res = JVal::MkObj();
    res.Set("content", std::move(arr));
    return res;
}

// --- wait_ms -------------------------------------------------------------

static JVal Tool_SeekMovie(const JVal &args)
{
    const auto enabled = std::getenv("THREEDMM_BATCH_TEST");
    if (!enabled || std::strcmp(enabled, "1"))
        return ContentError("Requires THREEDMM_BATCH_TEST=1 in an isolated test process");
    auto studio = vpapp->Pstdio();
    auto movie = studio ? studio->Pmvie() : nullptr;
    if (!movie || movie->FPlaying()) return ContentError("No idle movie is open");
    const auto requestedScene = args.Get("scene"), requestedFrame = args.Get("frame");
    if (!requestedScene || !requestedFrame || !requestedScene->IsInt() || !requestedFrame->IsInt())
        return ContentError("scene and frame must be one-based integers matching the UI");
    const auto sceneNumber = requestedScene->AsInt();
    if (sceneNumber < 1 || sceneNumber > movie->Cscen()) return ContentError("scene_out_of_range");
    const auto sceneIndex = sceneNumber - 1;
    bool success = movie->FSwitchScen(static_cast<long>(sceneIndex));
    auto scene = movie->Pscen();
    if (!scene) return ContentError("scene_not_ready");
    const auto frameCount = std::int64_t(scene->NfrmLast()) - scene->NfrmFirst() + 1;
    if (requestedFrame->AsInt() < 1 || requestedFrame->AsInt() > frameCount) return ContentError("frame_out_of_range");
    const auto frame = std::int64_t(scene->NfrmFirst()) + requestedFrame->AsInt() - 1;
    if (success) success = scene->FGotoFrm(static_cast<long>(frame));
    if (success) movie->Pbwld()->Render();
    movie->Pmcc()->UpdateScrollbars();
    vpappb->UpdateMarked();
    JVal result = JVal::MkObj();
    result.Set("success", JVal::MkBool(success));
    result.Set("scene_number", JVal::MkInt(movie->Iscen() + 1));
    result.Set("frame_number", JVal::MkInt(scene->Nfrm() - scene->NfrmFirst() + 1));
    return _WrapInfoAsContent(result);
}

static JVal Tool_WaitMs(const JVal &args)
{
    const auto requested = args.Get("ms") ? args.Get("ms")->AsInt(100) : 100;
    const auto ms = static_cast<std::uint32_t>(std::max<long long>(0, std::min<long long>(30000, requested)));
    PumpForMs(ms);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "waited %lu ms", (unsigned long)ms);
    return ContentText(buf);
}

// --- read_crash_log -----------------------------------------------------

static JVal Tool_ReadCrashLog(const JVal &args)
{
    char tmp[4096];
    if (!KauaiFiles::TempPath(tmp, sizeof(tmp))) return ContentError("TempPath failed");
    std::string path = std::string(tmp) + "/3dmmforever-crash.txt";
    std::ifstream f(path);
    if (!f.good()) return ContentText(std::string("(no crash log at ") + path + ")");
    std::stringstream ss;
    ss << f.rdbuf();
    return ContentText(ss.str());
}

// --- quit ----------------------------------------------------------------

static JVal Tool_Quit(const JVal &args)
{
    if (vpcex) vpcex->EnqueueCid(cidQuit);
    return ContentText("quit posted");
}

// --- list_dialogs / dismiss_dialog (worker-thread safe) ------------------
//
// Win32 EnumWindows / GetWindowTextW / SendMessage / PostMessage are
// thread-safe and don't require the GUI thread to be unblocked, so these
// tools dispatch inline from the worker reader thread (see ReaderThreadProc)
// instead of going through Drain. Without that we couldn't see or dismiss a
// modal MessageBox (e.g. an MSVC /RTCu Debug-Error popup), because Drain
// only runs from kauai's main loop which is suspended while the modal box is
// up.

#ifdef _WIN32
namespace
{
struct DialogScanContext
{
    DWORD pid = 0;
    JVal *arr = nullptr;
};

static std::string Utf16ToUtf8(const wchar_t *w)
{
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s((size_t)(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static BOOL CALLBACK CollectChildText(HWND hwnd, LPARAM lparam)
{
    auto *texts = reinterpret_cast<std::vector<std::string> *>(lparam);
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    int len = GetWindowTextLengthW(hwnd);
    if (len > 0 && len < 4096)
    {
        std::wstring wt((size_t)len, L'\0');
        GetWindowTextW(hwnd, &wt[0], len + 1);
        std::string t = Utf16ToUtf8(wt.c_str());
        std::string cs = Utf16ToUtf8(cls);
        if (!t.empty()) texts->push_back(cs + ": " + t);
    }
    return TRUE;
}

static BOOL CALLBACK CollectDialog(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<DialogScanContext *>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    // Skip our own main app window: we already report it via get_state.
    if (hwnd == _nativeMainWindow) return TRUE;

    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 512);

    JVal d = JVal::MkObj();
    d.Set("hwnd", JVal::MkInt(static_cast<long long>(reinterpret_cast<std::intptr_t>(hwnd))));
    d.Set("class", JVal::MkStr(Utf16ToUtf8(cls)));
    d.Set("title", JVal::MkStr(Utf16ToUtf8(title)));
    d.Set("enabled", JVal::MkBool(IsWindowEnabled(hwnd) ? true : false));
    HWND fg = GetForegroundWindow();
    d.Set("foreground", JVal::MkBool(hwnd == fg));

    std::vector<std::string> child_texts;
    EnumChildWindows(hwnd, &CollectChildText, reinterpret_cast<LPARAM>(&child_texts));
    JVal carr = JVal::MkArr();
    for (auto &s : child_texts) carr.a->push_back(JVal::MkStr(s));
    d.Set("children", std::move(carr));

    ctx->arr->a->push_back(std::move(d));
    return TRUE;
}
} // namespace

// Returns top-level windows owned by our process other than the main app
// window: dialog boxes (#32770), MSVC RTC popups, assertion dialogs, etc.
// Each entry includes hwnd (as integer), class name, title, and the visible
// text of all child controls (so we can read button labels and the dialog
// body).
static JVal Tool_ListDialogs(const JVal &args)
{
    JVal arr = JVal::MkArr();
    DialogScanContext ctx{GetCurrentProcessId(), &arr};
    EnumWindows(&CollectDialog, reinterpret_cast<LPARAM>(&ctx));
    JVal r = JVal::MkObj();
    r.Set("count", JVal::MkInt((long)arr.a->size()));
    r.Set("dialogs", std::move(arr));
    // Wrap as content so the caller's JSON-RPC result has the same shape as
    // other tools.
    JVal content_arr = JVal::MkArr();
    JVal item = JVal::MkObj();
    item.Set("type", JVal::MkStr("text"));
    std::string s;
    JWrite(s, r);
    item.Set("text", JVal::MkStr(s));
    content_arr.a->push_back(std::move(item));
    JVal res = JVal::MkObj();
    res.Set("content", std::move(content_arr));
    return res;
}

namespace
{
struct FindButtonContext
{
    std::string want;     // case-insensitive substring match
    HWND found = nullptr; // first match wins
};

static std::string ToLower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

static BOOL CALLBACK FindButtonByText(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<FindButtonContext *>(lparam);
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    // Must be a button-like control to BM_CLICK reliably.
    if (lstrcmpiW(cls, L"Button") != 0) return TRUE;
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0 || len > 256) return TRUE;
    std::wstring wt((size_t)len, L'\0');
    GetWindowTextW(hwnd, &wt[0], len + 1);
    std::string t = ToLower(Utf16ToUtf8(wt.c_str()));
    // Strip Windows accelerator prefix '&'.
    std::string filtered;
    filtered.reserve(t.size());
    for (char c : t)
        if (c != '&') filtered.push_back(c);
    if (filtered.find(ctx->want) != std::string::npos)
    {
        ctx->found = hwnd;
        return FALSE; // stop enum
    }
    return TRUE;
}
} // namespace

// Dismiss a dialog. Two modes:
//   1. {"hwnd": ..., "button": "abort|retry|ignore|ok|cancel|yes|no|close"} —
//      posts WM_COMMAND with the standard MessageBox button id. Works for
//      regular MessageBox-style dialogs and for the MSVC /RTCu Debug Error
//      popup (which uses Abort/Retry/Ignore).
//   2. {"hwnd": ..., "button_text": "Ignore"} — finds the first child Button
//      whose label contains that text (case-insensitive, '&' accelerator
//      ignored) and BM_CLICKs it. Use this when the standard ids don't work
//      (custom dialog procs).
static JVal Tool_DismissDialog(const JVal &args)
{
    const auto hwnd_lw = args.Get("hwnd") ? args.Get("hwnd")->AsInt(0) : 0;
    HWND hwnd = (HWND)(intptr_t)hwnd_lw;
    if (!hwnd || !IsWindow(hwnd)) return ContentError("invalid hwnd");

    if (args.Get("button_text"))
    {
        std::string txt = ToLower(args.Get("button_text")->AsStr());
        FindButtonContext ctx{txt, nullptr};
        EnumChildWindows(hwnd, &FindButtonByText, reinterpret_cast<LPARAM>(&ctx));
        if (!ctx.found) return ContentError(std::string("no child button matched: ") + txt);
        PostMessage(ctx.found, BM_CLICK, 0, 0);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "BM_CLICK posted to button hwnd=%lld", (long long)(intptr_t)ctx.found);
        return ContentText(buf);
    }

    std::string button = args.Get("button") ? args.Get("button")->AsStr() : "ok";
    int id = IDOK;
    if (button == "ok")
        id = IDOK;
    else if (button == "cancel")
        id = IDCANCEL;
    else if (button == "abort")
        id = IDABORT;
    else if (button == "retry")
        id = IDRETRY;
    else if (button == "ignore")
        id = IDIGNORE;
    else if (button == "yes")
        id = IDYES;
    else if (button == "no")
        id = IDNO;
    else if (button == "close")
    {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return ContentText("posted WM_CLOSE");
    }
    else
        return ContentError(std::string("unknown button: ") + button);
    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "posted WM_COMMAND id=%d to hwnd=%lld", id, hwnd_lw);
    return ContentText(buf);
}

#endif // _WIN32

// --- tool registry -------------------------------------------------------

struct ToolDef
{
    const char *name;
    const char *desc;
    const char *schemaJson; // raw JSON for inputSchema
    JVal (*handler)(const JVal &args);
    bool worker_safe; // safe to run on the reader thread (no kauai state)
};

#ifdef KAUAI_SDL3
static JVal Tool_GetSDLDialog(const JVal &)
{
    const auto state = KauaiSDL::CurrentDialog();
    JVal result = JVal::MkObj();
    result.Set("active", JVal::MkBool(state.active));
    result.Set("ready", JVal::MkBool(state.ready));
    result.Set("embedded", JVal::MkBool(state.embedded));
    result.Set("generation", JVal::MkInt(static_cast<long long>(state.generation)));
    result.Set("window_id", JVal::MkInt(state.window));
    result.Set("title", JVal::MkStr(state.title));
    JVal controls = JVal::MkArr();
    for (const auto &control : state.controls)
    {
        JVal item = JVal::MkObj();
        item.Set("id", JVal::MkInt(control.id));
        item.Set("item", JVal::MkInt(control.item));
        item.Set("label", JVal::MkStr(control.label));
        item.Set("text", JVal::MkStr(control.text));
        item.Set("number", JVal::MkInt(control.number));
        item.Set("enabled", JVal::MkBool(control.enabled));
        item.Set("read_only", JVal::MkBool(control.readOnly));
        controls.a->push_back(std::move(item));
    }
    result.Set("controls", std::move(controls));
    std::string text;
    JWrite(text, result);
    return ContentText(text);
}
static JVal Tool_SDLDialogAction(const JVal &args)
{
    const auto generation = args.Get("generation");
    const auto action = args.Get("action");
    if (!generation || !action)
        return ContentError("generation and action are required");
    KauaiSDL::DialogAction request;
    const auto name = action->AsStr();
    if (name == "click")
        request.kind = KauaiSDL::DialogActionKind::click;
    else if (name == "text")
        request.kind = KauaiSDL::DialogActionKind::text;
    else if (name == "enter")
        request.kind = KauaiSDL::DialogActionKind::enter;
    else if (name == "escape")
        request.kind = KauaiSDL::DialogActionKind::escape;
    else if (name == "tab")
        request.kind = KauaiSDL::DialogActionKind::tab;
    else if (name == "screenshot")
        request.kind = KauaiSDL::DialogActionKind::screenshot;
    else
        return ContentError("Unknown SDL dialog action");
    if (const auto id = args.Get("control"))
        request.control = static_cast<std::uint32_t>(id->AsInt());
    if (const auto text = args.Get("text"))
        request.text = text->AsStr();
    if (!KauaiSDL::QueueDialogAction(static_cast<std::uint64_t>(generation->AsInt()), request))
        return ContentError("Dialog/control is unavailable, read-only, disabled or from a different session");
    return ContentText("Queued through SDL dialog input.");
}
#endif

static const ToolDef kTools[] = {
#ifdef KAUAI_SDL3
    {"list_assets", "List original asset names and source/chunk tags for direct authoring.",
     "{\"type\":\"object\",\"properties\":{\"kind\":{\"type\":\"string\",\"enum\":[\"props\",\"actors\","
     "\"backgrounds\",\"materials\"]}}}",
     &Tool_ListAssets, false},
    {"create_scene", "Add a scene using a background tag from list_assets; returns its camera transform.",
     "{\"type\":\"object\",\"properties\":{\"background_cno\":{\"type\":\"integer\"},\"sid\":{\"type\":\"integer\"}},"
     "\"required\":[\"background_cno\"]}",
     &Tool_CreateScene, false},
    {"edit_frame", "Select a one-based authoring frame, extending the scene when necessary.",
     "{\"type\":\"object\",\"properties\":{\"frame\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000}},"
     "\"required\":[\"frame\"]}",
     &Tool_EditFrame, false},
    {"select_scene", "Select an existing one-based scene for authoring without extending it.",
     "{\"type\":\"object\",\"properties\":{\"scene\":{\"type\":\"integer\",\"minimum\":1}},\"required\":[\"scene\"]}",
     &Tool_SelectScene, false},
    {"set_frame_pause", "Set a native pause at the current authoring frame; sound waits for audio to finish.",
     "{\"type\":\"object\",\"properties\":{\"kind\":{\"type\":\"string\",\"enum\":[\"none\",\"sound\",\"click\","
     "\"time\"]},"
     "\"ms\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3600000}},\"required\":[\"kind\"]}",
     &Tool_SetFramePause, false},
    {"create_text_box", "Insert native story text at this frame on the 544 by 306 movie canvas; defaults to scene end.",
     "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"bounds\":{\"type\":\"array\","
     "\"items\":{\"type\":\"integer\"},\"minItems\":4,\"maxItems\":4},\"font_size\":{\"type\":\"integer\"},"
     "\"bold\":{\"type\":\"boolean\"},\"color\":{\"type\":\"string\"},\"font\":{\"type\":\"string\"},"
     "\"align\":{\"type\":\"string\",\"enum\":[\"left\",\"center\",\"right\"]},\"last_frame\":{\"type\":\"integer\"}},"
     "\"required\":[\"text\",\"bounds\"]}",
     &Tool_CreateTextBox, false},
    {"edit_text_box",
     "Replace a native text box's content using its first-character style, retaining bounds and lifetime.",
     "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\",\"minimum\":1},\"text\":{\"type\":"
     "\"string\"},"
     "\"align\":{\"type\":\"string\",\"enum\":[\"left\",\"center\",\"right\"]}},\"required\":[\"index\",\"text\"]}",
     &Tool_EditTextBox, false},
    {"create_actor", "Insert and commit a named actor or prop from its template tag, with native undo.",
     "{\"type\":\"object\",\"properties\":{\"template_cno\":{\"type\":\"integer\"},\"sid\":{\"type\":\"integer\"},"
     "\"name\":{\"type\":\"string\"}},\"required\":[\"template_cno\"]}",
     &Tool_CreateActor, false},
    {"transform_actor",
     "Edit a prop/actor at this frame: absolute world xyz, relative offset, world-axis Euler degrees, uniform scale, "
     "xyz stretch and #RRGGBB colour (shaded false selects one palette entry). relative applies to rotation/size; "
     "absolute is default. Uses normal authored "
     "events and undo.",
     "{\"type\":\"object\",\"properties\":{\"actor_id\":{\"type\":\"integer\"},\"position\":{\"type\":\"array\","
     "\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3},\"offset\":{\"type\":\"array\",\"items\":{"
     "\"type\":\"number\"},\"minItems\":3,\"maxItems\":3},\"rotation\":{\"type\":\"array\",\"items\":{\"type\":"
     "\"number\"},\"minItems\":3,\"maxItems\":3},\"scale\":{\"type\":\"number\",\"minimum\":0.25,\"maximum\":10},"
     "\"stretch\":{\"type\":\"array\",\"items\":{\"type\":\"number\",\"minimum\":0.25,\"maximum\":4},\"minItems\":3,"
     "\"maxItems\":3},\"relative\":{\"type\":\"boolean\"},\"color\":{\"type\":\"string\"},"
     "\"shaded\":{\"type\":\"boolean\"}},\"required\":[\"actor_id\"]"
     "}",
     &Tool_TransformActor, false},
    {"inspect_actor", "Read an actor's world position, authored rotation/size and model dimensions.",
     "{\"type\":\"object\",\"properties\":{\"actor_id\":{\"type\":\"integer\"}},\"required\":[\"actor_id\"]}",
     &Tool_InspectActor, false},
    {"transform_actors",
     "Apply up to 256 transform_actor edits in order, with one repaint. Stops on the first error and reports completed "
     "edits; each edit keeps native undo.",
     "{\"type\":\"object\",\"properties\":{\"edits\":{\"type\":\"array\",\"items\":{\"type\":\"object\"},\"minItems\":"
     "1,\"maxItems\":256}},\"required\":[\"edits\"]}",
     &Tool_TransformActors, false},
    {"import_sound", "Import a WAVE file and place it as a non-looping scene speech cue at the authoring frame.",
     "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}", &Tool_ImportSound,
     false},
    {"save_movie", "Save using the original movie writer; requires a new destination file.",
     "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}", &Tool_SaveMovie,
     false},
    {"get_sdl_dialog", "Observe the active SDL modal dialog and original control IDs without touching Kauai state.",
     "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}", &Tool_GetSDLDialog, true},
    {"sdl_dialog_action",
     "Queue mouse, text or keyboard input to the observed SDL dialog; screenshot text is a BMP output path.",
     "{\"type\":\"object\",\"properties\":{\"generation\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\"},"
     "\"control\":{\"type\":\"integer\"},\"text\":{\"type\":\"string\"}},\"required\":[\"generation\",\"action\"]}",
     &Tool_SDLDialogAction, true},
    {
        "type_text",
        "Queue UTF-8 text through the SDL Studio text-input event path.",
        "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}",
        &Tool_TypeText,
        false,
    },
#endif
    {
        "screenshot",
        "Capture the Studio canvas as PNG; presentation=true captures the movie-only frame at its rendering size.",
        "{\"type\":\"object\",\"properties\":{\"presentation\":{\"type\":\"boolean\"}},\"additionalProperties\":false}",
        &Tool_Screenshot,
        false,
    },
    {
        "click",
        "Click a point in the 3dmovie main window's client coordinates.",
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"shift\":{\"type\":\"boolean\"},"
        "\"x\":{\"type\":\"integer\"},"
        "\"y\":{\"type\":\"integer\"},"
        "\"button\":{\"type\":\"string\",\"enum\":[\"left\",\"right\"]}"
        "},\"required\":[\"x\",\"y\"]}",
        &Tool_Click,
        false,
    },
    {
        "key",
        "Queue a key down/up using the legacy virtual-key code through the Studio input backend.",
        "{\"type\":\"object\","
        "\"properties\":{\"vk\":{\"type\":\"integer\"}},"
        "\"required\":[\"vk\"]}",
        &Tool_Key,
        false,
    },
    {
        "send_command",
        "Enqueue a kauai command (cid, optional target hid, lw0..lw3) into vpcex.",
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"cid\":{\"type\":\"integer\"},"
        "\"hid\":{\"type\":\"integer\"},"
        "\"lw0\":{\"type\":\"integer\"},"
        "\"lw1\":{\"type\":\"integer\"},"
        "\"lw2\":{\"type\":\"integer\"},"
        "\"lw3\":{\"type\":\"integer\"}"
        "},\"required\":[\"cid\"]}",
        &Tool_SendCommand,
        false,
    },
    {
        "find_gob",
        "Look up a kidspace gob by hid. Returns hwnd-client rect, visible "
        "rect (zero size = hidden / not yet laid out), and a 'visible' bool.",
        "{\"type\":\"object\","
        "\"properties\":{\"hid\":{\"type\":\"integer\"}},"
        "\"required\":[\"hid\"]}",
        &Tool_FindGob,
        false,
    },
    {
        "wait_for_gob",
        "Poll find_gob until the gob meets the readiness signal, pumping "
        "host events between probes. mode=visible (default) waits for "
        "non-empty rcVis -- gob actually painted; mode=positioned waits for "
        "found AND non-zero w/h (use for child gobs whose rcVis lags painting "
        "but whose rcCur is already in place after a MoveAbsGob); mode=exists "
        "just waits for the gob to be created. Returns same fields as "
        "find_gob plus waited_ms; if the timeout (default 5000ms, max 30000ms) "
        "elapses, returns timed_out:true.",
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"hid\":{\"type\":\"integer\"},"
        "\"timeout_ms\":{\"type\":\"integer\"},"
        "\"mode\":{\"type\":\"string\",\"enum\":[\"visible\",\"positioned\",\"exists\"]}"
        "},\"required\":[\"hid\"]}",
        &Tool_WaitForGob,
        false,
    },
    {
        "get_state",
        "Report main window state, foreground status, modal dialog (if any), and ErrorStack depth.",
        "{\"type\":\"object\",\"properties\":{\"movie_details\":{\"type\":\"boolean\"}},\"additionalProperties\":"
        "false}",
        &Tool_GetState,
        false,
    },
    {"test_movie_playback", "Advance every movie frame without timer waits; isolated test processes only.",
     "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\"},\"max_frames\":{\"type\":\"integer\"},"
     "\"max_ms\":{\"type\":\"integer\"}},\"additionalProperties\":false}",
     &Tool_TestMoviePlayback, false},
    {"seek_movie", "Seek using one-based scene/frame UI numbers and return the engine's actual success status.",
     "{\"type\":\"object\",\"properties\":{\"scene\":{\"type\":\"integer\"},\"frame\":{\"type\":\"integer\"}},"
     "\"required\":[\"scene\",\"frame\"],\"additionalProperties\":false}",
     &Tool_SeekMovie, false},
    {
        "wait_ms",
        "Pump host events for N ms (capped at 30000) so the GUI can settle.",
        "{\"type\":\"object\","
        "\"properties\":{\"ms\":{\"type\":\"integer\"}},"
        "\"required\":[\"ms\"]}",
        &Tool_WaitMs,
        false,
    },
    {
        "read_crash_log",
        "Return 3dmmforever-crash.txt from the platform temporary directory.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_ReadCrashLog,
        false,
    },
    {
        "quit",
        "Request graceful Studio shutdown through its command queue.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_Quit,
        false,
    },
#ifdef _WIN32
    {
        "list_dialogs",
        "Enumerate top-level windows (other than the main app window) owned by "
        "this process: dialog boxes, MSVC RTC popups, kauai assertion dialogs, etc. "
        "Returns hwnd, class name, title, and child-control texts. Runs on the MCP "
        "worker thread so it works even when the main thread is blocked in a modal box.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_ListDialogs,
        true,
    },
    {
        "dismiss_dialog",
        "Dismiss a top-level dialog. Pass {\"hwnd\":N,\"button\":\"abort|retry|ignore|ok|cancel|yes|no|close\"} "
        "to post a standard MessageBox button id, or {\"hwnd\":N,\"button_text\":\"Ignore\"} to find a child "
        "Button by its label and BM_CLICK it (case-insensitive substring; '&' accelerators ignored). "
        "Worker-thread safe.",
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"hwnd\":{\"type\":\"integer\"},"
        "\"button\":{\"type\":\"string\"},"
        "\"button_text\":{\"type\":\"string\"}"
        "},\"required\":[\"hwnd\"]}",
        &Tool_DismissDialog,
        true,
    },
#endif
};

// Worker-direct dispatch: returns true if the request was handled inline
// from the reader thread (tools/call against a worker_safe tool); false if
// the caller should queue it for Drain to handle on the main thread.
static bool TryDispatchWorkerDirect(const Request &req)
{
    if (req.method != "tools/call") return false;
    auto *pname = req.params.Get("name");
    if (!pname) return false;
    std::string tname = pname->AsStr();
    for (const auto &t : kTools)
    {
        if (tname == t.name)
        {
            if (!t.worker_safe) return false;
            auto *pargs = req.params.Get("arguments");
            JVal targs = pargs ? *pargs : JVal::MkObj();
            auto result = t.handler(targs);
            if (req.hasId) WriteMessage(MakeResult(req.id, std::move(result)));
            return true;
        }
    }
    return false;
}

static JVal Tool_ListTools()
{
    JVal arr = JVal::MkArr();
    for (const auto &t : kTools)
    {
        JVal obj = JVal::MkObj();
        obj.Set("name", JVal::MkStr(t.name));
        obj.Set("description", JVal::MkStr(t.desc));
        // Parse the schema JSON into a JVal so it serializes as a real object,
        // not a string. Done once per tools/list call - cheap.
        JVal schema;
        JParser p(t.schemaJson, std::strlen(t.schemaJson));
        if (p.FParse(&schema)) obj.Set("inputSchema", std::move(schema));
        arr.a->push_back(std::move(obj));
    }
    JVal r = JVal::MkObj();
    r.Set("tools", std::move(arr));
    return r;
}

static JVal Tool_CallTool(const JVal &params)
{
    std::string name = params.Get("name") ? params.Get("name")->AsStr() : "";
    JVal args = params.Get("arguments") ? *params.Get("arguments") : JVal::MkObj();
    for (const auto &t : kTools)
    {
        if (name == t.name) return t.handler(args);
    }
    return ContentError(std::string("unknown tool: ") + name);
}

static JVal Tool_Initialize(const JVal &params)
{
    JVal r = JVal::MkObj();
    r.Set("protocolVersion", JVal::MkStr("2024-11-05"));
    JVal caps = JVal::MkObj();
    caps.Set("tools", JVal::MkObj());
    r.Set("capabilities", std::move(caps));
    JVal info = JVal::MkObj();
    info.Set("name", JVal::MkStr("3dmovie"));
    info.Set("version", JVal::MkStr("0.1.0"));
    r.Set("serverInfo", std::move(info));
    return r;
}

// ============================================================
// Public API.
// ============================================================

bool FParseEnabledFromCommandLine(const char *pszCmdLine)
{
    if (!pszCmdLine) return false;
    return std::strstr(pszCmdLine, "--mcp-server") != nullptr;
}

bool FActive() { return _enabled.load(); }

bool FInit()
{
    if (_initialized.load()) return true;
    // Self-check the command line: callers don't need a separate enable step.
    bool requested = FParseEnabledFromCommandLine(KauaiLaunch::CommandLine());
    if (KauaiLaunch::HasArguments())
    {
        for (int i = 1; i < KauaiLaunch::ArgumentCount(); ++i)
        {
            const auto argument = KauaiLaunch::Arguments()[i];
            if (argument[0] != '-' || !std::strcmp(argument, "--"))
                break;
            if (!std::strcmp(argument, "--mcp-server"))
                requested = true;
        }
    }
    if (!requested) return false;
    _enabled.store(true);

    if (!StdioAvailable())
    {
        _enabled.store(false);
        return false;
    }
    _shouldExit.store(false);
    _quitPosted = false;
#ifdef _WIN32
    _nativeMainWindow = _GetMainWindow();
#ifndef KAUAI_SDL3
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&_gdiplusToken, &gsi, NULL);
#endif

    // Install our /RTC error handler so we capture which source file:line
    // tripped the runtime check (e.g. /RTCu uninitialized-variable). The
    // standard MSVC handler shows the modal Debug Error dialog but the dialog
    // doesn't always preserve the file path. Keep the existing logging hook.
#ifdef _MSC_VER
    _RTC_SetErrorFuncW(&MyRtcErrorFunc);
#endif
#endif // _WIN32

    _readerThread = std::thread(ReaderThreadProc);
    _initialized.store(true);
    return true;
}

void Shutdown()
{
    if (!_initialized.load()) return;
    _shouldExit.store(true);
    if (_readerThread.joinable()) _readerThread.join();
#if defined(_WIN32) && !defined(KAUAI_SDL3)
    if (_gdiplusToken)
    {
        Gdiplus::GdiplusShutdown(_gdiplusToken);
        _gdiplusToken = 0;
    }
#endif
    _initialized.store(false);
    _enabled.store(false);
    std::lock_guard<std::mutex> lk(_qMutex);
    _queue.clear();
}

void Drain(bool readOnly)
{
    if (!_initialized.load()) return;

    // Snapshot current queue contents to minimize lock-hold time.
    std::deque<Request> reqs;
    {
        std::lock_guard<std::mutex> lk(_qMutex);
        if (!readOnly)
            reqs.swap(_queue);
        else
            for (auto request = _queue.begin(); request != _queue.end();)
            {
                const auto name = request->params.Get("name");
                const auto tool = name ? name->AsStr() : std::string();
                if (request->method == "tools/call" &&
                    (tool == "get_state" || tool == "screenshot" || tool == "find_gob"))
                {
                    reqs.push_back(std::move(*request));
                    request = _queue.erase(request);
                }
                else
                    ++request;
            }
    }
    for (auto &req : reqs)
    {
        if (req.method == "initialize")
        {
            auto result = Tool_Initialize(req.params);
            if (req.hasId) WriteMessage(MakeResult(req.id, std::move(result)));
        }
        else if (req.method == "tools/list")
        {
            auto result = Tool_ListTools();
            if (req.hasId) WriteMessage(MakeResult(req.id, std::move(result)));
        }
        else if (req.method == "tools/call")
        {
            // No SEH guard here: if a tool handler faults, let it bubble up to
            // the Application::Run __try/__except in src/studio/utest.cpp so
            // the existing crash-log path captures it. The host will see the
            // stdio pipe close and can relaunch.
            auto result = Tool_CallTool(req.params);
            if (req.hasId) WriteMessage(MakeResult(req.id, std::move(result)));
        }
        else if (req.method == "ping")
        {
            auto result = JVal::MkObj();
            if (req.hasId) WriteMessage(MakeResult(req.id, std::move(result)));
        }
        else if (req.method == "notifications/initialized" || req.method == "notifications/cancelled")
        {
            // No response for notifications.
        }
        else if (req.hasId)
        {
            WriteMessage(MakeError(req.id, -32601, std::string("method not found: ") + req.method));
        }
    }
    if (!readOnly && _shouldExit.load() && !_quitPosted && vpcex)
    {
        // EOF may have arrived after our snapshot, with more requests queued
        // behind it. Let the next Drain answer those before scheduling quit.
        std::lock_guard<std::mutex> lk(_qMutex);
        if (_queue.empty())
        {
            _quitPosted = true;
            vpcex->EnqueueCid(cidQuit);
        }
    }
}

} // namespace mcp

// Legacy C wrapper retained for callers outside the namespace. The framework
// entry adapter no longer depends on this Studio-specific symbol.
extern "C" int Mcp_FEnabledFromCmdLine(const char *psz)
{
    return mcp::FParseEnabledFromCommandLine(psz) ? 1 : 0;
}

#else // !DEBUG

// Retain the legacy wrapper as a no-op in release builds.
extern "C" int Mcp_FEnabledFromCmdLine(const char *)
{
    return 0;
}

#endif // DEBUG
