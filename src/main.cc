#include <GL/glew.h>
#include <GL/glut.h>

#include <resman.h>
#include <stdio.h>
#include <string.h>

#include "sdr.h"

static bool init(const char *fname);
static void cleanup();

static void display();
static void reshape(int w, int h);
static void keyboard(unsigned char key, int x, int y);
static void idle();

static int sdr_load(const char *fname, int id, void *closure);
static int sdr_done(int id, void *closure);
static void sdr_destroy(int id, void *closure);

resman *sdrman;
unsigned int sdrprog;

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitWindowSize(800, 600);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);

	glutCreateWindow("sdrviewer");
	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutIdleFunc(idle);

	if(!argv[1]) {
		fprintf(stderr, "Usage: %s <pixel shader filename>\n", argv[0]);
		return 1;
	}

	if(!init(argv[1]))
		return 1;

	atexit(cleanup);

	glutMainLoop();
}

static bool init(const char *fname)
{
	glewInit();

	glClearColor(1.0, 0.0, 1.0, 1.0);

	if(!(sdrman = resman_create()))
		return false;

	resman_set_load_func(sdrman, sdr_load, 0);
	resman_set_done_func(sdrman, sdr_done, 0);
	resman_set_destroy_func(sdrman, sdr_destroy, 0);

	if(resman_add(sdrman, fname, 0) == -1) {
		fprintf(stderr, "Failed to load shader\n");
		return false;
	}

	return true;
}

static void cleanup()
{
	resman_free(sdrman);
}

static void display()
{
	glClear(GL_COLOR_BUFFER_BIT);

	resman_poll(sdrman);

	if(sdrprog) {
		glUseProgram(sdrprog);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(-1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, -1);
		glTexCoord2f(1, 0); glVertex2f(1, 1);
		glTexCoord2f(0, 0); glVertex2f(-1, 1);
		glEnd();
	}

	glutSwapBuffers();
}

static void reshape(int w, int h)
{
	glViewport(0, 0, w, h);
}

static void keyboard(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);
	default:
		break;
	}
}

static void idle()
{
	glutPostRedisplay();
}

static int sdr_load(const char *fname, int id, void *closure)
{
	printf("%s\n", __func__);
	FILE *fp = fopen(fname, "rb");
	if(!fp) {
		fprintf(stderr, "Failed to load shader: %s\n", fname);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	long fsz = ftell(fp);
	rewind(fp);

	char *buf = new char[fsz + 1];
	if(fread(buf, 1, fsz, fp) != (size_t)fsz) {
		fprintf(stderr, "Unexpected EOF while reading: %s\n", fname);
		fclose(fp);
		delete [] buf;
		return -1;
	}

	buf[fsz] = '\0';
	resman_set_res_data(sdrman, id, buf);
	return 0;
}

#define FSDR_START_TAG "[fragment shader]"
static int sdr_done(int id, void *closure)
{
	printf("sdr done \n");
	char *buf = (char *)resman_get_res_data(sdrman, id);
	char *start = strstr(buf, FSDR_START_TAG);

	if(!start) {
		fprintf(stderr, "Failed to find fragment shader in the given file.\n");
		return -1;
	}

	start += strlen(FSDR_START_TAG);

	char *end = strstr(start, "\n[");
	if(end)
		end[1] = '\0';

	unsigned int fsdr = create_pixel_shader(start);
	delete buf;

	if(!fsdr) {
		fprintf(stderr, "Failed to create pixel shader\n");
		return -1;
	}

	unsigned int tmp_sdrprog = create_program_link(fsdr, 0);
	if(!tmp_sdrprog) {
		free_shader(fsdr);
		fprintf(stderr, "Failed to link program.\n");
		return -1;
	}

	if(sdrprog)
		free_program(sdrprog);

	sdrprog = tmp_sdrprog;
	return 0;
}

static void sdr_destroy(int id, void *closure)
{
}
