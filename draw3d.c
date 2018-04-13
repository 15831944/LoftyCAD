#include "stdafx.h"
#include "LoftyCAD.h"
#include <stdio.h>

// Some standard colors sent to GL.
void
color(OBJECT obj_type, BOOL selected, BOOL highlighted)
{
    switch (obj_type)
    {
    case OBJ_VOLUME:
    case OBJ_POINT:
        break;  // no action here

    case OBJ_EDGE:
        if (selected)
            glColor3d(1.0, 0.0, 0.0);
        else if (highlighted)
            glColor3d(0.0, 1.0, 0.0);
        else
            glColor3d(0.5, 0.5, 0.5);
        break;

    case OBJ_FACE:
        if (selected)
            glColor3d(1.0, 0.0, 0.0);
        else if (highlighted)
            glColor3d(0.0, 0.8, 0.0);
        else
            glColor3d(0.8, 0.8, 0.8);
        break;
    }
}

// Shade in a face.
void
face_shade(Face *face, BOOL selected, BOOL highlighted)
{
    Point   *v;

    gen_view_list(face);
    glBegin(GL_POLYGON);
    color(OBJ_FACE, selected, highlighted);
    for (v = face->view_list; v != NULL; v = (Point *)v->hdr.next)
        glVertex3f(v->x, v->y, v->z);
    glEnd();
}

// Draw any object.
void
draw_object(Object *obj, BOOL selected, BOOL highlighted)
{
    Face *face;
    Edge *edge;
    StraightEdge *se;

    switch (obj->type)
    {
    case OBJ_POINT:
        // Only if selected/highlighted.
        if (selected)
        {
            glLoadName((GLuint)obj);

            // TODO


        }
        break;

    case OBJ_EDGE:
        glLoadName((GLuint)obj);
        edge = (Edge *)obj;
        switch (edge->type)
        {
        case EDGE_STRAIGHT:
            se = (StraightEdge *)edge;
            glBegin(GL_LINES);
            color(OBJ_EDGE, selected, highlighted);
            glVertex3f(se->endpoints[0]->x, se->endpoints[0]->y, se->endpoints[0]->z);
            glVertex3f(se->endpoints[1]->x, se->endpoints[1]->y, se->endpoints[1]->z);
            glEnd();
            break;

        case EDGE_CIRCLE:
        case EDGE_ARC:
        case EDGE_BEZIER:
            break;
        }


        break;

    case OBJ_FACE:
        glLoadName((GLuint)obj);
        face_shade((Face *)obj, selected, highlighted);
        for (edge = ((Face *)obj)->edges; edge != NULL; edge = (Edge *)edge->hdr.next)
            draw_object(&edge->hdr, selected, highlighted);
        break;

    case OBJ_VOLUME:
        glLoadName((GLuint)obj);
        for (face = ((Volume *)obj)->faces; face != NULL; face = (Face *)face->hdr.next)
            draw_object(&face->hdr, selected, highlighted);
        break;
    }
}


// Draw the contents of the main window. Everything happens in here.
void CALLBACK
Draw(BOOL picking, GLint x_pick, GLint y_pick)
{
    float matRot[4][4];
    POINT   pt;
    Object  *obj;

    if (!picking)
    {
        // handle mouse movement actions when starting a new object.
        // Highlight snap targets (use curr_obj for this)
        if (!left_mouse && !right_mouse)
        {
            //if (app_state > STATE_MOVING)  // Pick all the time (aids selection)
            {
                auxGetMouseLoc(&pt.x, &pt.y);
                curr_obj = Pick(pt.x, pt.y);
            }
        }

        // Only clear pixel buffer stuff if not picking (reduces flashing)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_LIGHTING);

        // handle left mouse dragging actions. We must be moving or drawing,
        // otherwise the trackball would have it and we wouldn't be here.
        if (left_mouse)
        {
            auxGetMouseLoc(&pt.x, &pt.y);

            // Use XYZ coordinates rather than mouse position deltas, as we may
            // have snapped and we want to preserve the accuracy. Just use mouse
            // position to check for gross movement.
            if (pt.x != left_mouseX || pt.y != left_mouseY)
            {
                Point   new_point, p1, p3;
                Point   *p00, *p01, *p02, *p03;
                StraightEdge *se;
                Face *rf;
                Plane norm;

                switch (app_state)
                {
                case STATE_MOVING:
                    // Move the selection by a delta in XYZ within the facing plane
                    intersect_ray_plane(pt.x, pt.y, facing_plane, &new_point);
                    for (obj = selection; obj != NULL; obj = obj->next)
                    {
                        move_obj
                            (
                            obj->prev,
                            new_point.x - picked_point.x,
                            new_point.x - picked_point.y,
                            new_point.x - picked_point.z
                            );
                    }
                    break;

                case STATE_DRAWING_EDGE:
                    if (picked_plane == NULL)
                    {
                        // Uhoh. We don't have a plane yet. TODO: Check if the mouse has moved into
                        // a face object, and use that.


                    }

                    // Move the end point of the current edge
                    intersect_ray_plane(pt.x, pt.y, picked_plane, &new_point);

                    // If first move, create the edge here.
                    if (curr_obj == NULL)
                    {
                        curr_obj = (Object *)edge_new(EDGE_STRAIGHT);
                        se = (StraightEdge *)curr_obj;
                        // TODO: share points if snapped onto an existing edge endpoint,
                        // and the edge is not referenced by a face. For now, just create points...
                        se->endpoints[0] = point_newp(&picked_point);
                        se->endpoints[1] = point_newp(&new_point);
                    }
                    else
                    {
                        se = (StraightEdge *)curr_obj;
                        se->endpoints[1]->x = new_point.x;
                        se->endpoints[1]->y = new_point.y;
                        se->endpoints[1]->z = new_point.z;
                    }

                    break;

                case STATE_DRAWING_RECT:
                    if (picked_plane == NULL)
                    {
                        // Uhoh. We don't have a plane yet. TODO: Check if the mouse has moved into
                        // a face object, and use that.


                    }

                    // Move the opposite corner point
                    intersect_ray_plane(pt.x, pt.y, picked_plane, &new_point);

                    // generate the other corners. The rect goes in the 
                    // order picked-p1-new-p3.
                    switch (facing_index)
                    {
                    case PLANE_XY:
                    case PLANE_MINUS_XY:
                        p1.x = new_point.x;
                        p1.y = picked_point.y;
                        p1.z = picked_point.z;
                        p3.x = picked_point.x;
                        p3.y = new_point.y;
                        p3.z = picked_point.z;
                        break;

                    case PLANE_YZ:
                    case PLANE_MINUS_YZ:
                        p1.x = picked_point.x;
                        p1.y = new_point.y;
                        p1.z = picked_point.z;
                        p3.x = picked_point.x;
                        p3.y = picked_point.y;
                        p3.z = new_point.z;
                        break;

                    case PLANE_XZ:
                    case PLANE_MINUS_XZ:
                        p1.x = new_point.x;
                        p1.y = picked_point.y;
                        p1.z = picked_point.z;
                        p3.x = picked_point.x;
                        p3.y = picked_point.y;
                        p3.z = new_point.z;
                        break;

                    case PLANE_GENERAL:
                        ASSERT(FALSE, "Not implemented yet");
                        break;
                    }

                    // Make sure the normal vector is pointing towards the eye,
                    // swapping p1 and p3 if necessary.
                    normal3(&p1, &picked_point, &p3, &norm);
                    {
                        char buf[256];
                        sprintf_s(buf, 256, "%f %f %f\r\n", norm.A, norm.B, norm.C);
                        Log(buf);
                    }
                    if (dot(norm.A, norm.B, norm.C, facing_plane->A, facing_plane->B, facing_plane->C) < 0)
                    {
                        Point swap = p1;
                        p1 = p3;
                        p3 = swap;
                    }


                    // If first move, create the rect and its edges here.
                    // Create a special rect with no edges but a 4-point view list.
                    // Only create the edges when completed, as we have to keep the anticlockwise
                    // order of the points no matter how the mouse is dragged around.
                    if (curr_obj == NULL)
                    {
                        rf = face_new(*picked_plane);
                        rf->type = FACE_RECT;

                        // generate four points for the view list
                        p00 = point_newp(&picked_point);
                        p01 = point_newp(&p1);
                        p02 = point_newp(&new_point);
                        p03 = point_newp(&p3);

                        // put the points into the view list
                        rf->view_list = p00;
                        p00->hdr.next = (Object *)p01;
                        p01->hdr.next = (Object *)p02;
                        p02->hdr.next = (Object *)p03;

                        // set it valid, so Draw doesn't try to overwrite it
                        rf->view_valid = TRUE;

                        curr_obj = (Object *)rf;
                    }
                    else
                    {
                        rf = (Face *)curr_obj;

                        // Dig out the points that need updating, and update them
                        p01 = (Point *)rf->view_list->hdr.next;
                        p02 = (Point *)p01->hdr.next;
                        p03 = (Point *)p02->hdr.next;

                        // Update the points
                        p01->x = p1.x;
                        p01->y = p1.y;
                        p01->z = p1.z;
                        p02->x = new_point.x;
                        p02->y = new_point.y;
                        p02->z = new_point.z;
                        p03->x = p3.x;
                        p03->y = p3.y;
                        p03->z = p3.z;
                    }

                    break;

                case STATE_DRAWING_CIRCLE:
                case STATE_DRAWING_ARC:
                case STATE_DRAWING_BEZIER:
                case STATE_DRAWING_MEASURE:
                case STATE_DRAWING_EXTRUDE:
                    ASSERT(FALSE, "Not implemented");
                    break;

                default:
                    ASSERT(FALSE, "Mouse drag in starting state");
                    break;
                }

                left_mouseX = pt.x;
                left_mouseY = pt.y;
            }

        }

        // handle panning with right mouse drag. 
        // Perhaps use gluLookAt here instead if it gives a more natural pan.
        if (right_mouse)
        {
            auxGetMouseLoc(&pt.x, &pt.y);
            if (pt.y != right_mouseY)
            {
                GLint viewport[4], width, height;

                glGetIntegerv(GL_VIEWPORT, viewport);
                width = viewport[2];
                height = viewport[3];
                if (width > height)
                {
                    // Y window coords need inverting for GL
                    xTrans += 2 * zTrans * (float)(right_mouseX - pt.x) / height;
                    yTrans += 2 * zTrans * (float)(pt.y - right_mouseY) / height;
                }
                else
                {
                    xTrans += 2 * zTrans * (float)(right_mouseX - pt.x) / width;
                    yTrans += 2 * zTrans * (float)(pt.y - right_mouseY) / width;
                }

                Position(FALSE, 0, 0);
                right_mouseX = pt.x;
                right_mouseY = pt.y;
            }
            else
            {
                // right mouse click - handle context menu

            }
        }

        // handle zooming. No state change here.
        if (zoom_delta != 0)
        {
            zTrans += 0.01f * zoom_delta;
            Position(FALSE, 0, 0);
            zoom_delta = 0;
        }
    }

    // handle picking, or just position for viewing
    Position(picking, x_pick, y_pick);

    // draw contents
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    trackball_CalcRotMatrix(matRot);
    glMultMatrixf(&(matRot[0][0]));

    // traverse object tree
    glInitNames();
    glPushName(0);
    for (obj = object_tree; obj != NULL; obj = obj->next)
        draw_object(obj, FALSE, FALSE);

    // draw selection
    for (obj = selection; obj != NULL; obj = obj->next)
        draw_object(obj->prev, TRUE, FALSE);

    // draw any current object not yet added to the object tree,
    // or any under highlighting
    if (curr_obj != NULL)
        draw_object(curr_obj, FALSE, TRUE);

    // Draw axes XYZ in RGB. TODO: move these to bottom corner and get them out of the way
    glLoadName(0);
    glBegin(GL_LINES);
    glColor3d(1.0, 0.0, 0.0);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(1.0, 0.0, 0.0);
    glEnd();
    glBegin(GL_LINES);
    glColor3d(0.0, 1.0, 0.0);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(0.0, 1.0, 0.0);
    glEnd();
    glBegin(GL_LINES);
    glColor3d(0.0, 0.0, 1.0);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(0.0, 0.0, 1.0);
    glEnd();

#if 0
    // TEST draw a polygon
    glBegin(GL_POLYGON);
    glColor3d(0.0, 0.0, 0.0);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(1.0, 0.0, 0.0);
    glVertex3d(1.0, 0.5, 0.0);
    glVertex3d(0.5, 0.5, 0.0);
    glVertex3d(0.5, 1.0, 0.0);
    glVertex3d(0.0, 1.0, 0.0);
    glEnd();
#endif

    glFlush();
    if (!picking)
        auxSwapBuffers();
}
