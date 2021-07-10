#include <assert.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

static int width = 512;
static int height = 512;
static EGLDisplay *egl_display;
static EGLSurface *egl_surface;
static EGLContext *egl_context;
struct {
	GLuint pos;
	GLuint col;
	GLuint tex;
	GLint utexture_y;
	GLint utexture_uv;
} gl;

static const char *vert_shader_text =
	"attribute vec4 in_Position;			\n"
	"attribute vec4 in_Color;			\n"
	"attribute vec2 in_TexCoord;			\n"
	"						\n"
	"varying vec4 vColor;				\n"
	"varying vec2 vTexCoord;			\n"
	"						\n"
	"void main() {					\n"
	"  gl_Position = in_Position;			\n"
	"  vColor = in_Color;				\n"
	"  vTexCoord = in_TexCoord;			\n"
	"}						\n";

static const char *frag_shader_text =
	"precision mediump float;			\n"
	"						\n"
	"varying vec4 vColor;				\n"
	"varying vec2 vTexCoord;			\n"
	"						\n"
        "uniform sampler2D uTexY;			\n"
        "uniform sampler2D uTexUV;			\n"
	"						\n"
	"void main() {					\n"
	"  float r, g, b, y, u, v;			\n"
	"  y = texture2D(uTexY, vTexCoord).x;		\n"
	"  u = texture2D(uTexUV, vTexCoord).x - 0.5;	\n"
	"  v = texture2D(uTexUV, vTexCoord).y - 0.5;	\n"
	"  r = y + 1.13983*v;				\n"
	"  g = y - 0.39465*u - 0.58060*v;		\n"
	"  b = y + 2.03211*u;				\n"
	"  gl_FragColor = vec4(r, g, b, 1.0);		\n"
	"}						\n";

GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
	printf("error 0x%x | %s:%d\n", errorCode, file, line);
    }
    return errorCode;
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)

static GLuint
create_shader(const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %.*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void
init_gl(void)
{
	extern const uint32_t raw_512x512_nv12[];
	uint8_t *luma = &((uint8_t *)raw_512x512_nv12)[0];
	uint8_t *chroma = &((uint8_t *)raw_512x512_nv12)[width * height];
	GLuint frag, vert;
	GLuint program;
	GLint status;

	frag = create_shader(frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(vert_shader_text, GL_VERTEX_SHADER);

	program = glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%.*s\n", len, log);
		exit(1);
	}

	glUseProgram(program);

	gl.pos = 0;
	gl.col = 1;
	gl.tex = 2;

	glBindAttribLocation(program, gl.pos, "in_Position");
	glBindAttribLocation(program, gl.col, "in_Color");
	glBindAttribLocation(program, gl.tex, "in_TexCoord");
	glLinkProgram(program);

	gl.utexture_y = glGetUniformLocation(program, "uTexY");
	gl.utexture_uv = glGetUniformLocation(program, "uTexUV");

	// Load textures
	GLuint tex[2];
	glGenTextures(2, tex);

	// Luma
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex[0]);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, luma);
	glCheckError();

	// Chroma
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tex[1]);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

#if 1
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width/2, height/2, 0, GL_RG, GL_UNSIGNED_BYTE, chroma);
#else
	// This means the fragment shader should use r and a, instead of x and y.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width/2, height/2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, chroma);
#endif
	glCheckError();
}

static void realize_cb (GtkWidget *widget)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_NONE
	};
	EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE
	};

	EGLConfig egl_config;
	EGLint major, minor, n_config;
	EGLBoolean ret;

	egl_display = eglGetDisplay((EGLNativeDisplayType) gdk_x11_display_get_xdisplay (gtk_widget_get_display (widget)));

	ret = eglInitialize(egl_display, &major, &minor);
	assert(ret == EGL_TRUE);

	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	eglChooseConfig(egl_display, attributes, &egl_config, 1, &n_config);
	egl_surface = eglCreateWindowSurface(egl_display, egl_config, gdk_x11_window_get_xid (gtk_widget_get_window (widget)), NULL);
	assert(egl_surface);

	egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
	assert(egl_context);

	ret = eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
	assert(ret == EGL_TRUE);

        printf("using GL setup: \n"
                "   renderer '%s'\n"
                "   vendor '%s'\n"
                "   GL version '%s'\n"
                "   GLSL version '%s'\n",
                glGetString(GL_RENDERER), glGetString(GL_VENDOR),
                glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

	init_gl();
}

static gboolean draw_cb (GtkWidget *widget)
{
	static const GLfloat verts[4][2] = {
		{ -1.0f,  1.0f },
		{  1.0f,  1.0f },
		{  1.0f, -1.0f },
		{ -1.0f, -1.0f }
	};
	static const GLfloat texcoords[4][2] = {
		{  0.0f,  0.0f },
		{  1.0f,  0.0f },
		{  1.0f,  1.0f },
		{  0.0f,  1.0f }
	};
	static const GLfloat colors[4][3] = {
		{ 1, 1, 1 },
		{ 1, 1, 1 },
		{ 1, 1, 1 },
		{ 1, 1, 1 }
	};

	glViewport (0, 0, gtk_widget_get_allocated_width (widget), gtk_widget_get_allocated_height (widget));

	glVertexAttribPointer(gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(gl.tex, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
	glVertexAttribPointer(gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);

	glEnableVertexAttribArray(gl.pos);
	glEnableVertexAttribArray(gl.tex);
	glEnableVertexAttribArray(gl.col);

	glUniform1i(gl.utexture_y,  0);
	glUniform1i(gl.utexture_uv, 1);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glDisableVertexAttribArray(gl.pos);
	glDisableVertexAttribArray(gl.tex);
	glDisableVertexAttribArray(gl.col);

	eglSwapBuffers (egl_display, egl_surface);

	return TRUE;
}

static gboolean redraw(GtkWidget *widget)
{
	gtk_widget_queue_draw(widget);

	return TRUE;
}

int main (int argc, char **argv)
{
	GtkWidget *w;

	gtk_init(&argc, &argv);

	w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_double_buffered(GTK_WIDGET(w), FALSE);
	g_signal_connect(G_OBJECT(w), "realize", G_CALLBACK(realize_cb), NULL);
	g_signal_connect(G_OBJECT(w), "draw", G_CALLBACK(draw_cb), NULL);
	g_timeout_add(34, (GSourceFunc) redraw, w);

	gtk_widget_show(w);

	gtk_main();

	return 0;
}
