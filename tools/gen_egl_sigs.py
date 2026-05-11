#!/usr/bin/env python3
import os, re, urllib.request

print("RUNNING IN:", os.getcwd())

EGL_EXT_H = "eglext.h"
URL = "https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/main/api/EGL/eglext.h"

OUT_H = "egl_generated.h"
OUT_C = "egl_generated.c"
OUT_PROXY = "egl_invoke_dynamic.c"

BLACKLIST = set()

PACK_MAP = {
    "EGLint": "aw_i32",
    "EGLBoolean": "aw_u32",
    "EGLenum": "aw_u32",
    "EGLAttrib": "aw_u64",
    "EGLAttribKHR": "aw_u64",
    "EGLTimeKHR": "aw_u64",
    "EGLTime": "aw_u64",
    "EGLuint64KHR": "aw_u64",
    "EGLnsecsANDROID": "aw_u64",
}

UNPACK_MAP = {
    "EGLint": ("ar_i32", "EGLint"),
    "EGLBoolean": ("ar_u32", "EGLBoolean"),
    "EGLenum": ("ar_u32", "EGLenum"),
    "EGLAttrib": ("ar_u64", "EGLAttrib"),
    "EGLAttribKHR": ("ar_u64", "EGLAttribKHR"),
    "EGLTimeKHR": ("ar_u64", "EGLTimeKHR"),
    "EGLTime": ("ar_u64", "EGLTime"),
    "EGLuint64KHR": ("ar_u64", "EGLuint64KHR"),
    "EGLnsecsANDROID": ("ar_u64", "EGLnsecsANDROID"),
}

PROTO_RE = re.compile(
    r'^EGLAPI\s+(.+?)\s+EGLAPIENTRY\s+(\w+)\s*\((.*?)\);'
)

def download_eglext():
    if not os.path.exists(EGL_EXT_H):
        urllib.request.urlretrieve(URL, EGL_EXT_H)

def normalize_type(t: str) -> str:
    return " ".join(t.split())

def base_type(t: str) -> str:
    return normalize_type(t.replace("const", "")).strip()

def parse_param(p: str):
    p = p.strip().rstrip(',')
    if not p:
        return None
    m = re.match(r'^(.*?)(\w+)$', p)
    if not m:
        return None
    ptype = m.group(1).strip()
    pname = m.group(2).strip()
    return ptype, pname

def is_valid_egl_function(name, ret, params):
    if name in BLACKLIST:
        return False
    if not any(suffix in name for suffix in (
        "KHR","EXT","NV","ANDROID","WL","HI","MESA","IMG","QNX","ARM"
    )):
        return False
    return True

def is_pointer_type(tnorm: str) -> bool:
    b = base_type(tnorm)
    if "*" in tnorm:
        return True
    if b.startswith("EGL"):
        # All opaque EGL handles + function pointer typedefs
        return True
    return False

def parse_eglext():
    funcs = []
    with open(EGL_EXT_H, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            m = PROTO_RE.match(line)
            if not m:
                continue
            ret = m.group(1).strip()
            name = m.group(2).strip()
            params_str = m.group(3).strip()
            params = []
            if params_str and params_str != "void":
                parts = [p.strip() for p in params_str.split(",") if p.strip()]
                for p in parts:
                    parsed = parse_param(p)
                    if parsed:
                        params.append(parsed)
            if is_valid_egl_function(name, ret, params):
                funcs.append((name, ret, params))
    return funcs

def emit():
    funcs = parse_eglext()

    # header
    with open(OUT_H, "w") as h:
        h.write(r'''
#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef struct
{
    const char *name;
    void *dispatch;
    uint32_t idx;
    uint8_t resolved;
} EGLProcEntry;

extern EGLProcEntry egl_proc_table[];

EGLProcEntry *egl_find_proc(const char *name);
''')

    # stub side
    with open(OUT_C, "w") as c:
        c.write(r'''
#define LOG_PREFIX "[stub/dynamic_egl]"
#include "egl_generated.h"
#include "../stub/bridge_core.h"
#include "../bridge/shared_util.h"
''')

        for name, ret, params in funcs:
            sig = [f"{t} {n}" for t, n in params]
            c.write(f'''
static {ret} dispatch_{name}({", ".join(sig) if sig else "void"})
{{
    BRIDGE_BEGIN();
    BridgeCtrl *C = BRIDGE_CTRL();
    C->opcode = OP_InvokeDynamicEGL;

    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

#ifdef DEBUG_EGL_GETPROC
    log_console("dispatch_{name}");
#endif

    EGLProcEntry *pe = egl_find_proc("{name}");
    if (!pe || !pe->idx)
    {{
#ifdef DEBUG_EGL_GETPROC
        log_console("{name}: idx not initialized");
#endif
        return ({ret})0;
    }}

    aw_u32(&W, pe->idx);
''')

            for t, n in params:
                tnorm = normalize_type(t)
                b = base_type(tnorm)
                if is_pointer_type(tnorm):
                    c.write(f"    aw_u64(&W, (uint64_t)(uintptr_t){n});\n")
                else:
                    fn = PACK_MAP.get(b, "aw_u64")
                    c.write(f"    {fn}(&W, {n});\n")

            c.write("    C->args_len = W.pos;\n")

            if ret == "void":
                c.write("    BRIDGE_SEND_VOID();\n}\n")
            else:
                c.write(f"    return ({ret})BRIDGE_SEND_CALL();\n}}\n")

        c.write("\nEGLProcEntry egl_proc_table[] = {\n")
        for idx, (name, _, _) in enumerate(funcs):
            c.write(f'    {{"{name}", dispatch_{name}, {idx}}},\n')
        c.write("    {NULL, NULL, 0}\n};\n")

        c.write(r'''
EGLProcEntry *egl_find_proc(const char *name)
{
#ifdef DEBUG_EGL_GETPROC
    log_console("egl_find_proc: %s", name);
#endif
    EGLProcEntry *p = egl_proc_table;
    while (p->name)
    {
        if (!strcmp(p->name, name))
        {
#ifdef DEBUG_EGL_GETPROC
            log_console("egl_find_proc: found %s", name);
#endif
            return p;
        }
        p++;
    }
#ifdef DEBUG_EGL_GETPROC
    log_console("egl_find_proc: NOT FOUND %s", name);
#endif
    return NULL;
}
''')

    # proxy side
    with open(OUT_PROXY, "w") as p:
        p.write(r'''
#define EGL_EGLEXT_PROTOTYPES
#define LOG_PREFIX "[proxy/dynamic_egl]"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "egl_generated.h"
#include "gles_bridge_protocol.h"
#include "../bridge/shared_util.h"

void h_InvokeDynamicEGL(BridgeCtrl *C, uint8_t *D)
{
    ArgReader r = ar_init(C->args, C->args_len);
    uint32_t idx = ar_u32(&r);
    (void)D;

#ifdef DEBUG_EGL_GETPROC
    log_console("h_InvokeDynamicEGL idx=%u", idx);
#endif

    switch (idx)
    {
''')

        for idx, (name, ret, params) in enumerate(funcs):
            sig = [f"{t} {n}" for t, n in params]
            p.write(f'''
        case {idx}:
        {{
#ifdef DEBUG_EGL_GETPROC
            log_console("invoke {idx}:{name}");
#endif
            typedef {ret} (EGLAPIENTRYP PFN_{name})({", ".join(sig) if sig else "void"});
            static PFN_{name} pfn = NULL;

            if (!pfn)
            {{
                pfn = (PFN_{name})eglGetProcAddress("{name}");
#ifdef DEBUG_EGL_GETPROC
                log_console("eglGetProcAddress({name}) -> %p", pfn);
#endif
                if (!pfn)
                    break;
            }}
''')

            call_args = []
            for t, n in params:
                tnorm = normalize_type(t)
                b = base_type(tnorm)
                if is_pointer_type(tnorm):
                    call_args.append(f'({tnorm})ar_u64(&r)')
                else:
                    unpack = UNPACK_MAP.get(b)
                    if unpack is None:
                        call_args.append(f'({tnorm})ar_u64(&r)')
                    else:
                        fn_ar, _ = unpack
                        call_args.append(f'({tnorm}){fn_ar}(&r)')

            if ret == "void":
                if call_args:
                    p.write(f"            pfn({', '.join(call_args)});\n")
                else:
                    p.write("            pfn();\n")
            else:
                if call_args:
                    p.write(
                        "            C->result = "
                        "(uint64_t)(uintptr_t)pfn(" + ", ".join(call_args) + ");\n"
                    )
                else:
                    p.write(
                        "            C->result = "
                        "(uint64_t)(uintptr_t)pfn();\n"
                    )

            p.write("            break;\n        }\n")

        p.write(r'''
        default:
#ifdef DEBUG_EGL_GETPROC
            log_console("unknown idx=%u", idx);
#endif
            break;
    }
}
''')

    print(f"Generated {OUT_H}, {OUT_C}, {OUT_PROXY}")

download_eglext()
emit()
