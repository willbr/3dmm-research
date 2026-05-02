/* Copyright (c) 3DMMForever contributors.
   Licensed under the MIT License. */

/***************************************************************************
    mcpserv.cpp: embedded MCP server. See mcpserv.h.

    Wire protocol: newline-delimited JSON-RPC 2.0 over stdin/stdout.

    Threading model:
    - One worker thread does blocking ReadFile on stdin, parses line-by-line
      JSON-RPC, and pushes Request objects onto a queue protected by a mutex.
    - The main GUI thread calls mcp::Drain() once per kauai event-loop tick
      (from Application::TopOfLoop). Drain pops requests, dispatches synchronously
      on the GUI thread (so it's always safe to touch HWNDs / GDI / vpcex), and
      writes responses back to stdout.
    - Stdout writes are guarded by the same mutex so the worker thread (which
      doesn't currently write, but might in the future for notifications) and
      the main thread can't interleave message bytes.
***************************************************************************/

#ifdef DEBUG

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

#include <windows.h>
#include <objidl.h> // IStream
#include <gdiplus.h>
#include <rtcapi.h> // _RTC_SetErrorFuncW for runtime-check capture
#pragma comment(lib, "gdiplus.lib")

#include "studio.h"
#include "mcpserv.h"

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
static HANDLE _hStdin = INVALID_HANDLE_VALUE;
static HANDLE _hStdout = INVALID_HANDLE_VALUE;
static std::thread _readerThread;
static std::mutex _qMutex;
static std::deque<Request> _queue;
static std::mutex _writeMutex;
static ULONG_PTR _gdiplusToken = 0;

// ============================================================
// I/O helpers
// ============================================================

static void WriteRaw(const char *p, size_t n)
{
    if (_hStdout == INVALID_HANDLE_VALUE) return;
    std::lock_guard<std::mutex> lk(_writeMutex);
    DWORD written = 0;
    DWORD remaining = (DWORD)n;
    while (remaining > 0)
    {
        if (!WriteFile(_hStdout, p, remaining, &written, NULL)) break;
        if (written == 0) break;
        p += written;
        remaining -= written;
    }
    FlushFileBuffers(_hStdout);
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
// return 1 so the standard dialog still appears, preserving normal behavior
// for interactive use and so `list_dialogs` can present it to an MCP client.
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
// Reader thread: blocking ReadFile loop with line splitting.
// ============================================================

// Forward decl; defined after kTools because it walks that table.
static bool TryDispatchWorkerDirect(const Request &req);

static void ReaderThreadProc()
{
    std::string buf;
    char chunk[4096];
    for (;;)
    {
        DWORD got = 0;
        BOOL ok = ReadFile(_hStdin, chunk, sizeof(chunk), &got, NULL);
        if (!ok || got == 0)
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

extern HWND _GetMainHwnd(); // forward-decls below
static JVal Tool_Initialize(const JVal &params);
static JVal Tool_ListTools();
static JVal Tool_CallTool(const JVal &params);

// Pump Windows messages AND drain kauai's command queue for `ms`
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
static void PumpForMs(DWORD ms)
{
    DWORD start = GetTickCount();
    for (;;)
    {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= ms) return;
        DWORD remaining = ms - elapsed;
        MsgWaitForMultipleObjects(0, NULL, FALSE, remaining > 16 ? 16 : remaining, QS_ALLINPUT);
        MSG m;
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
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

static HWND _GetMainHwnd()
{
    return vwig.hwndApp;
}

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
static std::string CaptureWindowPngBase64(HWND hwnd)
{
    if (!hwnd) return {};
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
    HWND hwnd = _GetMainHwnd();
    if (!hwnd) return ContentError("main window not available");
    std::string b64 = CaptureWindowPngBase64(hwnd);
    if (b64.empty()) return ContentError("screenshot capture failed");
    return ContentImage(b64);
}

// --- click ----------------------------------------------------------------

static JVal Tool_Click(const JVal &args)
{
    HWND hwnd = _GetMainHwnd();
    if (!hwnd) return ContentError("main window not available");
    long x = (long)(args.Get("x") ? args.Get("x")->AsInt(-1) : -1);
    long y = (long)(args.Get("y") ? args.Get("y")->AsInt(-1) : -1);
    if (x < 0 || y < 0) return ContentError("missing or negative x/y");
    std::string button = args.Get("button") ? args.Get("button")->AsStr() : "left";
    bool isRight = (button == "right");

    POINT p = {x, y};
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
    INPUT mv = {};
    mv.type = INPUT_MOUSE;
    mv.mi.dx = nx;
    mv.mi.dy = ny;
    mv.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &mv, sizeof(INPUT));
    PumpForMs(40);

    INPUT dn = {};
    dn.type = INPUT_MOUSE;
    dn.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &dn, sizeof(INPUT));
    PumpForMs(60);

    INPUT up = {};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    SendInput(1, &up, sizeof(INPUT));
    PumpForMs(100);

    char msg[160];
    std::snprintf(msg, sizeof(msg), "clicked %s at client (%ld,%ld) screen (%ld,%ld)",
                  isRight ? "right" : "left", x, y, p.x, p.y);
    return ContentText(msg);
}

// --- key -----------------------------------------------------------------

static JVal Tool_Key(const JVal &args)
{
    HWND hwnd = _GetMainHwnd();
    if (!hwnd) return ContentError("main window not available");
    int vk = (int)(args.Get("vk") ? args.Get("vk")->AsInt(0) : 0);
    if (vk == 0) return ContentError("missing vk");

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
}

// --- send_command --------------------------------------------------------

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

static JVal Tool_FindGob(const JVal &args)
{
    long hid = (long)(args.Get("hid") ? args.Get("hid")->AsInt(0) : 0);
    if (hid == 0) return ContentError("missing hid");
    JVal info = _GobInfo(hid);
    if (!info.Get("found")->AsBool(false)) return ContentError("no gob with that hid");
    return _WrapInfoAsContent(info);
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
    DWORD start = GetTickCount();
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
            info.Set("waited_ms", JVal::MkInt((long)(GetTickCount() - start)));
            return _WrapInfoAsContent(info);
        }
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= (DWORD)timeoutMs)
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

static JVal Tool_GetState(const JVal &args)
{
    HWND hwnd = _GetMainHwnd();
    JVal o = JVal::MkObj();
    o.Set("alive", JVal::MkBool(true));
    o.Set("hwnd_present", JVal::MkBool(hwnd != NULL));
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
            if (h == vwig.hwndApp) return TRUE;
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
    if (vpers) o.Set("error_stack_depth", JVal::MkInt(vpers->Cerc()));
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

static JVal Tool_WaitMs(const JVal &args)
{
    DWORD ms = (DWORD)(args.Get("ms") ? args.Get("ms")->AsInt(100) : 100);
    if (ms > 30000) ms = 30000;
    PumpForMs(ms);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "waited %lu ms", (unsigned long)ms);
    return ContentText(buf);
}

// --- read_crash_log -----------------------------------------------------

static JVal Tool_ReadCrashLog(const JVal &args)
{
    char tmp[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmp)) return ContentError("GetTempPath failed");
    std::string path = std::string(tmp) + "3dmmforever-crash.txt";
    std::ifstream f(path);
    if (!f.good()) return ContentText(std::string("(no crash log at ") + path + ")");
    std::stringstream ss;
    ss << f.rdbuf();
    return ContentText(ss.str());
}

// --- quit ----------------------------------------------------------------

static JVal Tool_Quit(const JVal &args)
{
    PostMessage(vwig.hwndApp, WM_CLOSE, 0, 0);
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
    if (hwnd == vwig.hwndApp) return TRUE;

    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 512);

    JVal d = JVal::MkObj();
    d.Set("hwnd", JVal::MkInt((long)(intptr_t)hwnd));
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
    long hwnd_lw = args.Get("hwnd") ? (long)args.Get("hwnd")->AsInt(0) : 0;
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
    std::snprintf(buf, sizeof(buf), "posted WM_COMMAND id=%d to hwnd=%ld", id, hwnd_lw);
    return ContentText(buf);
}

// --- tool registry -------------------------------------------------------

struct ToolDef
{
    const char *name;
    const char *desc;
    const char *schemaJson; // raw JSON for inputSchema
    JVal (*handler)(const JVal &args);
    bool worker_safe; // safe to run on the reader thread (no kauai state)
};

static const ToolDef kTools[] = {
    {
        "screenshot",
        "Capture the 3dmovie main window client area as PNG.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_Screenshot,
        false,
    },
    {
        "click",
        "Click a point in the 3dmovie main window's client coordinates.",
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"x\":{\"type\":\"integer\"},"
        "\"y\":{\"type\":\"integer\"},"
        "\"button\":{\"type\":\"string\",\"enum\":[\"left\",\"right\"]}"
        "},\"required\":[\"x\",\"y\"]}",
        &Tool_Click,
        false,
    },
    {
        "key",
        "Send a Win32 virtual-key down+up to the foreground window.",
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
        "Windows messages between probes. mode=visible (default) waits for "
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
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_GetState,
        false,
    },
    {
        "wait_ms",
        "Pump Windows messages for N ms (capped at 30000) so the GUI can settle.",
        "{\"type\":\"object\","
        "\"properties\":{\"ms\":{\"type\":\"integer\"}},"
        "\"required\":[\"ms\"]}",
        &Tool_WaitMs,
        false,
    },
    {
        "read_crash_log",
        "Return the contents of %TEMP%\\3dmmforever-crash.txt.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_ReadCrashLog,
        false,
    },
    {
        "quit",
        "Post WM_CLOSE to the main window for graceful shutdown.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        &Tool_Quit,
        false,
    },
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
            WriteMessage(MakeResult(req.id, t.handler(targs)));
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
    if (!FParseEnabledFromCommandLine(vwig.pszCmdLine)) return false;
    _enabled.store(true);

    _hStdin = GetStdHandle(STD_INPUT_HANDLE);
    _hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (_hStdin == INVALID_HANDLE_VALUE || _hStdin == NULL)
    {
        // Without an inherited stdin pipe we can't accept requests. Disable.
        _enabled.store(false);
        return false;
    }

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&_gdiplusToken, &gsi, NULL);

    // Install our /RTC error handler so we capture which source file:line
    // tripped the runtime check (e.g. /RTCu uninitialized-variable). The
    // standard MSVC handler shows the modal Debug Error dialog but the dialog
    // doesn't always preserve the file path. We log to AppendCrashLog first,
    // then return 1 so the runtime still shows the dialog (so list_dialogs
    // can also see it and the user can drive the repro interactively).
    _RTC_SetErrorFuncW(&MyRtcErrorFunc);

    _readerThread = std::thread(ReaderThreadProc);
    _initialized.store(true);
    return true;
}

void Shutdown()
{
    if (!_initialized.load()) return;
    _shouldExit.store(true);
    if (_hStdin != INVALID_HANDLE_VALUE)
    {
        // Closing stdin breaks the worker thread out of its blocking ReadFile.
        CancelIoEx(_hStdin, NULL);
    }
    if (_readerThread.joinable()) _readerThread.join();
    if (_gdiplusToken)
    {
        Gdiplus::GdiplusShutdown(_gdiplusToken);
        _gdiplusToken = 0;
    }
    _initialized.store(false);
}

void Drain()
{
    if (!_initialized.load()) return;

    // If the host disconnected, post WM_CLOSE so we exit cleanly rather than
    // running headless forever. The user can also send `quit`.
    if (_shouldExit.load())
    {
        static bool posted = false;
        if (!posted)
        {
            posted = true;
            if (vwig.hwndApp) PostMessage(vwig.hwndApp, WM_CLOSE, 0, 0);
        }
    }

    // Snapshot current queue contents to minimize lock-hold time.
    std::deque<Request> reqs;
    {
        std::lock_guard<std::mutex> lk(_qMutex);
        reqs.swap(_queue);
    }
    for (auto &req : reqs)
    {
        if (req.method == "initialize")
        {
            WriteMessage(MakeResult(req.id, Tool_Initialize(req.params)));
        }
        else if (req.method == "tools/list")
        {
            WriteMessage(MakeResult(req.id, Tool_ListTools()));
        }
        else if (req.method == "tools/call")
        {
            // No SEH guard here: if a tool handler faults, let it bubble up to
            // the Application::Run __try/__except in src/studio/utest.cpp so
            // the existing crash-log path captures it. The host will see the
            // stdio pipe close and can relaunch.
            WriteMessage(MakeResult(req.id, Tool_CallTool(req.params)));
        }
        else if (req.method == "ping")
        {
            WriteMessage(MakeResult(req.id, JVal::MkObj()));
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
}

} // namespace mcp

// Allow the mcp::FParseEnabledFromCommandLine call from WinMain (which is in
// kauai/src/appbwin.cpp) to find this symbol. Declared inline here as a thin
// shim so kauai doesn't need to know mcp:: namespace.
extern "C" int Mcp_FEnabledFromCmdLine(const char *psz)
{
    return mcp::FParseEnabledFromCommandLine(psz) ? 1 : 0;
}

#else // !DEBUG

// Release builds get a no-op stub so kauai's WinMain link still resolves
// the symbol without forcing an #ifdef there too.
extern "C" int Mcp_FEnabledFromCmdLine(const char *)
{
    return 0;
}

#endif // DEBUG
