#include <GL/glew.h>

#include <GL/freeglut.h>
#include <GL/glx.h>

#include <errno.h>
#include <sys/select.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

#include <resman.h>

#include "sdr.h"

static bool init(const char *fname);
static void cleanup();

static void display();
static void reshape(int w, int h);
static void keyboard(unsigned char key, int x, int y);
static void mouse(int bn, int st, int x, int y);
static void motion(int x, int y);
static void post_redisplay();

static int sdr_load(const char *fname, int id, void *closure);
static int sdr_done(int id, void *closure);

static resman *sdrman;
static bool redraw_pending = false;

static float cam_phi, cam_theta, cam_dist = 10;

static unsigned int sdrprog;
static int uloc_cam_xform = -1;

static bool quit;

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitWindowSize(800, 600);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);

	glutCreateWindow("sdrviewer");
	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	if(!argv[1]) {
		fprintf(stderr, "Usage: %s <pixel shader filename>\n", argv[0]);
		return 1;
	}

	if(!init(argv[1]))
		return 1;

	Display *dpy = glXGetCurrentDisplay();
	int xfd = ConnectionNumber(dpy);

	while(!quit) {
		if(!redraw_pending) {
			int num_rfds;
			int *rfds = resman_get_wait_fds(sdrman, &num_rfds);

			fd_set rset;
			FD_ZERO(&rset);

			int maxfd = xfd;
			FD_SET(xfd, &rset);

			for(int i=0; i<num_rfds; i++) {
				FD_SET(rfds[i], &rset);
				if(rfds[i] > maxfd) maxfd = rfds[i];
			}

			int sel_found;
			while((sel_found = select(maxfd + 1, &rset, 0, 0, 0)) == -1 &&
				errno == EINTR);

			if(sel_found) {
				for(int i=0; i<num_rfds; i++) {
					if(FD_ISSET(rfds[i], &rset)) {
						post_redisplay();
					}
				}
			}
		}

		redraw_pending = false;
		glutMainLoopEvent();
	}
	cleanup();
}

static bool init(const char *fname)
{
	glewInit();

	glClearColor(1.0, 0.0, 1.0, 1.0);

	if(!(sdrman = resman_create()))
		return false;

	resman_set_load_func(sdrman, sdr_load, 0);
	resman_set_done_func(sdrman, sdr_done, 0);

	if(resman_add(sdrman, fname, 0) == -1) {
		fprintf(stderr, "Failed to load shader\n");
		return false;
	}

	resman_wait_all(sdrman);
	return true;
}

static void cleanup()
{
	resman_free(sdrman);
}

static void display()
{
	resman_poll(sdrman);

	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(-cam_theta, 0, 1, 0);
	glRotatef(-cam_phi, 1, 0, 0);
	glTranslatef(0, 0, -cam_dist);

	if(sdrprog) {
		glUseProgram(sdrprog);

		if(uloc_cam_xform != -1) {
			float cam_mat[16];
			glGetFloatv(GL_MODELVIEW_MATRIX, cam_mat);
			glUniformMatrix4fv(uloc_cam_xform, 1, 0, cam_mat);

			/* busy loop until the libresman bug gets fixed... */
			post_redisplay();
		}

		glLoadIdentity();

		glBegin(GL_QUADS);
		glVertex2f(-1, -1);
		glVertex2f(1, -1);
		glVertex2f(1, 1);
		glVertex2f(-1, 1);
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
		quit = true;
		break;
	default:
		break;
	}
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
		start = buf;
	}

	start += strlen(FSDR_START_TAG);

	char *end = strstr(start, "\n[");
	if(end)
		end[1] = '\0';

	clear_shader_header(GL_FRAGMENT_SHADER);
	add_shader_header(GL_FRAGMENT_SHADER, "#version 450\n#define SDRVIEWER\n");

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

	uloc_cam_xform = glGetUniformLocation(sdrprog, "cam_xform");

	post_redisplay();

	return 0;
}

static bool bnstate[8];
static int prev_x, prev_y;
static void mouse(int bn, int st, int x, int y)
{
	int bidx = bn - GLUT_LEFT_BUTTON;
	bnstate[bidx] = st == GLUT_DOWN;

	prev_x = x;
	prev_y = y;
}

static void motion(int x, int y)
{
	int dx = x - prev_x;
	int dy = y - prev_y;

	prev_x = x;
	prev_y = y;

	if(!(dx | dy))
		return;

	if(bnstate[0]) {
		cam_theta += dx * 0.5;
		cam_phi += dy * 0.5;

		if(cam_phi < -90) cam_phi = -90;
		if(cam_phi > 90) cam_phi = 90;

		post_redisplay();
	}

	if(bnstate[2]) {
		cam_dist += dy * 0.1;

		if(cam_dist < 0) cam_dist = 0;

		post_redisplay();
	}
}

static void post_redisplay()
{
	redraw_pending = true;
	glutPostRedisplay();
}
