#include "modules.h"
#include "natives.h"
#include "audio.h"
#include "ir.h"
#include "../uforth.h"
#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>

// ─── Source log ───────────────────────────────────────────────────────────────

#define MAX_SOURCE_LOG 64

struct WordEntry {
    char line[TIB_SIZE];
};

static WordEntry _sourceLog[MAX_SOURCE_LOG];
static int       _sourceLogN = 0;

void forth_modules_init() {
    _sourceLogN = 0;
}

void forth_modules_deinit() {
    _sourceLogN = 0;
}

void forth_log_word(const char *line) {
    if (_sourceLogN >= MAX_SOURCE_LOG) return;
    strncpy(_sourceLog[_sourceLogN++].line, line, TIB_SIZE - 1);
    _sourceLog[_sourceLogN - 1].line[TIB_SIZE - 1] = '\0';
}

// ─── Filesystem helpers ───────────────────────────────────────────────────────

static FS *_pickFS() {
    if (SD.begin()) return &SD;
    return &LittleFS;
}

// foo.bar.baz → /forth/foo/bar/baz.f
static void _libToPath(const char *lib, char *out, size_t sz) {
    size_t off = 0;
    const char *base = "/forth/";
    size_t blen = strlen(base);
    if (blen >= sz) return;
    memcpy(out, base, blen);
    off = blen;
    for (const char *p = lib; *p && off < sz - 3; p++)
        out[off++] = (*p == '.') ? '/' : *p;
    if (off + 2 < sz) { out[off++] = '.'; out[off++] = 'f'; }
    out[off] = '\0';
}

// create all intermediate directories for a file path
static void _mkdirp(FS *fs, const char *filepath) {
    char tmp[128];
    strncpy(tmp, filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            fs->mkdir(tmp);
            *p = '/';
        }
    }
}

// ─── Native module registry ───────────────────────────────────────────────────

struct NativeModule {
    const char *name;
    void (*register_fn)();
};

static const NativeModule _native_modules[] = {
    { "br.audio", forth_register_audio },
    { "br.ir",    forth_register_ir    },
};

bool forth_register_load(const char *lib) {
    for (size_t i = 0; i < sizeof(_native_modules) / sizeof(_native_modules[0]); i++) {
        if (strcmp(lib, _native_modules[i].name) == 0) {
            _native_modules[i].register_fn();
            return true;
        }
    }
    return false;
}

// ─── store ────────────────────────────────────────────────────────────────────

static void fn_store() {
    char *w1 = uforth_next_word();
    char lib[64];
    strncpy(lib, w1, uforth_iram->tibwordlen);
    lib[uforth_iram->tibwordlen] = '\0';

    // strip trailing dot if present
    int libLen = strlen(lib);
    if (libLen > 0 && lib[libLen - 1] == '.') lib[--libLen] = '\0';

    char path[128];
    _libToPath(lib, path, sizeof(path));

    FS *fs = _pickFS();
    _mkdirp(fs, path);

    File f = fs->open(path, FILE_WRITE);
    if (!f) { forth_output("store: open failed\n"); return; }

    char msg[128];
    snprintf(msg, sizeof(msg), "store %s -> %s\n", lib, path);
    forth_output(msg);

    // match ": lib.name body ;" — strip lib. from name
    char prefixDot[66];
    snprintf(prefixDot, sizeof(prefixDot), "%s.", lib);
    int pdLen = strlen(prefixDot);
    int count = 0;

    for (int i = 0; i < _sourceLogN; i++) {
        const char *line = _sourceLog[i].line;
        if (strncmp(line, ": ", 2) != 0) continue;
        const char *name = line + 2;
        if (strncmp(name, prefixDot, pdLen) != 0) continue;
        const char *shortName = name + pdLen;
        const char *bodyStart = shortName;
        while (*bodyStart && *bodyStart != ' ') bodyStart++;
        f.print(": ");
        f.write((const uint8_t *)shortName, bodyStart - shortName);
        f.println(bodyStart);
        // log word name being saved
        char nameBuf[64];
        int nLen = bodyStart - shortName;
        strncpy(nameBuf, shortName, nLen);
        nameBuf[nLen] = '\0';
        snprintf(msg, sizeof(msg), "  %s\n", nameBuf);
        forth_output(msg);
        count++;
    }

    f.close();
    snprintf(msg, sizeof(msg), "%d word(s) saved\n", count);
    forth_output(msg);
}

// ─── load (shared implementation) ────────────────────────────────────────────

static void _doLoad(const char *lib) {
    bool r = forth_register_load(lib);
    if(r){
      forth_output("found native\n");
    }

    char path[128];
    _libToPath(lib, path, sizeof(path));

    FS *fs = _pickFS();
    File f = fs->open(path, FILE_READ);
    if (!f) return;

    String line;
    auto compileLine = [&](const String &l) {
        if (!l.startsWith(": ")) return;
        String newLine = ": " + String(lib) + "." + l.substring(2);
        char buf[TIB_SIZE];
        strncpy(buf, newLine.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (uforth_interpret(buf) == UFORTH_OK)
            forth_log_word(buf);
    };

    while (f.available()) {
        char c = (char)f.read();
        if (c == '\n') { line.trim(); compileLine(line); line = ""; }
        else line += c;
    }
    line.trim();
    compileLine(line);
    f.close();

    // call lib._init if it exists (ignore error)
    char initBuf[TIB_SIZE];
    snprintf(initBuf, sizeof(initBuf), "%s._init", lib);
    uforth_interpret(initBuf);
}

static void fn_load() {
    char *w1 = uforth_next_word();
    char lib[64];
    strncpy(lib, w1, uforth_iram->tibwordlen);
    lib[uforth_iram->tibwordlen] = '\0';
    _doLoad(lib);
}

// ─── loada — load + alias ─────────────────────────────────────────────────────

static forth_add_alias_fn _add_alias_fn = nullptr;

void forth_set_add_alias(forth_add_alias_fn fn) { _add_alias_fn = fn; }

static void fn_loada() {
    char *w1 = uforth_next_word();
    char lib[64];
    strncpy(lib, w1, uforth_iram->tibwordlen);
    lib[uforth_iram->tibwordlen] = '\0';

    char *w2 = uforth_next_word();
    char alias[64];
    strncpy(alias, w2, uforth_iram->tibwordlen);
    alias[uforth_iram->tibwordlen] = '\0';

    _doLoad(lib);

    // register alias: alias. -> lib.  (strip trailing dots first)
    if (_add_alias_fn && strlen(alias) > 0) {
        int alen = strlen(alias); if (alen > 0 && alias[alen-1] == '.') alias[--alen] = '\0';
        int llen = strlen(lib);   if (llen > 0 && lib[llen-1]   == '.') lib[--llen]   = '\0';
        char from[66], to[66];
        snprintf(from, sizeof(from), "%s.", alias);
        snprintf(to,   sizeof(to),   "%s.", lib);
        _add_alias_fn(from, to);
    }
}

// ─── registration ─────────────────────────────────────────────────────────────

void forth_register_modules() {
    forth_register("store", fn_store);
    forth_register("load",  fn_load);
    forth_register("loada", fn_loada);
}
