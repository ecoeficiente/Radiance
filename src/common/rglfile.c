/* Copyright (c) 1998 Silicon Graphics, Inc. */

#ifndef lint
static char SCCSid[] = "$SunId$ SGI";
#endif

/*
 * Load Radiance object(s) and create OpenGL display lists
 */

#include "radogl.h"

#ifndef NLIST2ALLOC
#define NLIST2ALLOC	16		/* batch of display lists to get */
#endif

extern int	o_sphere(), o_face(), o_cone(), o_ring(),
		o_source(), o_instance();

extern int	m_normal(), m_glass(), m_aniso(), m_brdf(), m_brdf2(),
		m_light(), m_prism(), m_mirror();

FUN  ofun[NUMOTYPE] = INIT_OTYPE;


static
initotypes()			/* initialize ofun array */
{
	if (ofun[OBJ_SPHERE].funp == o_sphere)
		return;			/* already done */
						/* assign surface types */
	ofun[OBJ_SPHERE].funp =
	ofun[OBJ_BUBBLE].funp = o_sphere;
	ofun[OBJ_FACE].funp = o_face;
	ofun[OBJ_CONE].funp =
	ofun[OBJ_CUP].funp =
	ofun[OBJ_CYLINDER].funp =
	ofun[OBJ_TUBE].funp = o_cone;
	ofun[OBJ_RING].funp = o_ring;
	ofun[OBJ_SOURCE].funp = o_source;
	ofun[OBJ_INSTANCE].funp = o_instance;
						/* assign material types */
	ofun[MAT_TRANS].funp =
	ofun[MAT_PLASTIC].funp =
	ofun[MAT_METAL].funp = m_normal;
	ofun[MAT_GLASS].funp =
	ofun[MAT_DIELECTRIC].funp =
	ofun[MAT_INTERFACE].funp = m_glass;
	ofun[MAT_PLASTIC2].funp =
	ofun[MAT_METAL2].funp =
	ofun[MAT_TRANS2].funp = m_aniso;
	ofun[MAT_TDATA].funp =
	ofun[MAT_PDATA].funp =
	ofun[MAT_MDATA].funp =
	ofun[MAT_TFUNC].funp =
	ofun[MAT_PFUNC].funp =
	ofun[MAT_MFUNC].funp = m_brdf;
	ofun[MAT_BRTDF].funp = m_brdf2;
	ofun[MAT_GLOW].funp =
	ofun[MAT_LIGHT].funp =
	ofun[MAT_SPOT].funp =
	ofun[MAT_ILLUM].funp = m_light;
	ofun[MAT_MIRROR].funp = m_mirror;
	ofun[MAT_DIRECT1].funp =
	ofun[MAT_DIRECT2].funp = m_prism;
}


int
newglist()			/* allocate an OGL list id */
{
	static int	nextlist, nleft = 0;

	if (!nleft--) {
		nextlist = glGenLists(NLIST2ALLOC);
		nleft = NLIST2ALLOC-1;
	}
	return(nextlist++);
}


rgl_checkerr(where)		/* check for GL or GLU error */
char	*where;
{
	register GLenum	errcode;

	while ((errcode = glGetError()) != GL_NO_ERROR) {
		sprintf(errmsg, "OpenGL error %s: %s",
				where, gluErrorString(errcode));
		error(WARNING, errmsg);
	}
}


int
rgl_filelist(ic, inp)		/* load scene files into display list */
int	ic;
char	**inp;
{
	int	listid;

	initotypes();		/* prepare */
	listid = newglist();
	glNewList(listid, GL_COMPILE);
	lightinit();		/* start light source list */
	while (ic--)		/* load each file */
		rgl_load(*inp++);
	surfclean();		/* clean up first pass */
	lightclean();		/* clean up light sources also */
	glEndList();		/* end of top display list */
	lightdefs();		/* define light sources */
	loadoctrees();		/* load octrees (sublists) for instances */
	return(listid);		/* all done -- return list id */
}


int
rgl_octlist(fname, cent, radp)	/* load octree objects into display list */
char	*fname;
FVECT	cent;			/* returned octree center (optional) */
FLOAT	*radp;			/* returned octree size (optional) */
{
	double	r;
	int	listid;
				/* modeled after rgl_filelist() */
	initotypes();
				/* check the octree and get its size */
	r = checkoct(fname, cent);
	if (radp != NULL) *radp = r;
				/* start the display list */
	listid = newglist();
	glNewList(listid, GL_COMPILE);
	lightinit();		/* start light source list */
	loadoct(fname);		/* load octree objects into display list */
	surfclean();		/* clean up and close top list */
	lightclean();		/* clean up light sources also */
	glEndList();		/* close top list */
	lightdefs();		/* define light sources */
	loadoctrees();		/* load referenced octrees into sublists */
	return(listid);
}


rgl_load(inpspec)		/* convert scene description into OGL calls */
char	*inpspec;
{
	FILE	*popen();
	char	*fgetline();
	FILE	*infp;
	char	buf[1024];
	register int	c;

	if (inpspec == NULL) {
		infp = stdin;
		inpspec = "standard input";
	} else if (inpspec[0] == '!') {
		if ((infp = popen(inpspec+1, "r")) == NULL) {
			sprintf(errmsg, "cannot execute \"%s\"", inpspec);
			error(SYSTEM, errmsg);
		}
	} else if ((infp = fopen(inpspec, "r")) == NULL) {
		sprintf(errmsg, "cannot open scene file \"%s\"", inpspec);
		error(SYSTEM, errmsg);
	}
	while ((c = getc(infp)) != EOF) {
		if (isspace(c))
			continue;
		if (c == '#') {				/* comment */
			fgets(buf, sizeof(buf), infp);
		} else if (c == '!') {			/* command */
			ungetc(c, infp);
			fgetline(buf, sizeof(buf), infp);
			rgl_load(buf);
		} else {				/* object */
			ungetc(c, infp);
			rgl_object(inpspec, infp);
		}
	}
	if (inpspec[0] == '!')
		pclose(infp);
	else
		fclose(infp);
}


rgl_object(name, fp)			/* read the next object */
char  *name;
FILE  *fp;
{
	static OBJREC	ob;
	char  sbuf[MAXSTR];
	int  rval;
					/* get modifier */
	strcpy(sbuf, "EOF");
	fgetword(sbuf, MAXSTR, fp);
	ob.omod = 0;			/* use ob.os for pointer to material */
	if (!strcmp(sbuf, VOIDID))
		ob.os = NULL;
	else
		ob.os = (char *)getmatp(sbuf);
					/* get type */
	strcpy(sbuf, "EOF");
	fgetword(sbuf, MAXSTR, fp);
	if (!strcmp(sbuf, ALIASID))
		ob.otype = -1;
	else if ((ob.otype = otype(sbuf)) < 0) {
		sprintf(errmsg, "(%s): unknown type \"%s\"", name, sbuf);
		error(USER, errmsg);
	}
					/* get identifier */
	sbuf[0] = '\0';
	fgetword(sbuf, MAXSTR, fp);
	ob.oname = sbuf;
					/* get arguments */
	if (ob.otype == -1) {
		char  sbuf2[MAXSTR];		/* get alias */
		strcpy(sbuf2, "EOF");
		fgetword(sbuf2, MAXSTR, fp);
		if (ob.os == NULL)
			ob.os = (char *)getmatp(sbuf2);
		o_default(&ob);			/* fake reference */
		return;
	}
	if ((rval = readfargs(&ob.oargs, fp)) == 0) {
		sprintf(errmsg, "(%s): bad arguments", name);
		objerror(&ob, USER, errmsg);
	} else if (rval < 0) {
		sprintf(errmsg, "(%s): error reading scene", name);
		error(SYSTEM, errmsg);
	}
					/* execute */
	(*ofun[ob.otype].funp)(&ob);
					/* free arguments */
	freefargs(&ob.oargs);
}