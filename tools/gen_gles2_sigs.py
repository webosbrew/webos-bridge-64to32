#!/usr/bin/env python3
import os, re, urllib.request

GL2EXT_H = "gl2ext.h"
URL = "https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/refs/heads/main/api/GLES2/gl2ext.h"

OUT_H = "gles2_generated.h"
OUT_C = "gles2_generated.c"
OUT_PROXY = "gles2_invoke_dynamic.c"

# Functions missing
BLACKLIST = {
    "glMaxActiveShaderCoresARM",
    "glDrawMeshTasksEXT",
    "glDrawMeshTasksIndirectEXT",
    "glMultiDrawMeshTasksIndirectEXT",
    "glMultiDrawMeshTasksIndirectCountEXT",
    "glCreateShaderProgramvEXT",
    "glGenSamplers",
    "glDeleteSamplers",
    "glIsSampler",
    "glBindSampler",
    "glSamplerParameteri",
    "glSamplerParameteriv",
    "glSamplerParameterf",
    "glSamplerParameterfv",
    "glGetSamplerParameteriv",
    "glGetSamplerParameterfv",
    "glTextureBarrierNV",
    "glNamedFramebufferTextureMultiviewOVR",
}

PACK_MAP = {
    "GLenum": "aw_u32",
    "GLuint": "aw_u32",
    "GLbitfield": "aw_u32",
    "GLboolean": "aw_u32",
    "GLint": "aw_i32",
    "GLsizei": "aw_i32",
    "GLintptr": "aw_i32",
    "GLsizeiptr": "aw_i32",
    "GLfixed": "aw_i32",
    "GLfloat": "aw_f32",
    "GLclampf": "aw_f32",
    "GLdouble": "aw_u64",
}

POINTER_TYPEDEFS = {
    "GLeglImageOES",
    "GLeglClientBufferEXT",
    "GLsync",
    "GLDEBUGPROCKHR",
    "GLVULKANPROCNV",
}

UNPACK_MAP = {
    "GLenum": ("ar_u32", "GLenum"),
    "GLuint": ("ar_u32", "GLuint"),
    "GLbitfield": ("ar_u32", "GLbitfield"),
    "GLboolean": ("ar_u32", "GLboolean"),
    "GLint": ("ar_i32", "GLint"),
    "GLsizei": ("ar_i32", "GLsizei"),
    "GLintptr": ("ar_i32", "GLintptr"),
    "GLsizeiptr": ("ar_i32", "GLsizeiptr"),
    "GLfixed": ("ar_i32", "GLfixed"),
    "GLfloat": ("ar_f32", "GLfloat"),
    "GLclampf": ("ar_f32", "GLclampf"),
    "GLdouble": ("ar_u64", "GLdouble"),
}

INVALID_TYPES = {
    "GLdouble",
    "GLVULKANPROCNV",
    "GLint64EXT",
    "GLuint64EXT",
}

PROTO_RE = re.compile(r'^GL_APICALL\s+(.+?)\s+GL_APIENTRY\s+(\w+)\s*\((.*?)\);')

def download_gl2ext():
    if not os.path.exists(GL2EXT_H):
        urllib.request.urlretrieve(URL, GL2EXT_H)

def normalize_type(t: str) -> str:
    return " ".join(t.split())

def base_type(t: str) -> str:
    # strip consts but keep stars
    return normalize_type(t.replace("const", "")).strip()

def parse_param(p: str):
    p = p.strip().rstrip(',')
    if not p:
        return None
    # split "type ... name" by last identifier
    m = re.match(r'^(.*?)(\w+)$', p)
    if not m:
        return None
    ptype = m.group(1).strip()
    pname = m.group(2).strip()
    return ptype, pname

def is_valid_gles_function(name, ret, params):
    # kill vendor names in function name
    if any(x in name for x in ("NV", "AMD", "INTEL", "APPLE")):
        return False

    # kill bad return types
    if base_type(ret) in INVALID_TYPES:
        return False

    # kill bad param types
    for t, _ in params:
        if base_type(t) in INVALID_TYPES:
            return False

    if name in (
        "glMatrixMultTransposedEXT",
        "glMatrixLoadTransposedEXT",
        "glMatrixLoaddEXT",
        "glMatrixMultdEXT",
    ):
        return False

    return True

def parse_gl2ext():
    funcs = []
    with open(GL2EXT_H, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            m = PROTO_RE.match(line)
            if not m:
                continue
            ret = m.group(1).strip()
            name = m.group(2).strip()
            if name in BLACKLIST:
                continue
            params_str = m.group(3).strip()
            params = []
            if params_str and params_str != "void":
                parts = [p.strip() for p in params_str.split(",") if p.strip()]
                for p in parts:
                    parsed = parse_param(p)
                    if parsed:
                        params.append(parsed)
            if is_valid_gles_function(name, ret, params):
                funcs.append((name, ret, params))
    return funcs

def emit():
    funcs = parse_gl2ext()

    # header
    with open(OUT_H, "w") as h:
        h.write(r'''
#pragma once

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

typedef struct
{
    const char *name;
    void *dispatch;
    uint32_t idx;
    uint8_t resolved;
} ProcEntry;

extern ProcEntry proc_table[];

ProcEntry *find_proc(const char *name);
''')

    # stub side
    with open(OUT_C, "w") as c:
        c.write(r'''
#define LOG_PREFIX "[stub/dynamic_gl]"
#include <stdint.h>
#include <string.h>
#include "gles2_generated.h"
#include "../stub/bridge_core.h"
#include "../bridge/shared_util.h"

''')
        for name, ret, params in funcs:
            sig = [f"{t} {n}" for t, n in params]
            c.write(f'''
static {ret} dispatch_{name}(
    {", ".join(sig) if sig else "void"}
)
{{
    BRIDGE_BEGIN();

    BridgeCtrl *C = BRIDGE_CTRL();
    C->opcode = OP_InvokeDynamic;

    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

#ifdef DEBUG_EGL_GETPROC
    log_console("dispatch_{name}");
#endif

    ProcEntry *pe = find_proc("{name}");

    if (!pe)
    {{
#ifdef DEBUG_EGL_GETPROC
        log_console("find_proc: ERROR unknown proc: {name}");
#endif
        return ({ret})0;
    }}

    if (!pe->idx)
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
                if "*" in tnorm or b in POINTER_TYPEDEFS:
                    c.write(f'''
    aw_u64(&W, (uint64_t)(uintptr_t){n});
''')
                else:
                    fn = PACK_MAP.get(b, "aw_u64")
                    c.write(f'''
    {fn}(&W, {n});
''')
            c.write('''
    C->args_len = W.pos;
''')
            if ret == "void":
                c.write('''
    BRIDGE_SEND_VOID();
}
''')
            else:
                c.write(f'''
    return ({ret})BRIDGE_SEND_CALL();
}}
''')
        c.write('''
ProcEntry proc_table[] =
{
''')
        for idx, (name, ret, params) in enumerate(funcs):
            c.write(f'''
    {{"{name}", dispatch_{name}, {idx}}},
''')
        c.write('''
    {NULL, NULL, 0}
};

ProcEntry *find_proc(const char *name)
{
#ifdef DEBUG_EGL_GETPROC
    log_console("find_proc: called with name=%s", name);
#endif
    ProcEntry *p = proc_table;

    while (p->name)
    {
        if (!strcmp(p->name, name))
        {
#ifdef DEBUG_EGL_GETPROC
            log_console("find_proc: found proc: %s", name);
#endif
            return p;
        }

        p++;
    }

#ifdef DEBUG_EGL_GETPROC
    log_console("find_proc: name=%s not found", name);
#endif
    return NULL;
}
''')

    # proxy side
    with open(OUT_PROXY, "w") as p:
        p.write(r'''
#define GL_GLEXT_PROTOTYPES
#define LOG_PREFIX "[proxy/dynamic_gl]"
#include <stdint.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include "gles2_generated.h"
#include "gles_bridge_protocol.h"
#include "../bridge/shared_util.h"

void h_InvokeDynamic(
    BridgeCtrl *C,
    uint8_t *D)
{
    ArgReader r =
        ar_init(
            C->args,
            C->args_len
        );

    uint32_t idx =
        ar_u32(&r);

#ifdef DEBUG_EGL_GETPROC
    log_console("h_InvokeDynamic - idx: %d", idx);
#endif

    (void)D;

    switch (idx)
    {
''')
        for idx, (name, ret, params) in enumerate(funcs):
            # typedef for function pointer
            sig = [f"{t} {n}" for t, n in params]
            p.write(f'''
        case {idx}:
        {{
#ifdef DEBUG_EGL_GETPROC
            log_console("h_InvokeDynamic - invoking {idx}:{name}");
#endif
            typedef {ret} (GL_APIENTRYP PFN_{name})({", ".join(sig) if sig else "void"});
            static PFN_{name} pfn = NULL;

            if (!pfn)
            {{
                pfn = (PFN_{name}) eglGetProcAddress("{name}");
                if (!pfn)
                {{
                    /* function not available on this driver */
#ifdef DEBUG_EGL_GETPROC
                    log_console("h_InvokeDynamic - {idx}:{name} - not found via eglGetProcAddress");
#endif
                    break;
                }}
            }}
''')
            call_args = []
            for t, n in params:
                tnorm = normalize_type(t)
                b = base_type(tnorm)
                if "*" in tnorm or b in POINTER_TYPEDEFS:
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
                    p.write(f'            pfn(' + ', '.join(call_args) + ');\n')
                else:
                    p.write(f'            pfn();\n')
            else:
                if call_args:
                    p.write(
                        f'            C->result = (uint64_t)(uintptr_t)pfn('
                        + ', '.join(call_args) + ');\n'
                    )
                else:
                    p.write(
                        f'            C->result = (uint64_t)(uintptr_t)pfn();\n'
                    )
            p.write('            break;\n        }\n')
        p.write(r'''
        default:
        {
            /* unknown idx */
            break;
        }
    }
}
''')

    print(f"Generated {OUT_H}, {OUT_C}, {OUT_PROXY}")

download_gl2ext()
emit()
