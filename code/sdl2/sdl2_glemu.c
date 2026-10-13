/*
Emscripten-only GL compatibility shims.

Emscripten's legacy GL emulation (-sLEGACY_GL_EMULATION=1) supplies the fixed
function entry points Q3 needs (glTexEnvf, glPushMatrix, glLoadMatrixf,
glOrtho, glPolygonMode, glAlphaFunc, glClipPlane, glBegin/glEnd,
glVertexPointer, glTexCoordPointer, glColor4ubv). It does not supply these two:

  glArrayElement - emits one vertex from the enabled client state arrays.
      Q3 reaches it only through R_DrawElements with r_primitives == 1. The
      r_primitives == 3 path (R_ArrayElementDiscrete) emits colour, texcoord
      and vertex explicitly from tess, which is why GLimp_Init forces
      r_primitives 3 on this platform. The symbol is never called.

  glCallList - Q3 1.32 never creates a display list (no glNewList /
      glEndList anywhere in the renderer) and surf->listNum is never
      assigned, so tr_surface.c calls glCallList(0). A no-op is the correct
      behaviour, not a stub.

They are declared in renderer/qgl.h as function pointers and mapped to the
plain names by qgl_linked.h, so the linker needs them.
*/

#ifdef __EMSCRIPTEN__

#include <GL/gl.h>
#include "../renderer/qgl.h"

void glArrayElement(GLint i) {
	(void)i;
}

void glCallList(GLuint list) {
	(void)list;
}

void Ems_GLDrawBufferNoop(GLenum mode) {
	(void)mode;
}

/*
Emscripten's legacy GL emulation has no glMultiTexCoord2fARB, so
SDL_GL_GetProcAddress returns 0. R_ArrayElementDiscrete calls qglMultiTexCoord2fARB
for every vertex while a texture unit other than 0 is current, and a null
function pointer traps the wasm main loop. The emulation routes glTexCoord*
through the client active texture, so select the unit then emit the coordinate.
*/
void Ems_MultiTexCoord2fARB(GLenum texture, GLfloat s, GLfloat t) {
	qglClientActiveTextureARB(texture);
	qglTexCoord2f(s, t);
}

/*
qglTexParameterfv is redirected here on WebGL. GL_TEXTURE_BORDER_COLOR is not a
WebGL texParameter name; passing it raises INVALID_ENUM and floods the console.
Q3 sets it once, for the fog grid, and the comment at that call site says
clamp-to-edge is the behaviour it wants, so only that name is dropped.
*/
void Ems_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
	if (pname == GL_TEXTURE_BORDER_COLOR) {
		return;
	}
	glTexParameterfv(target, pname, params);
}

#endif
