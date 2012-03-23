/*
 * freeglut_geometry.c
 *
 * Freeglut geometry rendering methods.
 *
 * Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.
 * Written by Pawel W. Olszta, <olszta@sourceforge.net>
 * Creation date: Fri Dec 3 1999
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <GL/freeglut.h>
#include "fg_internal.h"

/*
 * Need more types of polyhedra? See CPolyhedron in MRPT
 */


#ifndef GL_ES_VERSION_2_0
/* General functions for drawing geometry
 * Solids are drawn by glDrawArrays if composed of triangles, or by
 * glDrawElements if consisting of squares or pentagons that were
 * decomposed into triangles (some vertices are repeated in that case).
 * WireFrame drawing will have to be done per face, using GL_LINE_LOOP and
 * issuing one draw call per face. Always use glDrawArrays as no triangle
 * decomposition needed. We use the "first" parameter in glDrawArrays to go
 * from face to face.
 */
static void fghDrawGeometryWire(GLfloat *vertices, GLfloat *normals, GLsizei numFaces, GLsizei numEdgePerFace)
{
    int i;
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glNormalPointer(GL_FLOAT, 0, normals);

    /* Draw per face (TODO: could use glMultiDrawArrays if available) */
    for (i=0; i<numFaces; i++)
        glDrawArrays(GL_LINE_LOOP, i*numEdgePerFace, numEdgePerFace);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}
static void fghDrawGeometrySolid(GLfloat *vertices, GLfloat *normals, GLubyte *vertIdxs, GLsizei numVertices, GLsizei numEdgePerFace)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glNormalPointer(GL_FLOAT, 0, normals);
    if (numEdgePerFace==3)
        glDrawArrays(GL_TRIANGLES, 0, numVertices);
    else
        glDrawElements(GL_TRIANGLES, numVertices, GL_UNSIGNED_BYTE, vertIdxs);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}

/* Shape decomposition to triangles
 * We'll use glDrawElements to draw all shapes that are not triangles, so
 * generate an index vector here, using the below sampling scheme.
 * Be careful to keep winding of all triangles counter-clockwise,
 * assuming that input has correct winding...
 */
static GLubyte   vert4Decomp[6] = {0,1,2, 0,2,3};             /* quad    : 4 input vertices, 6 output (2 triangles) */
static GLubyte   vert5Decomp[9] = {0,1,2, 0,2,4, 4,2,3};      /* pentagon: 5 input vertices, 9 output (3 triangles) */

static void fghGenerateGeometryWithIndexArray(int numFaces, int numEdgePerFace, GLfloat *vertices, GLubyte *vertIndices, GLfloat *normals, GLfloat *vertOut, GLfloat *normOut, GLubyte *vertIdxOut)
{
    int i,j,numEdgeIdxPerFace;
    GLubyte   *vertSamps = NULL;
    switch (numEdgePerFace)
    {
    case 3:
        /* nothing to do here, we'll drawn with glDrawArrays */
        break;
    case 4:
        vertSamps = vert4Decomp;
        numEdgeIdxPerFace = 6;      /* 6 output vertices for each face */
        break;
    case 5:
        vertSamps = vert5Decomp;
        numEdgeIdxPerFace = 9;      /* 9 output vertices for each face */
        break;
    }
    /*
     * Build array with vertices using vertex coordinates and vertex indices
     * Do same for normals.
     * Need to do this because of different normals at shared vertices.
     */
    for (i=0; i<numFaces; i++)
    {
        int normIdx         = i*3;
        int faceIdxVertIdx  = i*numEdgePerFace; // index to first element of "row" in vertex indices
        for (j=0; j<numEdgePerFace; j++)
        {
            int outIdx  = i*numEdgePerFace*3+j*3;
            int vertIdx = vertIndices[faceIdxVertIdx+j]*3;

            vertOut[outIdx  ] = vertices[vertIdx  ];
            vertOut[outIdx+1] = vertices[vertIdx+1];
            vertOut[outIdx+2] = vertices[vertIdx+2];

            normOut[outIdx  ] = normals [normIdx  ];
            normOut[outIdx+1] = normals [normIdx+1];
            normOut[outIdx+2] = normals [normIdx+2];
        }

        /* generate vertex indices for each face */
        if (vertSamps)
            for (j=0; j<numEdgeIdxPerFace; j++)
                vertIdxOut[i*numEdgeIdxPerFace+j] = faceIdxVertIdx + vertSamps[j];
    }
}

static void fghGenerateGeometry(int numFaces, int numEdgePerFace, GLfloat *vertices, GLubyte *vertIndices, GLfloat *normals, GLfloat *vertOut, GLfloat *normOut)
{
    /* This function does the same as fghGenerateGeometryWithIndexArray, just skipping the index array generation... */
    fghGenerateGeometryWithIndexArray(numFaces, numEdgePerFace, vertices, vertIndices, normals, vertOut, normOut, NULL);
}


/* -- INTERNAL SETUP OF GEOMETRY --------------------------------------- */
/* -- stuff that can be cached -- */
/* Cache of input to glDrawArrays or glDrawElements
 * In general, we build arrays with all vertices or normals.
 * We cant compress this and use glDrawElements as all combinations of
 * vertex and normals are unique.
 */
#define DECLARE_SHAPE_CACHE(name,nameICaps,nameCaps)\
    static GLboolean name##Cached = FALSE;\
    static GLfloat name##_verts[nameCaps##_VERT_ELEM_PER_OBJ];\
    static GLfloat name##_norms[nameCaps##_VERT_ELEM_PER_OBJ];\
    static void fgh##nameICaps##Generate()\
    {\
        fghGenerateGeometry(nameCaps##_NUM_FACES, nameCaps##_NUM_EDGE_PER_FACE,\
                            name##_v, name##_vi, name##_n,\
                            name##_verts, name##_norms);\
    }
#define DECLARE_SHAPE_CACHE_DECOMPOSE_TO_TRIANGLE(name,nameICaps,nameCaps)\
    static GLboolean name##Cached = FALSE;\
    static GLfloat  name##_verts[nameCaps##_VERT_ELEM_PER_OBJ];\
    static GLfloat  name##_norms[nameCaps##_VERT_ELEM_PER_OBJ];\
    static GLubyte   name##_vertIdxs[nameCaps##_VERT_PER_OBJ_TRI];\
    static void fgh##nameICaps##Generate()\
    {\
        fghGenerateGeometryWithIndexArray(nameCaps##_NUM_FACES, nameCaps##_NUM_EDGE_PER_FACE,\
                                          name##_v, name##_vi, name##_n,\
                                          name##_verts, name##_norms, name##_vertIdxs);\
    }

/* -- Cube -- */
#define CUBE_NUM_VERT           8
#define CUBE_NUM_FACES          6
#define CUBE_NUM_EDGE_PER_FACE  4
#define CUBE_VERT_PER_OBJ       (CUBE_NUM_FACES*CUBE_NUM_EDGE_PER_FACE)
#define CUBE_VERT_ELEM_PER_OBJ  (CUBE_VERT_PER_OBJ*3)
#define CUBE_VERT_PER_OBJ_TRI   (CUBE_VERT_PER_OBJ+CUBE_NUM_FACES*2)    /* 2 extra edges per face when drawing quads as triangles */
/* Vertex Coordinates */
static GLfloat cube_v[CUBE_NUM_VERT*3] =
{
     .5f, .5f, .5f,
    -.5f, .5f, .5f,
    -.5f,-.5f, .5f,
     .5f,-.5f, .5f,
     .5f,-.5f,-.5f,
     .5f, .5f,-.5f,
    -.5f, .5f,-.5f,
    -.5f,-.5f,-.5f
};
/* Normal Vectors */
static GLfloat cube_n[CUBE_NUM_FACES*3] =
{
     0.0f, 0.0f, 1.0f,
     1.0f, 0.0f, 0.0f,
     0.0f, 1.0f, 0.0f,
    -1.0f, 0.0f, 0.0f,
     0.0f,-1.0f, 0.0f,
     0.0f, 0.0f,-1.0f
};

/* Vertex indices */
static GLubyte cube_vi[CUBE_VERT_PER_OBJ] =
{
    0,1,2,3,
    0,3,4,5,
    0,5,6,1,
    1,6,7,2,
    7,4,3,2,
    4,7,6,5
};
DECLARE_SHAPE_CACHE_DECOMPOSE_TO_TRIANGLE(cube,Cube,CUBE);

/* -- Dodecahedron -- */
/* Magic Numbers:  It is possible to create a dodecahedron by attaching two
 * pentagons to each face of of a cube. The coordinates of the points are:
 *   (+-x,0, z); (+-1, 1, 1); (0, z, x )
 * where x = (-1 + sqrt(5))/2, z = (1 + sqrt(5))/2 or
 *       x = 0.61803398875 and z = 1.61803398875.
 */
#define DODECAHEDRON_NUM_VERT           20
#define DODECAHEDRON_NUM_FACES          12
#define DODECAHEDRON_NUM_EDGE_PER_FACE  5
#define DODECAHEDRON_VERT_PER_OBJ       (DODECAHEDRON_NUM_FACES*DODECAHEDRON_NUM_EDGE_PER_FACE)
#define DODECAHEDRON_VERT_ELEM_PER_OBJ  (DODECAHEDRON_VERT_PER_OBJ*3)
#define DODECAHEDRON_VERT_PER_OBJ_TRI   (DODECAHEDRON_VERT_PER_OBJ+DODECAHEDRON_NUM_FACES*4)    /* 4 extra edges per face when drawing pentagons as triangles */
/* Vertex Coordinates */
static GLfloat dodecahedron_v[DODECAHEDRON_NUM_VERT*3] =
{
               0.0f,  1.61803398875f,  0.61803398875f,
    -          1.0f,            1.0f,            1.0f,
    -0.61803398875f,            0.0f,  1.61803398875f,
     0.61803398875f,            0.0f,  1.61803398875f,
               1.0f,            1.0f,            1.0f,
               0.0f,  1.61803398875f, -0.61803398875f,
               1.0f,            1.0f, -          1.0f,
     0.61803398875f,            0.0f, -1.61803398875f,
    -0.61803398875f,            0.0f, -1.61803398875f,
    -          1.0f,            1.0f, -          1.0f,
               0.0f, -1.61803398875f,  0.61803398875f,
               1.0f, -          1.0f,            1.0f,
    -          1.0f, -          1.0f,            1.0f,
               0.0f, -1.61803398875f, -0.61803398875f,
    -          1.0f, -          1.0f, -          1.0f,
               1.0f, -          1.0f, -          1.0f,
     1.61803398875f, -0.61803398875f,            0.0f,
     1.61803398875f,  0.61803398875f,            0.0f,
    -1.61803398875f,  0.61803398875f,            0.0f,
    -1.61803398875f, -0.61803398875f,            0.0f
};
/* Normal Vectors */
static GLfloat dodecahedron_n[DODECAHEDRON_NUM_FACES*3] =
{
                0.0f,  0.525731112119f,  0.850650808354f,
                0.0f,  0.525731112119f, -0.850650808354f,
                0.0f, -0.525731112119f,  0.850650808354f,
                0.0f, -0.525731112119f, -0.850650808354f,

     0.850650808354f,             0.0f,  0.525731112119f,
    -0.850650808354f,             0.0f,  0.525731112119f,
     0.850650808354f,             0.0f, -0.525731112119f,
    -0.850650808354f,             0.0f, -0.525731112119f,

     0.525731112119f,  0.850650808354f,             0.0f,
     0.525731112119f, -0.850650808354f,             0.0f,
    -0.525731112119f,  0.850650808354f,             0.0f, 
    -0.525731112119f, -0.850650808354f,             0.0f,
};

/* Vertex indices */
static GLubyte dodecahedron_vi[DODECAHEDRON_VERT_PER_OBJ] =
{
     0,  1,  2,  3,  4, 
     5,  6,  7,  8,  9, 
    10, 11,  3,  2, 12, 
    13, 14,  8,  7, 15, 

     3, 11, 16, 17,  4, 
     2,  1, 18, 19, 12, 
     7,  6, 17, 16, 15, 
     8, 14, 19, 18,  9, 

    17,  6,  5,  0,  4, 
    16, 11, 10, 13, 15, 
    18,  1,  0,  5,  9, 
    19, 14, 13, 10, 12
};
DECLARE_SHAPE_CACHE_DECOMPOSE_TO_TRIANGLE(dodecahedron,Dodecahedron,DODECAHEDRON);


/* -- Icosahedron -- */
#define ICOSAHEDRON_NUM_VERT           12
#define ICOSAHEDRON_NUM_FACES          20
#define ICOSAHEDRON_NUM_EDGE_PER_FACE  3
#define ICOSAHEDRON_VERT_PER_OBJ       (ICOSAHEDRON_NUM_FACES*ICOSAHEDRON_NUM_EDGE_PER_FACE)
#define ICOSAHEDRON_VERT_ELEM_PER_OBJ  (ICOSAHEDRON_VERT_PER_OBJ*3)
#define ICOSAHEDRON_VERT_PER_OBJ_TRI   ICOSAHEDRON_VERT_PER_OBJ
/* Vertex Coordinates */
static GLfloat icosahedron_v[ICOSAHEDRON_NUM_VERT*3] =
{
                1.0f,             0.0f,             0.0f,
     0.447213595500f,  0.894427191000f,             0.0f,
     0.447213595500f,  0.276393202252f,  0.850650808354f,
     0.447213595500f, -0.723606797748f,  0.525731112119f,
     0.447213595500f, -0.723606797748f, -0.525731112119f,
     0.447213595500f,  0.276393202252f, -0.850650808354f,
    -0.447213595500f, -0.894427191000f,             0.0f,
    -0.447213595500f, -0.276393202252f,  0.850650808354f,
    -0.447213595500f,  0.723606797748f,  0.525731112119f,
    -0.447213595500f,  0.723606797748f, -0.525731112119f,
    -0.447213595500f, -0.276393202252f, -0.850650808354f,
    -           1.0f,             0.0f,             0.0f
};
/* Normal Vectors:
 * icosahedron_n[i][0] = ( icosahedron_v[icosahedron_vi[i][1]][1] - icosahedron_v[icosahedron_vi[i][0]][1] ) * ( icosahedron_v[icosahedron_vi[i][2]][2] - icosahedron_v[icosahedron_vi[i][0]][2] ) - ( icosahedron_v[icosahedron_vi[i][1]][2] - icosahedron_v[icosahedron_vi[i][0]][2] ) * ( icosahedron_v[icosahedron_vi[i][2]][1] - icosahedron_v[icosahedron_vi[i][0]][1] ) ;
 * icosahedron_n[i][1] = ( icosahedron_v[icosahedron_vi[i][1]][2] - icosahedron_v[icosahedron_vi[i][0]][2] ) * ( icosahedron_v[icosahedron_vi[i][2]][0] - icosahedron_v[icosahedron_vi[i][0]][0] ) - ( icosahedron_v[icosahedron_vi[i][1]][0] - icosahedron_v[icosahedron_vi[i][0]][0] ) * ( icosahedron_v[icosahedron_vi[i][2]][2] - icosahedron_v[icosahedron_vi[i][0]][2] ) ;
 * icosahedron_n[i][2] = ( icosahedron_v[icosahedron_vi[i][1]][0] - icosahedron_v[icosahedron_vi[i][0]][0] ) * ( icosahedron_v[icosahedron_vi[i][2]][1] - icosahedron_v[icosahedron_vi[i][0]][1] ) - ( icosahedron_v[icosahedron_vi[i][1]][1] - icosahedron_v[icosahedron_vi[i][0]][1] ) * ( icosahedron_v[icosahedron_vi[i][2]][0] - icosahedron_v[icosahedron_vi[i][0]][0] ) ;
*/
static GLfloat icosahedron_n[ICOSAHEDRON_NUM_FACES*3] =
{
     0.760845213037948f,  0.470228201835026f,  0.341640786498800f,
     0.760845213036861f, -0.179611190632978f,  0.552786404500000f,
     0.760845213033849f, -0.581234022404097f,                0.0f,
     0.760845213036861f, -0.179611190632978f, -0.552786404500000f,
     0.760845213037948f,  0.470228201835026f, -0.341640786498800f,
     0.179611190628666f,  0.760845213037948f,  0.552786404498399f,
     0.179611190634277f, -0.290617011204044f,  0.894427191000000f,
     0.179611190633958f, -0.940456403667806f,                0.0f,
     0.179611190634278f, -0.290617011204044f, -0.894427191000000f,
     0.179611190628666f,  0.760845213037948f, -0.552786404498399f,
    -0.179611190633958f,  0.940456403667806f,                0.0f,
    -0.179611190634277f,  0.290617011204044f,  0.894427191000000f,
    -0.179611190628666f, -0.760845213037948f,  0.552786404498399f,
    -0.179611190628666f, -0.760845213037948f, -0.552786404498399f,
    -0.179611190634277f,  0.290617011204044f, -0.894427191000000f,
    -0.760845213036861f,  0.179611190632978f, -0.552786404500000f,
    -0.760845213033849f,  0.581234022404097f,                0.0f,
    -0.760845213036861f,  0.179611190632978f,  0.552786404500000f,
    -0.760845213037948f, -0.470228201835026f,  0.341640786498800f,
    -0.760845213037948f, -0.470228201835026f, -0.341640786498800f,
};

/* Vertex indices */
static GLubyte icosahedron_vi[ICOSAHEDRON_VERT_PER_OBJ] =
{
    0,   1,  2 ,
    0,   2,  3 ,
    0,   3,  4 ,
    0,   4,  5 ,
    0,   5,  1 ,
    1,   8,  2 ,
    2,   7,  3 ,
    3,   6,  4 ,
    4,  10,  5 ,
    5,   9,  1 ,
    1,   9,  8 ,
    2,   8,  7 ,
    3,   7,  6 ,
    4,   6, 10 ,
    5,  10,  9 ,
    11,  9, 10 ,
    11,  8,  9 ,
    11,  7,  8 ,
    11,  6,  7 ,
    11, 10,  6 
};
DECLARE_SHAPE_CACHE(icosahedron,Icosahedron,ICOSAHEDRON);

/* -- Octahedron -- */
#define OCTAHEDRON_NUM_VERT           6
#define OCTAHEDRON_NUM_FACES          8
#define OCTAHEDRON_NUM_EDGE_PER_FACE  3
#define OCTAHEDRON_VERT_PER_OBJ       (OCTAHEDRON_NUM_FACES*OCTAHEDRON_NUM_EDGE_PER_FACE)
#define OCTAHEDRON_VERT_ELEM_PER_OBJ  (OCTAHEDRON_VERT_PER_OBJ*3)
#define OCTAHEDRON_VERT_PER_OBJ_TRI   OCTAHEDRON_VERT_PER_OBJ

/* Vertex Coordinates */
static GLfloat octahedron_v[OCTAHEDRON_NUM_VERT*3] =
{
     1.f,  0.f,  0.f,
     0.f,  1.f,  0.f,
     0.f,  0.f,  1.f,
    -1.f,  0.f,  0.f,
     0.f, -1.f,  0.f,
     0.f,  0.f, -1.f,

};
/* Normal Vectors */
static GLfloat octahedron_n[OCTAHEDRON_NUM_FACES*3] =
{
     0.577350269189f, 0.577350269189f, 0.577350269189f,    /* sqrt(1/3) */
     0.577350269189f, 0.577350269189f,-0.577350269189f,
     0.577350269189f,-0.577350269189f, 0.577350269189f,
     0.577350269189f,-0.577350269189f,-0.577350269189f,
    -0.577350269189f, 0.577350269189f, 0.577350269189f,
    -0.577350269189f, 0.577350269189f,-0.577350269189f,
    -0.577350269189f,-0.577350269189f, 0.577350269189f,
    -0.577350269189f,-0.577350269189f,-0.577350269189f

};

/* Vertex indices */
static GLubyte octahedron_vi[OCTAHEDRON_VERT_PER_OBJ] =
{
    0, 1, 2,
    0, 5, 1,
    0, 2, 4,
    0, 4, 5,
    3, 2, 1,
    3, 1, 5,
    3, 4, 2,
    3, 5, 4
};
DECLARE_SHAPE_CACHE(octahedron,Octahedron,OCTAHEDRON);

/* -- RhombicDodecahedron -- */
#define RHOMBICDODECAHEDRON_NUM_VERT            14
#define RHOMBICDODECAHEDRON_NUM_FACES           12
#define RHOMBICDODECAHEDRON_NUM_EDGE_PER_FACE   4
#define RHOMBICDODECAHEDRON_VERT_PER_OBJ       (RHOMBICDODECAHEDRON_NUM_FACES*RHOMBICDODECAHEDRON_NUM_EDGE_PER_FACE)
#define RHOMBICDODECAHEDRON_VERT_ELEM_PER_OBJ  (RHOMBICDODECAHEDRON_VERT_PER_OBJ*3)
#define RHOMBICDODECAHEDRON_VERT_PER_OBJ_TRI   (RHOMBICDODECAHEDRON_VERT_PER_OBJ+RHOMBICDODECAHEDRON_NUM_FACES*2)    /* 2 extra edges per face when drawing quads as triangles */

/* Vertex Coordinates */
static GLfloat rhombicdodecahedron_v[RHOMBICDODECAHEDRON_NUM_VERT*3] =
{
                0.0f,             0.0f,  1.0f,
     0.707106781187f,             0.0f,  0.5f,
                0.0f,  0.707106781187f,  0.5f,
    -0.707106781187f,             0.0f,  0.5f,
                0.0f, -0.707106781187f,  0.5f,
     0.707106781187f,  0.707106781187f,  0.0f,
    -0.707106781187f,  0.707106781187f,  0.0f,
    -0.707106781187f, -0.707106781187f,  0.0f,
     0.707106781187f, -0.707106781187f,  0.0f,
     0.707106781187f,             0.0f, -0.5f,
                0.0f,  0.707106781187f, -0.5f,
    -0.707106781187f,             0.0f, -0.5f,
                0.0f, -0.707106781187f, -0.5f,
                0.0f,             0.0f, -1.0f
};
/* Normal Vectors */
static GLfloat rhombicdodecahedron_n[RHOMBICDODECAHEDRON_NUM_FACES*3] =
{
     0.353553390594f,  0.353553390594f,  0.5f,
    -0.353553390594f,  0.353553390594f,  0.5f,
    -0.353553390594f, -0.353553390594f,  0.5f,
     0.353553390594f, -0.353553390594f,  0.5f,
                0.0f,             1.0f,  0.0f,
    -           1.0f,             0.0f,  0.0f,
                0.0f, -           1.0f,  0.0f,
                1.0f,             0.0f,  0.0f,
     0.353553390594f,  0.353553390594f, -0.5f,
    -0.353553390594f,  0.353553390594f, -0.5f,
    -0.353553390594f, -0.353553390594f, -0.5f,
     0.353553390594f, -0.353553390594f, -0.5f
};

/* Vertex indices */
static GLubyte rhombicdodecahedron_vi[RHOMBICDODECAHEDRON_VERT_PER_OBJ] =
{
    0,  1,  5,  2,
    0,  2,  6,  3,
    0,  3,  7,  4,
    0,  4,  8,  1,
    5, 10,  6,  2,
    6, 11,  7,  3,
    7, 12,  8,  4,
    8,  9,  5,  1,
    5,  9, 13, 10,
    6, 10, 13, 11,
    7, 11, 13, 12,
    8, 12, 13,  9
};
DECLARE_SHAPE_CACHE_DECOMPOSE_TO_TRIANGLE(rhombicdodecahedron,RhombicDodecahedron,RHOMBICDODECAHEDRON);

/* -- Tetrahedron -- */
/* Magic Numbers:  r0 = ( 1, 0, 0 )
 *                 r1 = ( -1/3, 2 sqrt(2) / 3, 0 )
 *                 r2 = ( -1/3, - sqrt(2) / 3,  sqrt(6) / 3 )
 *                 r3 = ( -1/3, - sqrt(2) / 3, -sqrt(6) / 3 )
 * |r0| = |r1| = |r2| = |r3| = 1
 * Distance between any two points is 2 sqrt(6) / 3
 *
 * Normals:  The unit normals are simply the negative of the coordinates of the point not on the surface.
 */
#define TETRAHEDRON_NUM_VERT            4
#define TETRAHEDRON_NUM_FACES           4
#define TETRAHEDRON_NUM_EDGE_PER_FACE   3
#define TETRAHEDRON_VERT_PER_OBJ        (TETRAHEDRON_NUM_FACES*TETRAHEDRON_NUM_EDGE_PER_FACE)
#define TETRAHEDRON_VERT_ELEM_PER_OBJ   (TETRAHEDRON_VERT_PER_OBJ*3)
#define TETRAHEDRON_VERT_PER_OBJ_TRI    TETRAHEDRON_VERT_PER_OBJ

/* Vertex Coordinates */
static GLfloat tetrahedron_v[TETRAHEDRON_NUM_VERT*3] =
{
                1.0f,             0.0f,             0.0f,
    -0.333333333333f,  0.942809041582f,             0.0f,
    -0.333333333333f, -0.471404520791f,  0.816496580928f,
    -0.333333333333f, -0.471404520791f, -0.816496580928f
};
/* Normal Vectors */
static GLfloat tetrahedron_n[TETRAHEDRON_NUM_FACES*3] =
{
    -           1.0f,             0.0f,             0.0f,
     0.333333333333f, -0.942809041582f,             0.0f,
     0.333333333333f,  0.471404520791f, -0.816496580928f,
     0.333333333333f,  0.471404520791f,  0.816496580928f
};

/* Vertex indices */
static GLubyte tetrahedron_vi[TETRAHEDRON_VERT_PER_OBJ] =
{
    1, 3, 2,
    0, 2, 3,
    0, 3, 1,
    0, 1, 2
};
DECLARE_SHAPE_CACHE(tetrahedron,Tetrahedron,TETRAHEDRON);

/* -- Sierpinski Sponge -- */
static unsigned int ipow (int x, unsigned int y)
{
    return y==0? 1: y==1? x: (y%2? x: 1) * ipow(x*x, y/2);
}

static void fghSierpinskiSpongeGenerate ( int numLevels, double offset[3], GLfloat scale, GLfloat* vertices, GLfloat* normals )
{
    int i, j;
    if ( numLevels == 0 )
    {
        for (i=0; i<TETRAHEDRON_NUM_FACES; i++)
        {
            int normIdx         = i*3;
            int faceIdxVertIdx  = i*TETRAHEDRON_NUM_EDGE_PER_FACE;
            for (j=0; j<TETRAHEDRON_NUM_EDGE_PER_FACE; j++)
            {
                int outIdx  = i*TETRAHEDRON_NUM_EDGE_PER_FACE*3+j*3;
                int vertIdx = tetrahedron_vi[faceIdxVertIdx+j]*3;

                vertices[outIdx  ] = (GLfloat)offset[0] + scale * tetrahedron_v[vertIdx  ];
                vertices[outIdx+1] = (GLfloat)offset[1] + scale * tetrahedron_v[vertIdx+1];
                vertices[outIdx+2] = (GLfloat)offset[2] + scale * tetrahedron_v[vertIdx+2];

                normals [outIdx  ] = tetrahedron_n[normIdx  ];
                normals [outIdx+1] = tetrahedron_n[normIdx+1];
                normals [outIdx+2] = tetrahedron_n[normIdx+2];
            }
        }
    }
    else if ( numLevels > 0 )
    {
        double local_offset[3] ;    /* Use a local variable to avoid buildup of roundoff errors */
        unsigned int stride = ipow(4,--numLevels)*TETRAHEDRON_VERT_ELEM_PER_OBJ;
        scale /= 2.0 ;
        for ( i = 0 ; i < TETRAHEDRON_NUM_FACES ; i++ )
        {
            int idx         = i*3;
            local_offset[0] = offset[0] + scale * tetrahedron_v[idx  ];
            local_offset[1] = offset[1] + scale * tetrahedron_v[idx+1];
            local_offset[2] = offset[2] + scale * tetrahedron_v[idx+2];
            fghSierpinskiSpongeGenerate ( numLevels, local_offset, scale, vertices+i*stride, normals+i*stride );
        }
    }
}

/* -- Now the various shapes involving circles -- */
/*
 * Compute lookup table of cos and sin values forming a circle
 * (or half circle if halfCircle==TRUE)
 *
 * Notes:
 *    It is the responsibility of the caller to free these tables
 *    The size of the table is (n+1) to form a connected loop
 *    The last entry is exactly the same as the first
 *    The sign of n can be flipped to get the reverse loop
 */
static void fghCircleTable(GLfloat **sint, GLfloat **cost, const int n, const GLboolean halfCircle)
{
    int i;
    
    /* Table size, the sign of n flips the circle direction */
    const int size = abs(n);

    /* Determine the angle between samples */
    const GLfloat angle = (halfCircle?1:2)*(GLfloat)M_PI/(GLfloat)( ( n == 0 ) ? 1 : n );

    /* Allocate memory for n samples, plus duplicate of first entry at the end */
    *sint = malloc(sizeof(GLfloat) * (size+1));
    *cost = malloc(sizeof(GLfloat) * (size+1));

    /* Bail out if memory allocation fails, fgError never returns */
    if (!(*sint) || !(*cost))
    {
        free(*sint);
        free(*cost);
        fgError("Failed to allocate memory in fghCircleTable");
    }

    /* Compute cos and sin around the circle */
    (*sint)[0] = 0.0;
    (*cost)[0] = 1.0;

    for (i=1; i<size; i++)
    {
        (*sint)[i] = sinf(angle*i);
        (*cost)[i] = cosf(angle*i);
    }

    
    if (halfCircle)
    {
        (*sint)[size] =  0.0f;  /* sin PI */
        (*cost)[size] = -1.0f;  /* cos PI */
    }
    else
    {
        /* Last sample is duplicate of the first (sin or cos of 2 PI) */
        (*sint)[size] = (*sint)[0];
        (*cost)[size] = (*cost)[0];
    }
}


/* -- INTERNAL DRAWING functions --------------------------------------- */
#define _DECLARE_INTERNAL_DRAW_DO_DECLARE(name,nameICaps,nameCaps,vertIdxs)\
    static void fgh##nameICaps( GLboolean useWireMode )\
    {\
        if (!name##Cached)\
        {\
            fgh##nameICaps##Generate();\
            name##Cached = GL_TRUE;\
        }\
        \
        if (useWireMode)\
        {\
            fghDrawGeometryWire (name##_verts,name##_norms,\
                                                             nameCaps##_NUM_FACES,nameCaps##_NUM_EDGE_PER_FACE);\
        }\
        else\
        {\
            fghDrawGeometrySolid(name##_verts,name##_norms,vertIdxs,\
                                 nameCaps##_VERT_PER_OBJ_TRI,                     nameCaps##_NUM_EDGE_PER_FACE);\
        }\
    }
#define DECLARE_INTERNAL_DRAW(name,nameICaps,nameCaps)                        _DECLARE_INTERNAL_DRAW_DO_DECLARE(name,nameICaps,nameCaps,NULL)
#define DECLARE_INTERNAL_DRAW_DECOMPOSED_TO_TRIANGLE(name,nameICaps,nameCaps) _DECLARE_INTERNAL_DRAW_DO_DECLARE(name,nameICaps,nameCaps,name##_vertIdxs)

static void fghCube( GLfloat dSize, GLboolean useWireMode )
{
    GLfloat *vertices;

    if (!cubeCached)
    {
        fghCubeGenerate();
        cubeCached = GL_TRUE;
    }

    if (dSize!=1.f)
    {
        /* Need to build new vertex list containing vertices for cube of different size */
        int i;

        vertices = malloc(CUBE_VERT_ELEM_PER_OBJ * sizeof(GLfloat));

        /* Bail out if memory allocation fails, fgError never returns */
        if (!vertices)
        {
            free(vertices);
            fgError("Failed to allocate memory in fghCube");
        }

        for (i=0; i<CUBE_VERT_ELEM_PER_OBJ; i++)
            vertices[i] = dSize*cube_verts[i];
    }
    else
        vertices = cube_verts;

    if (useWireMode)
        fghDrawGeometryWire (vertices,cube_norms,                                    CUBE_NUM_FACES,CUBE_NUM_EDGE_PER_FACE);
    else
        fghDrawGeometrySolid(vertices,cube_norms,cube_vertIdxs,CUBE_VERT_PER_OBJ_TRI,               CUBE_NUM_EDGE_PER_FACE);

    if (dSize!=1.f)
        /* cleanup allocated memory */
        free(vertices);
}

DECLARE_INTERNAL_DRAW_DECOMPOSED_TO_TRIANGLE(dodecahedron,Dodecahedron,DODECAHEDRON);
DECLARE_INTERNAL_DRAW(icosahedron,Icosahedron,ICOSAHEDRON);
DECLARE_INTERNAL_DRAW(octahedron,Octahedron,OCTAHEDRON);
DECLARE_INTERNAL_DRAW_DECOMPOSED_TO_TRIANGLE(rhombicdodecahedron,RhombicDodecahedron,RHOMBICDODECAHEDRON);
DECLARE_INTERNAL_DRAW(tetrahedron,Tetrahedron,TETRAHEDRON);

static void fghSierpinskiSponge ( int numLevels, double offset[3], GLfloat scale, GLboolean useWireMode )
{
    GLfloat *vertices;
    GLfloat * normals;
    GLsizei    numTetr = numLevels<0? 0 : ipow(4,numLevels); /* No sponge for numLevels below 0 */
    GLsizei    numVert = numTetr*TETRAHEDRON_VERT_PER_OBJ;
    GLsizei    numFace = numTetr*TETRAHEDRON_NUM_FACES;

    if (numTetr)
    {
        /* Allocate memory */
        vertices = malloc(numVert*3 * sizeof(GLfloat));
        normals  = malloc(numVert*3 * sizeof(GLfloat));
        /* Bail out if memory allocation fails, fgError never returns */
        if (!vertices || !normals)
        {
            free(vertices);
            free(normals);
            fgError("Failed to allocate memory in fghSierpinskiSponge");
        }

        /* Generate elements */
        fghSierpinskiSpongeGenerate ( numLevels, offset, scale, vertices, normals );

        /* Draw and cleanup */
        if (useWireMode)
            fghDrawGeometryWire (vertices,normals,             numFace,TETRAHEDRON_NUM_EDGE_PER_FACE);
        else
            fghDrawGeometrySolid(vertices,normals,NULL,numVert,        TETRAHEDRON_NUM_EDGE_PER_FACE);

        free(vertices);
        free(normals );
    }
}
#endif /* GL_ES_VERSION_2_0 */


/* -- INTERFACE FUNCTIONS ---------------------------------------------- */


#ifndef EGL_VERSION_1_0
/*
 * Draws a solid sphere
 */
void FGAPIENTRY glutSolidSphere(double radius, GLint slices, GLint stacks)
{
    int i,j;

    /* Adjust z and radius as stacks are drawn. */
    GLfloat radf = (GLfloat)radius;
    GLfloat z0,z1;
    GLfloat r0,r1;

    /* Pre-computed circle */

    GLfloat *sint1,*cost1;
    GLfloat *sint2,*cost2;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidSphere" );

    fghCircleTable(&sint1,&cost1,-slices,FALSE);
    fghCircleTable(&sint2,&cost2, stacks,TRUE);

    /* The top stack is covered with a triangle fan */

    z0 = 1;
    z1 = cost2[(stacks>0)?1:0];
    r0 = 0;
    r1 = sint2[(stacks>0)?1:0];

    glBegin(GL_TRIANGLE_FAN);

        glNormal3f(0,0,1);
        glVertex3f(0,0,radf);

        for (j=slices; j>=0; j--)
        {
            glNormal3f(cost1[j]*r1,      sint1[j]*r1,      z1     );
            glVertex3f(cost1[j]*r1*radf, sint1[j]*r1*radf, z1*radf);
        }

    glEnd();

    /* Cover each stack with a quad strip, except the top and bottom stacks */

    for( i=1; i<stacks-1; i++ )
    {
        z0 = z1; z1 = cost2[i+1];
        r0 = r1; r1 = sint2[i+1];

        glBegin(GL_QUAD_STRIP);

            for(j=0; j<=slices; j++)
            {
                glNormal3d(cost1[j]*r1,      sint1[j]*r1,      z1     );
                glVertex3d(cost1[j]*r1*radf, sint1[j]*r1*radf, z1*radf);
                glNormal3d(cost1[j]*r0,      sint1[j]*r0,      z0     );
                glVertex3d(cost1[j]*r0*radf, sint1[j]*r0*radf, z0*radf);
            }

        glEnd();
    }

    /* The bottom stack is covered with a triangle fan */

    z0 = z1;
    r0 = r1;

    glBegin(GL_TRIANGLE_FAN);

        glNormal3d(0,0,-1);
        glVertex3d(0,0,-radius);

        for (j=0; j<=slices; j++)
        {
            glNormal3d(cost1[j]*r0,      sint1[j]*r0,      z0     );
            glVertex3d(cost1[j]*r0*radf, sint1[j]*r0*radf, z0*radf);
        }

    glEnd();

    /* Release sin and cos tables */

    free(sint1);
    free(cost1);
    free(sint2);
    free(cost2);
}

/*
 * Draws a wire sphere
 */
void FGAPIENTRY glutWireSphere(double radius, GLint slices, GLint stacks)
{
    int i,j;

    /* Adjust z and radius as stacks and slices are drawn. */
    GLfloat radf = (GLfloat)radius;
    GLfloat r;
    GLfloat x,y,z;

    /* Pre-computed circle */

    GLfloat *sint1,*cost1;
    GLfloat *sint2,*cost2;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireSphere" );

    fghCircleTable(&sint1,&cost1,-slices,FALSE);
    fghCircleTable(&sint2,&cost2, stacks,TRUE);

    /* Draw a line loop for each stack */

    for (i=1; i<stacks; i++)
    {
        z = cost2[i];
        r = sint2[i];

        glBegin(GL_LINE_LOOP);

            for(j=0; j<=slices; j++)
            {
                x = cost1[j];
                y = sint1[j];

                glNormal3f(x,y,z);
                glVertex3f(x*r*radf,y*r*radf,z*radf);
            }

        glEnd();
    }

    /* Draw a line loop for each slice */

    for (i=0; i<slices; i++)
    {
        glBegin(GL_LINE_STRIP);

            for(j=0; j<=stacks; j++)
            {
                x = cost1[i]*sint2[j];
                y = sint1[i]*sint2[j];
                z = cost2[j];

                glNormal3f(x,y,z);
                glVertex3f(x*radf,y*radf,z*radf);
            }

        glEnd();
    }

    /* Release sin and cos tables */

    free(sint1);
    free(cost1);
    free(sint2);
    free(cost2);
}

/*
 * Draws a solid cone
 */
void FGAPIENTRY glutSolidCone( double base, double height, GLint slices, GLint stacks )
{
    int i,j;

    /* Step in z and radius as stacks are drawn. */

    GLfloat z0,z1;
    GLfloat r0,r1;

    const GLfloat zStep = (GLfloat)height / ( ( stacks > 0 ) ? stacks : 1 );
    const GLfloat rStep = (GLfloat)base / ( ( stacks > 0 ) ? stacks : 1 );

    /* Scaling factors for vertex normals */

    const GLfloat cosn = ( (GLfloat)height / sqrtf( height * height + base * base ));
    const GLfloat sinn = ( (GLfloat)base   / sqrtf( height * height + base * base ));

    /* Pre-computed circle */

    GLfloat *sint,*cost;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidCone" );

    fghCircleTable(&sint,&cost,-slices,FALSE);

    /* Cover the circular base with a triangle fan... */

    z0 = 0;
    z1 = zStep;

    r0 = (GLfloat)base;
    r1 = r0 - rStep;

    glBegin(GL_TRIANGLE_FAN);

        glNormal3f(0,0,-1);
        glVertex3f(0,0, z0 );

        for (j=0; j<=slices; j++)
            glVertex3f(cost[j]*r0, sint[j]*r0, z0);

    glEnd();

    /* Cover each stack with a quad strip, except the top stack */

    for( i=0; i<stacks-1; i++ )
    {
        glBegin(GL_QUAD_STRIP);

            for(j=0; j<=slices; j++)
            {
                glNormal3f(cost[j]*cosn, sint[j]*cosn, sinn);
                glVertex3f(cost[j]*r0,   sint[j]*r0,   z0  );
                glVertex3f(cost[j]*r1,   sint[j]*r1,   z1  );
            }

            z0 = z1; z1 += zStep;
            r0 = r1; r1 -= rStep;

        glEnd();
    }

    /* The top stack is covered with individual triangles */

    glBegin(GL_TRIANGLES);

        glNormal3f(cost[0]*sinn, sint[0]*sinn, cosn);

        for (j=0; j<slices; j++)
        {
            glVertex3f(cost[j+0]*r0,   sint[j+0]*r0,            z0    );
            glVertex3f(0,              0,              (GLfloat)height);
            glNormal3f(cost[j+1]*sinn, sint[j+1]*sinn,          cosn  );
            glVertex3f(cost[j+1]*r0,   sint[j+1]*r0,            z0    );
        }

    glEnd();

    /* Release sin and cos tables */

    free(sint);
    free(cost);
}

/*
 * Draws a wire cone
 */
void FGAPIENTRY glutWireCone( double base, double height, GLint slices, GLint stacks)
{
    int i,j;

    /* Step in z and radius as stacks are drawn. */

    GLfloat z = 0;
    GLfloat r = (GLfloat)base;

    const GLfloat zStep = (GLfloat)height / ( ( stacks > 0 ) ? stacks : 1 );
    const GLfloat rStep = (GLfloat)base / ( ( stacks > 0 ) ? stacks : 1 );

    /* Scaling factors for vertex normals */

    const GLfloat cosn = ( (GLfloat)height / sqrtf( height * height + base * base ));
    const GLfloat sinn = ( (GLfloat)base   / sqrtf( height * height + base * base ));

    /* Pre-computed circle */

    GLfloat *sint,*cost;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireCone" );

    fghCircleTable(&sint,&cost,-slices,FALSE);

    /* Draw the stacks... */

    for (i=0; i<stacks; i++)
    {
        glBegin(GL_LINE_LOOP);

            for( j=0; j<slices; j++ )
            {
                glNormal3f(cost[j]*sinn, sint[j]*sinn, cosn);
                glVertex3f(cost[j]*r,    sint[j]*r,    z   );
            }

        glEnd();

        z += zStep;
        r -= rStep;
    }

    /* Draw the slices */

    r = (GLfloat)base;

    glBegin(GL_LINES);

        for (j=0; j<slices; j++)
        {
            glNormal3f(cost[j]*sinn, sint[j]*sinn,          cosn  );
            glVertex3f(cost[j]*r,    sint[j]*r,             0     );
            glVertex3f(0,            0,            (GLfloat)height);
        }

    glEnd();

    /* Release sin and cos tables */

    free(sint);
    free(cost);
}


/*
 * Draws a solid cylinder
 */
void FGAPIENTRY glutSolidCylinder(double radius, double height, GLint slices, GLint stacks)
{
    int i,j;

    /* Step in z and radius as stacks are drawn. */
    GLfloat radf = (GLfloat)radius;
    GLfloat z0,z1;
    const GLfloat zStep = (GLfloat)height / ( ( stacks > 0 ) ? stacks : 1 );

    /* Pre-computed circle */

    GLfloat *sint,*cost;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidCylinder" );

    fghCircleTable(&sint,&cost,-slices,FALSE);

    /* Cover the base and top */

    glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0, 0, -1 );
        glVertex3f(0, 0,  0 );
        for (j=0; j<=slices; j++)
          glVertex3f(cost[j]*radf, sint[j]*radf, 0);
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0, 0,          1     );
        glVertex3f(0, 0, (GLfloat)height);
        for (j=slices; j>=0; j--)
          glVertex3f(cost[j]*radf, sint[j]*radf, (GLfloat)height);
    glEnd();

    /* Do the stacks */

    z0 = 0;
    z1 = zStep;

    for (i=1; i<=stacks; i++)
    {
        if (i==stacks)
            z1 = (GLfloat)height;

        glBegin(GL_QUAD_STRIP);
            for (j=0; j<=slices; j++ )
            {
                glNormal3f(cost[j],      sint[j],      0  );
                glVertex3f(cost[j]*radf, sint[j]*radf, z0 );
                glVertex3f(cost[j]*radf, sint[j]*radf, z1 );
            }
        glEnd();

        z0 = z1; z1 += zStep;
    }

    /* Release sin and cos tables */

    free(sint);
    free(cost);
}

/*
 * Draws a wire cylinder
 */
void FGAPIENTRY glutWireCylinder(double radius, double height, GLint slices, GLint stacks)
{
    int i,j;

    /* Step in z and radius as stacks are drawn. */
    GLfloat radf = (GLfloat)radius;
          GLfloat z = 0;
    const GLfloat zStep = (GLfloat)height / ( ( stacks > 0 ) ? stacks : 1 );

    /* Pre-computed circle */

    GLfloat *sint,*cost;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireCylinder" );

    fghCircleTable(&sint,&cost,-slices,FALSE);

    /* Draw the stacks... */

    for (i=0; i<=stacks; i++)
    {
        if (i==stacks)
            z = (GLfloat)height;

        glBegin(GL_LINE_LOOP);

            for( j=0; j<slices; j++ )
            {
                glNormal3f(cost[j],      sint[j],      0);
                glVertex3f(cost[j]*radf, sint[j]*radf, z);
            }

        glEnd();

        z += zStep;
    }

    /* Draw the slices */

    glBegin(GL_LINES);

        for (j=0; j<slices; j++)
        {
            glNormal3f(cost[j],      sint[j],               0     );
            glVertex3f(cost[j]*radf, sint[j]*radf,          0     );
            glVertex3f(cost[j]*radf, sint[j]*radf, (GLfloat)height);
        }

    glEnd();

    /* Release sin and cos tables */

    free(sint);
    free(cost);
}

/*
 * Draws a wire torus
 */
void FGAPIENTRY glutWireTorus( double dInnerRadius, double dOuterRadius, GLint nSides, GLint nRings )
{
  GLfloat  iradius = (float)dInnerRadius, oradius = (float)dOuterRadius;
  GLfloat phi, psi, dpsi, dphi;
  GLfloat *vertex, *normal;
  int    i, j;
  GLfloat spsi, cpsi, sphi, cphi ;

  FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireTorus" );

  if ( nSides < 1 ) nSides = 1;
  if ( nRings < 1 ) nRings = 1;

  /* Allocate the vertices array */
  vertex = (GLfloat *)calloc( sizeof(GLfloat), 3 * nSides * nRings );
  normal = (GLfloat *)calloc( sizeof(GLfloat), 3 * nSides * nRings );

  glPushMatrix();

  dpsi =  2.0f * (GLfloat)M_PI / (GLfloat)(nRings) ;
  dphi = -2.0f * (GLfloat)M_PI / (GLfloat)(nSides) ;
  psi  = 0.0f;

  for( j=0; j<nRings; j++ )
  {
    cpsi = cosf( psi ) ;
    spsi = sinf( psi ) ;
    phi = 0.0f;

    for( i=0; i<nSides; i++ )
    {
      int offset = 3 * ( j * nSides + i ) ;
      cphi = cosf( phi ) ;
      sphi = sinf( phi ) ;
      *(vertex + offset + 0) = cpsi * ( oradius + cphi * iradius ) ;
      *(vertex + offset + 1) = spsi * ( oradius + cphi * iradius ) ;
      *(vertex + offset + 2) =                    sphi * iradius  ;
      *(normal + offset + 0) = cpsi * cphi ;
      *(normal + offset + 1) = spsi * cphi ;
      *(normal + offset + 2) =        sphi ;
      phi += dphi;
    }

    psi += dpsi;
  }

  for( i=0; i<nSides; i++ )
  {
    glBegin( GL_LINE_LOOP );

    for( j=0; j<nRings; j++ )
    {
      int offset = 3 * ( j * nSides + i ) ;
      glNormal3fv( normal + offset );
      glVertex3fv( vertex + offset );
    }

    glEnd();
  }

  for( j=0; j<nRings; j++ )
  {
    glBegin(GL_LINE_LOOP);

    for( i=0; i<nSides; i++ )
    {
      int offset = 3 * ( j * nSides + i ) ;
      glNormal3fv( normal + offset );
      glVertex3fv( vertex + offset );
    }

    glEnd();
  }

  free ( vertex ) ;
  free ( normal ) ;
  glPopMatrix();
}

/*
 * Draws a solid torus
 */
void FGAPIENTRY glutSolidTorus( double dInnerRadius, double dOuterRadius, GLint nSides, GLint nRings )
{
  GLfloat  iradius = (float)dInnerRadius, oradius = (float)dOuterRadius;
  GLfloat phi, psi, dpsi, dphi;
  GLfloat *vertex, *normal;
  int    i, j;
  GLfloat spsi, cpsi, sphi, cphi ;

  FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidTorus" );

  if ( nSides < 1 ) nSides = 1;
  if ( nRings < 1 ) nRings = 1;

  /* Increment the number of sides and rings to allow for one more point than surface */
  nSides ++ ;
  nRings ++ ;

  /* Allocate the vertices array */
  vertex = (GLfloat *)calloc( sizeof(GLfloat), 3 * nSides * nRings );
  normal = (GLfloat *)calloc( sizeof(GLfloat), 3 * nSides * nRings );

  glPushMatrix();

  dpsi =  2.0f * (GLfloat)M_PI / (GLfloat)(nRings - 1) ;
  dphi = -2.0f * (GLfloat)M_PI / (GLfloat)(nSides - 1) ;
  psi  = 0.0f;

  for( j=0; j<nRings; j++ )
  {
    cpsi = cosf( psi ) ;
    spsi = sinf( psi ) ;
    phi = 0.0f;

    for( i=0; i<nSides; i++ )
    {
      int offset = 3 * ( j * nSides + i ) ;
      cphi = cosf( phi ) ;
      sphi = sinf( phi ) ;
      *(vertex + offset + 0) = cpsi * ( oradius + cphi * iradius ) ;
      *(vertex + offset + 1) = spsi * ( oradius + cphi * iradius ) ;
      *(vertex + offset + 2) =                    sphi * iradius  ;
      *(normal + offset + 0) = cpsi * cphi ;
      *(normal + offset + 1) = spsi * cphi ;
      *(normal + offset + 2) =        sphi ;
      phi += dphi;
    }

    psi += dpsi;
  }

    glBegin( GL_QUADS );
  for( i=0; i<nSides-1; i++ )
  {
    for( j=0; j<nRings-1; j++ )
    {
      int offset = 3 * ( j * nSides + i ) ;
      glNormal3fv( normal + offset );
      glVertex3fv( vertex + offset );
      glNormal3fv( normal + offset + 3 );
      glVertex3fv( vertex + offset + 3 );
      glNormal3fv( normal + offset + 3 * nSides + 3 );
      glVertex3fv( vertex + offset + 3 * nSides + 3 );
      glNormal3fv( normal + offset + 3 * nSides );
      glVertex3fv( vertex + offset + 3 * nSides );
    }
  }

  glEnd();

  free ( vertex ) ;
  free ( normal ) ;
  glPopMatrix();
}
#endif /* EGL_VERSION_1_0 */



/* -- INTERFACE FUNCTIONS -------------------------------------------------- */
/* Macro to generate interface functions */
#define DECLARE_SHAPE_INTERFACE(nameICaps)\
    void FGAPIENTRY glutWire##nameICaps( void )\
    {\
        FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWire"#nameICaps );\
        fgh##nameICaps( TRUE );\
    }\
    void FGAPIENTRY glutSolid##nameICaps( void )\
    {\
        FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolid"#nameICaps );\
        fgh##nameICaps( FALSE );\
    }

void FGAPIENTRY glutWireCube( double dSize )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireCube" );
    fghCube( (GLfloat)dSize, TRUE );
}
void FGAPIENTRY glutSolidCube( double dSize )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidCube" );
    fghCube( (GLfloat)dSize, FALSE );
}

DECLARE_SHAPE_INTERFACE(Dodecahedron);
DECLARE_SHAPE_INTERFACE(Icosahedron);
DECLARE_SHAPE_INTERFACE(Octahedron);
DECLARE_SHAPE_INTERFACE(RhombicDodecahedron);

void FGAPIENTRY glutWireSierpinskiSponge ( int num_levels, double offset[3], double scale )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutWireSierpinskiSponge" );
    fghSierpinskiSponge ( num_levels, offset, (GLfloat)scale, TRUE );
}
void FGAPIENTRY glutSolidSierpinskiSponge ( int num_levels, double offset[3], double scale )
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutSolidSierpinskiSponge" );
    fghSierpinskiSponge ( num_levels, offset, (GLfloat)scale, FALSE );
}

DECLARE_SHAPE_INTERFACE(Tetrahedron);


/*** END OF FILE ***/
