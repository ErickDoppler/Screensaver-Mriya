/* Tiny OpenGL 3.3 core loader. GL 1.1 entry points come from the system
 * library (opengl32 / libGL); everything newer is fetched through SDL.
 * SDL_opengl.h ships the Khronos headers, so the PFN typedefs are standard. */
#ifndef MR_GL_LOADER_H
#define MR_GL_LOADER_H
#include <SDL3/SDL_opengl.h>

/* glActiveTexture, glTexImage3D and glBlendEquation are GL 1.2-1.4: SDL's
 * gl.h already declares prototypes for them, yet opengl32.dll does not export
 * them, so each gets a prefixed pointer and a macro. The loader strips the
 * "bh_" prefix when looking the symbol up. */
#define BH_GL_FUNCS(X) \
    X(PFNGLACTIVETEXTUREPROC,          bh_glActiveTexture) \
    X(PFNGLCREATESHADERPROC,           glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,           glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,          glCompileShader) \
    X(PFNGLGETSHADERIVPROC,            glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,       glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC,           glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,          glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,           glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,            glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC,           glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,      glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC,             glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC,          glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC,     glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC,              glUniform1i) \
    X(PFNGLUNIFORM1IVPROC,             glUniform1iv) \
    X(PFNGLUNIFORM1FPROC,              glUniform1f) \
    X(PFNGLUNIFORM2FPROC,              glUniform2f) \
    X(PFNGLUNIFORM3FPROC,              glUniform3f) \
    X(PFNGLUNIFORM4FPROC,              glUniform4f) \
    X(PFNGLUNIFORM4FVPROC,             glUniform4fv) \
    X(PFNGLUNIFORMMATRIX4FVPROC,       glUniformMatrix4fv) \
    X(PFNGLUNIFORMMATRIX3FVPROC,       glUniformMatrix3fv) \
    X(PFNGLGENVERTEXARRAYSPROC,        glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,        glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC,     glDeleteVertexArrays) \
    X(PFNGLGENFRAMEBUFFERSPROC,        glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC,        glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,   glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC,     glDeleteFramebuffers) \
    X(PFNGLBLENDFUNCSEPARATEPROC,      glBlendFuncSeparate) \
    X(PFNGLGENBUFFERSPROC,             glGenBuffers) \
    X(PFNGLBINDBUFFERPROC,             glBindBuffer) \
    X(PFNGLBUFFERDATAPROC,             glBufferData) \
    X(PFNGLDELETEBUFFERSPROC,          glDeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,    glVertexAttribPointer) \
    X(PFNGLBUFFERSUBDATAPROC,          glBufferSubData) \
    X(PFNGLUNIFORM1FVPROC,             glUniform1fv) \
    X(PFNGLUNIFORM2FVPROC,             glUniform2fv) \
    X(PFNGLUNIFORM3FVPROC,             glUniform3fv) \
    X(PFNGLGENERATEMIPMAPPROC,         glGenerateMipmap) \
    X(PFNGLGENQUERIESPROC,             glGenQueries) \
    X(PFNGLDELETEQUERIESPROC,          glDeleteQueries) \
    X(PFNGLBEGINQUERYPROC,             glBeginQuery) \
    X(PFNGLENDQUERYPROC,               glEndQuery) \
    X(PFNGLGETQUERYOBJECTUIVPROC,      glGetQueryObjectuiv) \
    X(PFNGLGETQUERYOBJECTUI64VPROC,    glGetQueryObjectui64v) \
    X(PFNGLTEXIMAGE3DPROC,             bh_glTexImage3D) \
    X(PFNGLFRAMEBUFFERTEXTURELAYERPROC, glFramebufferTextureLayer) \
    X(PFNGLDRAWBUFFERSPROC,            glDrawBuffers) \
    X(PFNGLBLITFRAMEBUFFERPROC,        glBlitFramebuffer) \
    X(PFNGLTEXIMAGE2DMULTISAMPLEPROC,  glTexImage2DMultisample) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLDRAWARRAYSINSTANCEDPROC,    glDrawArraysInstanced) \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC,  glDrawElementsInstanced) \
    X(PFNGLVERTEXATTRIBDIVISORPROC,    glVertexAttribDivisor) \
    X(PFNGLVERTEXATTRIBIPOINTERPROC,   glVertexAttribIPointer) \
    X(PFNGLUNIFORM2IPROC,              glUniform2i) \
    X(PFNGLUNIFORMMATRIX2FVPROC,       glUniformMatrix2fv) \
    X(PFNGLBLENDEQUATIONPROC,          bh_glBlendEquation) \
    X(PFNGLCLEARBUFFERFVPROC,          glClearBufferfv) \
    X(PFNGLGENRENDERBUFFERSPROC,       glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC,       glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC,    glRenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC,    glDeleteRenderbuffers)

#define BH_GL_DECLARE(type, name) extern type name;
BH_GL_FUNCS(BH_GL_DECLARE)
#undef BH_GL_DECLARE
#define glActiveTexture bh_glActiveTexture
#define glTexImage3D bh_glTexImage3D
#define glBlendEquation bh_glBlendEquation

/* Returns 0 on success, or the name of the first missing function. */
const char *gl_load_functions(void);
/* Logs and clears any pending GL error; returns 1 if there was one. */
int gl_check(const char *where);
#endif
