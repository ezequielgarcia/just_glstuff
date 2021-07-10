#include <assert.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>

static EGLDisplay *egl_display;
static EGLSurface *egl_surface;
static EGLContext *egl_context;
struct {
	GLuint rotation_uniform;
	GLuint pos;
	GLuint col;
} gl;
unsigned int angle = 0;

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
	printf("%d | %s:%d\n", errorCode, file, line);
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
	glCheckError();

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCheckError();
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

	glBindAttribLocation(program, gl.pos, "pos");
	glBindAttribLocation(program, gl.col, "color");
	glLinkProgram(program);

	gl.rotation_uniform = glGetUniformLocation(program, "rotation");
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
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
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

	init_gl();
}

static gboolean draw_cb (GtkWidget *widget)
{
	static const GLfloat verts[3][2] = {
		{ -0.5, -0.5 },
		{  0.5, -0.5 },
		{  0,    0.5 }
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 }
	};
	GLfloat _angle;
	GLfloat rotation[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	angle += 5;
	_angle = angle % 360 * M_PI / 180.0;
	rotation[0][0] =  cos(_angle);
	rotation[0][2] =  sin(_angle);
	rotation[2][0] = -sin(_angle);
	rotation[2][2] =  cos(_angle);

	glViewport (0, 0, gtk_widget_get_allocated_width (widget), gtk_widget_get_allocated_height (widget));
	glUniformMatrix4fv(gl.rotation_uniform, 1, GL_FALSE, (GLfloat *) rotation);
	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(gl.pos);
	glEnableVertexAttribArray(gl.col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(gl.pos);
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
