// Browser compatibility layer for AssaultCube's OpenGL 1.x code.
//
// 1. Stubs for desktop GL calls that WebGL (Emscripten's legacy GL emulation) lacks.
// 2. An immediate-mode (glBegin/glEnd) front end. Emscripten's emulation stores
//    vertex attributes in the order they are called and assumes every vertex has
//    the same layout, while AssaultCube often sets a colour or texcoord once and
//    then emits many vertices, or changes colour mid-batch. Here every vertex is
//    buffered with the full current state and replayed with a uniform layout.
//
// platform.h redirects the engine's gl* immediate calls to the web_gl* functions.
#include <GL/gl.h>
#include <vector>
#include <string.h>


extern "C" {

// ---- stubs -----------------------------------------------------------------
// Display lists are disabled on the web (mdldlist is forced to 0).
GLuint glGenLists(GLsizei) { return 0; }
void glNewList(GLuint, GLenum) {}
void glEndList(void) {}
void glCallList(GLuint) {}
void glDeleteLists(GLuint, GLsizei) {}
// Reading textures back is impossible in WebGL; only used to put the minimap into map screenshots.
void glGetTexImage(GLenum, GLint, GLenum, GLenum, void *) {}

// ---- immediate mode --------------------------------------------------------
struct imvert { float x, y, z, u, v; unsigned char c[4]; };

static std::vector<imvert> verts;
static bool inbatch = false, batchtex = false, batchcolor = false;
static GLenum batchmode = 0;
static float curu = 0, curv = 0;
static unsigned char curcolor[4] = { 255, 255, 255, 255 };

static inline unsigned char tob(float f) { return f <= 0 ? 0 : f >= 1 ? 255 : (unsigned char)(f * 255.0f + 0.5f); }

void web_glBegin(GLenum mode)
{
    inbatch = true;
    batchmode = mode;
    batchtex = batchcolor = false;
    verts.clear();
}

// glPolygonMode(GL_LINE) (wireframe, used by the map editor's cursor and grid) does not exist
// in WebGL: polygons are turned into their edges and drawn as GL_LINES instead.
static GLenum polygonmode = GL_FILL;
static std::vector<imvert> edges;

// Wireframe edges from consecutive glBegin/glEnd pairs (hundreds per frame for the editor grid)
// are collected here and drawn in one call when wireframe mode ends or anything else is drawn.
static std::vector<imvert> linebatch;

static void flushlines()
{
    if(linebatch.empty()) return;
    // the batch was recorded untextured; texturing may have been re-enabled since
    GLboolean tex = glIsEnabled(GL_TEXTURE_2D);
    if(tex) glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINES);
    for(const imvert &v : linebatch)
    {
        glColor4ub(v.c[0], v.c[1], v.c[2], v.c[3]);
        glVertex3f(v.x, v.y, v.z);
    }
    glEnd();
    linebatch.clear();
    if(tex) glEnable(GL_TEXTURE_2D);
    glColor4ub(curcolor[0], curcolor[1], curcolor[2], curcolor[3]);
}

void web_glPolygonMode(GLenum face, GLenum mode) { if(mode != polygonmode) flushlines(); polygonmode = mode; }

static void addedge(int a, int b) { edges.push_back(verts[a]); edges.push_back(verts[b]); }

static bool toedges()
{
    int n = (int)verts.size();
    edges.clear();
    switch(batchmode)
    {
        case GL_QUADS: for(int i = 0; i + 3 < n; i += 4) { addedge(i, i+1); addedge(i+1, i+2); addedge(i+2, i+3); addedge(i+3, i); } break;
        case GL_TRIANGLES: for(int i = 0; i + 2 < n; i += 3) { addedge(i, i+1); addedge(i+1, i+2); addedge(i+2, i); } break;
        case GL_TRIANGLE_STRIP: for(int i = 2; i < n; i++) { addedge(i-2, i-1); addedge(i-1, i); addedge(i, i-2); } break;
        case GL_TRIANGLE_FAN: for(int i = 2; i < n; i++) { addedge(0, i-1); addedge(i-1, i); addedge(i, 0); } break;
        case GL_POLYGON: for(int i = 0; i < n; i++) addedge(i, (i+1) % n); break;
        default: return false;
    }
    return true;
}

void web_glEnd(void)
{
    inbatch = false;
    if(verts.empty()) return;
    GLenum mode = batchmode;
    const std::vector<imvert> *src = &verts;
    if(batchmode == GL_LINES && !batchtex)
    {
        // untextured lines (editor clip boxes, entity markers): defer into the shared line batch
        linebatch.insert(linebatch.end(), verts.begin(), verts.end() - (verts.size() & 1));
        return;
    }
    if(polygonmode == GL_LINE && toedges())
    {
        // untextured wireframe (the editor grid): defer into the shared line batch
        if(!batchtex) { linebatch.insert(linebatch.end(), edges.begin(), edges.end()); return; }
        mode = GL_LINES; src = &edges;
    }
    flushlines();
    if(src->empty()) return;
    glBegin(mode);
    for(const imvert &v : *src)
    {
        if(batchtex) glTexCoord2f(v.u, v.v);
        if(batchcolor) glColor4ub(v.c[0], v.c[1], v.c[2], v.c[3]);
        glVertex3f(v.x, v.y, v.z);
    }
    glEnd();
    // a colour set inside glBegin/glEnd stays current afterwards
    if(batchcolor) glColor4ub(curcolor[0], curcolor[1], curcolor[2], curcolor[3]);
}

static inline void setcolor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    curcolor[0] = r; curcolor[1] = g; curcolor[2] = b; curcolor[3] = a;
    if(inbatch) batchcolor = true;
    else glColor4ub(r, g, b, a);
}

void web_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { setcolor(r, g, b, a); }
void web_glColor3ub(GLubyte r, GLubyte g, GLubyte b) { setcolor(r, g, b, 255); }
void web_glColor4ubv(const GLubyte *c) { setcolor(c[0], c[1], c[2], c[3]); }
void web_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { setcolor(tob(r), tob(g), tob(b), tob(a)); }
void web_glColor3f(GLfloat r, GLfloat g, GLfloat b) { setcolor(tob(r), tob(g), tob(b), 255); }
void web_glColor4fv(const GLfloat *c) { setcolor(tob(c[0]), tob(c[1]), tob(c[2]), tob(c[3])); }
void web_glColor3fv(const GLfloat *c) { setcolor(tob(c[0]), tob(c[1]), tob(c[2]), 255); }

void web_glTexCoord2f(GLfloat u, GLfloat v) { curu = u; curv = v; if(inbatch) batchtex = true; }

void web_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if(!inbatch) return;
    imvert v;
    v.x = x; v.y = y; v.z = z; v.u = curu; v.v = curv;
    v.c[0] = curcolor[0]; v.c[1] = curcolor[1]; v.c[2] = curcolor[2]; v.c[3] = curcolor[3];
    verts.push_back(v);
}
void web_glVertex2f(GLfloat x, GLfloat y) { web_glVertex3f(x, y, 0); }
void web_glVertex3fv(const GLfloat *p) { web_glVertex3f(p[0], p[1], p[2]); }
void web_glVertex2fv(const GLfloat *p) { web_glVertex3f(p[0], p[1], 0); }

}

// ---- client-side vertex arrays ---------------------------------------------
// Emscripten's emulation uploads client arrays starting at vertex 0 and requires
// GL_QUADS draws to start at a multiple of 4. AssaultCube draws world geometry as
// many glDrawArrays(mode, first, count) ranges of one big array, so each draw is
// rebased: the array pointers are shifted by `first` and the draw starts at 0.
extern "C" {

struct arrayptr { GLint size; GLenum type; GLsizei stride; const char *ptr; bool set; };
enum { A_VERTEX, A_COLOR, A_NORMAL, A_TEX0, A_TEX1, A_NUM };
static arrayptr arrays[A_NUM] = {};
static bool enabled[A_NUM] = {};
static int clientunit = 0;

static GLsizei typesize(GLenum t)
{
    switch(t) { case GL_BYTE: case GL_UNSIGNED_BYTE: return 1; case GL_SHORT: case GL_UNSIGNED_SHORT: return 2; default: return 4; }
}
static GLsizei effstride(const arrayptr &a) { return a.stride ? a.stride : a.size * typesize(a.type); }

static void setarray(int i, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
    arrays[i].size = size; arrays[i].type = type; arrays[i].stride = stride; arrays[i].ptr = (const char *)ptr; arrays[i].set = true;
}

static void applyarray(int i, const char *ptr)
{
    const arrayptr &a = arrays[i];
    switch(i)
    {
        case A_VERTEX: glVertexPointer(a.size, a.type, a.stride, ptr); break;
        case A_COLOR: glColorPointer(a.size, a.type, a.stride, ptr); break;
        case A_NORMAL: glNormalPointer(a.type, a.stride, ptr); break;
        case A_TEX0: case A_TEX1:
            glClientActiveTexture(GL_TEXTURE0 + (i - A_TEX0));
            glTexCoordPointer(a.size, a.type, a.stride, ptr);
            break;
    }
}

void web_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { setarray(A_VERTEX, size, type, stride, ptr); glVertexPointer(size, type, stride, ptr); }
void web_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) { setarray(A_COLOR, size, type, stride, ptr); glColorPointer(size, type, stride, ptr); }
void web_glNormalPointer(GLenum type, GLsizei stride, const void *ptr) { setarray(A_NORMAL, 3, type, stride, ptr); glNormalPointer(type, stride, ptr); }
void web_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
    if(clientunit < 2) setarray(A_TEX0 + clientunit, size, type, stride, ptr);
    glTexCoordPointer(size, type, stride, ptr);
}
void web_glClientActiveTexture(GLenum unit) { clientunit = unit - GL_TEXTURE0; glClientActiveTexture(unit); }

void web_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    flushlines();
    if(count <= 0) return;
    if(!first) { glDrawArrays(mode, 0, count); return; }
    for(int i = 0; i < A_NUM; i++) if(arrays[i].set) applyarray(i, arrays[i].ptr + first * effstride(arrays[i]));
    glDrawArrays(mode, 0, count);
    for(int i = 0; i < A_NUM; i++) if(arrays[i].set) applyarray(i, arrays[i].ptr);
    glClientActiveTexture(GL_TEXTURE0 + clientunit);
}

static int capindex(GLenum cap)
{
    switch(cap)
    {
        case GL_VERTEX_ARRAY: return A_VERTEX;
        case GL_COLOR_ARRAY: return A_COLOR;
        case GL_NORMAL_ARRAY: return A_NORMAL;
        case GL_TEXTURE_COORD_ARRAY: return clientunit < 2 ? A_TEX0 + clientunit : -1;
    }
    return -1;
}
void web_glEnableClientState(GLenum cap) { int i = capindex(cap); if(i >= 0) enabled[i] = true; glEnableClientState(cap); }
void web_glDisableClientState(GLenum cap) { int i = capindex(cap); if(i >= 0) enabled[i] = false; glDisableClientState(cap); }

// Indexed draws are expanded into one interleaved, non-indexed vertex buffer:
// the emulation copies the wrong vertex range when indexed data lives in separate arrays.
// The same path batches many world strips into one draw (see web_glMultiDrawArrays).
static std::vector<char> expanded;
static std::vector<unsigned> gathered;

// draws vertices idx[0..count) of the currently enabled client arrays as `mode`
static void drawgathered(GLenum mode, const unsigned *idx, size_t count)
{
    flushlines();
    if(!count) return;
    int offset[A_NUM], bytes[A_NUM], stride = 0;
    for(int i = 0; i < A_NUM; i++)
    {
        if(!enabled[i] || !arrays[i].set) { bytes[i] = 0; continue; }
        bytes[i] = arrays[i].size * typesize(arrays[i].type);
        offset[i] = stride;
        stride += (bytes[i] + 3) & ~3;
    }
    if(!stride) return;
    // the emulation uploads through a fixed-size temporary buffer (GL_MAX_TEMP_BUFFER_SIZE, 2MB)
    size_t maxverts = (size_t)(1800000 / stride);
    if(mode == GL_TRIANGLES) maxverts -= maxverts % 3;
    else if(mode == GL_LINES) maxverts -= maxverts % 2;
    for(size_t done = 0; done < count; )
    {
        size_t n = count - done;
        if(n > maxverts)
        {
            if(mode != GL_TRIANGLES && mode != GL_LINES && mode != GL_POINTS) return; // strips can't be split; never that big
            n = maxverts;
        }
        expanded.resize((size_t)stride * n);
        char *dst = expanded.data();
        for(size_t k = 0; k < n; k++, dst += stride)
        {
            unsigned v = idx[done + k];
            for(int i = 0; i < A_NUM; i++) if(bytes[i])
                memcpy(dst + offset[i], arrays[i].ptr + (size_t)v * effstride(arrays[i]), bytes[i]);
        }
        const char *base = expanded.data();
        for(int i = 0; i < A_NUM; i++) if(bytes[i])
        {
            arrayptr saved = arrays[i];
            arrays[i].stride = stride;
            applyarray(i, base + offset[i]);
            arrays[i] = saved;
        }
        glDrawArrays(mode, 0, (GLsizei)n);
        done += n;
    }
    for(int i = 0; i < A_NUM; i++) if(bytes[i]) applyarray(i, arrays[i].ptr);
    glClientActiveTexture(GL_TEXTURE0 + clientunit);
}

void web_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if(count <= 0) return;
    gathered.resize(count);
    for(GLsizei n = 0; n < count; n++)
        gathered[n] = type == GL_UNSIGNED_SHORT ? ((const unsigned short *)indices)[n]
                    : type == GL_UNSIGNED_INT ? ((const unsigned *)indices)[n]
                    : ((const unsigned char *)indices)[n];
    drawgathered(mode, gathered.data(), count);
}

// The world is drawn as hundreds of short strips per texture. Each emulated draw call costs a lot
// of JavaScript, so all ranges are converted to one triangle list and drawn with a single call.
void web_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei n)
{
    gathered.clear();
    for(GLsizei r = 0; r < n; r++)
    {
        unsigned f = first[r];
        int c = count[r];
        switch(mode)
        {
            case GL_TRIANGLES:
                for(int i = 0; i + 2 < c; i += 3) { gathered.push_back(f+i); gathered.push_back(f+i+1); gathered.push_back(f+i+2); }
                break;
            case GL_TRIANGLE_STRIP:
                for(int i = 0; i + 2 < c; i++)
                {
                    // keep the winding of every other strip triangle consistent (back-face culling)
                    if(i & 1) { gathered.push_back(f+i+1); gathered.push_back(f+i); gathered.push_back(f+i+2); }
                    else { gathered.push_back(f+i); gathered.push_back(f+i+1); gathered.push_back(f+i+2); }
                }
                break;
            case GL_TRIANGLE_FAN:
                for(int i = 1; i + 1 < c; i++) { gathered.push_back(f); gathered.push_back(f+i); gathered.push_back(f+i+1); }
                break;
            case GL_QUADS:
                for(int i = 0; i + 3 < c; i += 4)
                {
                    gathered.push_back(f+i); gathered.push_back(f+i+1); gathered.push_back(f+i+2);
                    gathered.push_back(f+i); gathered.push_back(f+i+2); gathered.push_back(f+i+3);
                }
                break;
            default:
                for(GLsizei k = 0; k < n; k++) web_glDrawArrays(mode, first[k], count[k]);
                return;
        }
    }
    drawgathered(GL_TRIANGLES, gathered.data(), gathered.size());
}

void web_glDrawRangeElements(GLenum mode, GLuint, GLuint, GLsizei count, GLenum type, const void *idx) { web_glDrawElements(mode, count, type, idx); }


void web_glActiveTexture(GLenum unit) { glActiveTexture(unit); }

// The engine looks up extension entry points by their ARB/EXT names.
void *web_getprocaddress(const char *name)
{
    struct { const char *name; void *fn; } procs[] = {
        { "glActiveTextureARB", (void *)web_glActiveTexture },
        { "glClientActiveTextureARB", (void *)web_glClientActiveTexture },
        { "glMultiDrawArraysEXT", (void *)web_glMultiDrawArrays },
        { "glDrawRangeElementsEXT", (void *)web_glDrawRangeElements },
    };
    for(auto &p : procs) if(!strcmp(p.name, name)) return p.fn;
    return NULL;
}

}

// ---- state changes ---------------------------------------------------------
// Deferred lines must be drawn with the matrices and state they were recorded under,
// so every matrix or render-state change draws the pending batch first.
extern "C" {
void web_glMatrixMode(GLenum m) { flushlines(); glMatrixMode(m); }
void web_glLoadIdentity(void) { flushlines(); glLoadIdentity(); }
void web_glPushMatrix(void) { flushlines(); glPushMatrix(); }
void web_glPopMatrix(void) { flushlines(); glPopMatrix(); }
void web_glLoadMatrixf(const GLfloat *m) { flushlines(); glLoadMatrixf(m); }
void web_glMultMatrixf(const GLfloat *m) { flushlines(); glMultMatrixf(m); }
void web_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) { flushlines(); glOrtho(l, r, b, t, n, f); }
void web_glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) { flushlines(); glFrustum(l, r, b, t, n, f); }
void web_glTranslatef(GLfloat x, GLfloat y, GLfloat z) { flushlines(); glTranslatef(x, y, z); }
void web_glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { flushlines(); glRotatef(a, x, y, z); }
void web_glScalef(GLfloat x, GLfloat y, GLfloat z) { flushlines(); glScalef(x, y, z); }
void web_glEnable(GLenum c) { flushlines(); glEnable(c); }
void web_glDisable(GLenum c) { flushlines(); glDisable(c); }
void web_glBlendFunc(GLenum a, GLenum b) { flushlines(); glBlendFunc(a, b); }
void web_glDepthMask(GLboolean f) { flushlines(); glDepthMask(f); }
void web_glDepthFunc(GLenum f) { flushlines(); glDepthFunc(f); }
void web_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { flushlines(); glViewport(x, y, w, h); }
void web_glClear(GLbitfield m) { flushlines(); glClear(m); }
}

// called once per frame before the buffer swap
extern "C" void web_glflushpending(void) { flushlines(); }
