#include "stdafx.h"
#include "LoftyCAD.h"
#include <stdio.h>

// Triangulation of view lists, including clipping to volumes

// Tessellator for rendering to GL.
GLUtesselator *rtess = NULL;


// Clear a bounding box to empty
void
clear_bbox(Bbox *box)
{
    box->xmin = LARGE_COORD; 
    box->xmax = -LARGE_COORD;
    box->ymin = LARGE_COORD;
    box->ymax = -LARGE_COORD;
    box->zmin = LARGE_COORD;
    box->zmax = -LARGE_COORD;
}

// Expand a bounding box to include a point
void
expand_bbox(Bbox *box, Point *p)
{
    if (p->x < box->xmin)
        box->xmin = p->x;
    if (p->x > box->xmax)
        box->xmax = p->x;

    if (p->y < box->ymin)
        box->ymin = p->y;
    if (p->y > box->ymax)
        box->ymax = p->y;

    if (p->z < box->zmin)
        box->zmin = p->z;
    if (p->z > box->zmax)
        box->zmax = p->z;
}

// Expand a bounding box to include a point given by (x, y, z)
void
expand_bbox_coords(Bbox* box, float x, float y, float z)
{
    if (x < box->xmin)
        box->xmin = x;
    if (x > box->xmax)
        box->xmax = x;

    if (y < box->ymin)
        box->ymin = y;
    if (y > box->ymax)
        box->ymax = y;

    if (z < box->zmin)
        box->zmin = z;
    if (z > box->zmax)
        box->zmax = z;
}

// Form the union of two bboxes
void 
union_bbox(Bbox *box1, Bbox *box2, Bbox *u)
{
    u->xmin = min(box1->xmin, box2->xmin);
    u->xmax = max(box1->xmax, box2->xmax);
    u->ymin = min(box1->ymin, box2->ymin);
    u->ymax = max(box1->ymax, box2->ymax);
    u->zmin = min(box1->zmin, box2->zmin);
    u->zmax = max(box1->zmax, box2->zmax);
}

// Return TRUE if two bboxes intersect. Make sure to allow touching bboxes.
BOOL 
intersects_bbox(Bbox *box1, Bbox *box2)
{
    if (box1->xmax < box2->xmin - SMALL_COORD)
        return FALSE;
    if (box1->xmin > box2->xmax + SMALL_COORD)
        return FALSE;

    if (box1->ymax < box2->ymin - SMALL_COORD)
        return FALSE;
    if (box1->ymin > box2->ymax + SMALL_COORD)
        return FALSE;

    if (box1->zmax < box2->zmin - SMALL_COORD)
        return FALSE;
    if (box1->zmin > box2->zmax + SMALL_COORD)
        return FALSE;

    return TRUE;
}

// Enforce constraints on an arc edge e, when one of its points p has been moved.
void
enforce_arc_constraints(Edge *e, Point *p, float dx, float dy, float dz)
{
    ArcEdge *ae = (ArcEdge *)e;

    if (p == e->endpoints[0])
    {
        // Endpoint 0 has moved, this will force a recalculation of the radius.
        // Move endpoint 1 to suit the new radius
        float rad = length(ae->centre, e->endpoints[0]);
        Plane e1;

        e1.A = e->endpoints[1]->x - ae->centre->x;
        e1.B = e->endpoints[1]->y - ae->centre->y;
        e1.C = e->endpoints[1]->z - ae->centre->z;
        normalise_plane(&e1);
        e->endpoints[1]->x = ae->centre->x + e1.A * rad;
        e->endpoints[1]->y = ae->centre->y + e1.B * rad;
        e->endpoints[1]->z = ae->centre->z + e1.C * rad;
    }
    else if (p == e->endpoints[1])
    {
        // The other endpoint has moved. Update the first one similarly
        float rad = length(ae->centre, e->endpoints[1]);
        Plane e0;

        e0.A = e->endpoints[0]->x - ae->centre->x;
        e0.B = e->endpoints[0]->y - ae->centre->y;
        e0.C = e->endpoints[0]->z - ae->centre->z;
        normalise_plane(&e0);
        e->endpoints[0]->x = ae->centre->x + e0.A * rad;
        e->endpoints[0]->y = ae->centre->y + e0.B * rad;
        e->endpoints[0]->z = ae->centre->z + e0.C * rad;
    }
    else if (p == ae->centre)
    {
        // The centre has moved. We are moving the whole edge.
        e->endpoints[0]->x += dx;
        e->endpoints[0]->y += dy;
        e->endpoints[0]->z += dz;
        e->endpoints[1]->x += dx;
        e->endpoints[1]->y += dy;
        e->endpoints[1]->z += dz;
    }
}

// Mark all view lists as invalid for parts of a parent object, when a part of it
// has moved.
//
// Do this all the way down (even though gen_view_list will recompute all the children anyway)
// since we may encounter arc edges that need constraints updated when, say, an 
// endpoint has moved. We also pass in how much it has moved.
void
invalidate_all_view_lists(Object *parent, Object *obj, float dx, float dy, float dz)
{
    Group *group;
    Volume *vol;
    Face *f;
    Object *o;
    int i;

    switch (parent->type)
    {
    case OBJ_GROUP:
        group = (Group *)parent;
        clear_bbox(&group->bbox);
        for (o = group->obj_list.head; o != NULL; o = o->next)
            invalidate_all_view_lists(o, obj, dx, dy, dz);
        break;

    case OBJ_VOLUME:
        vol = (Volume *)parent;

        // Clear the current bbox so it gets updated with the view list.
        // Mark this volume as needing a new mesh update.
        clear_bbox(&vol->bbox);
        vol->mesh_valid = FALSE;

        for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
            invalidate_all_view_lists((Object *)f, obj, dx, dy, dz);
        break;

    case OBJ_FACE:
        f = (Face *)parent;
        f->view_valid = FALSE;
        for (i = 0; i < f->n_edges; i++)
            invalidate_all_view_lists((Object *)f->edges[i], obj, dx, dy, dz);
        break;

    case OBJ_EDGE:
        switch (((Edge *)parent)->type & ~EDGE_CONSTRUCTION)
        {
        case EDGE_ARC:
            ((Edge *)parent)->view_valid = FALSE;

            // Check that the obj in question is an endpoint or the centre,
            // and update the other point(s) in the arc to suit.
            if (obj->type == OBJ_POINT)
            {
                enforce_arc_constraints((Edge *)parent, (Point *)obj, dx, dy, dz);
            }
            else if (obj->type == OBJ_EDGE)
            {
                enforce_arc_constraints((Edge *)parent, ((Edge *)obj)->endpoints[0], dx, dy, dz);
                enforce_arc_constraints((Edge *)parent, ((Edge *)obj)->endpoints[1], dx, dy, dz);
            }

            break;

        case EDGE_BEZIER:
            ((Edge *)parent)->view_valid = FALSE;
            break;
        }
        break;
    }
}

// Generate volume view lists for all volumes in tree. Return TRUE if something new
// was generated, FALSE if everything was up to date.
BOOL
gen_view_list_tree_volumes(Group *tree)
{
    Object *obj;
    Volume *vol;
    Group *group;
    BOOL rc = FALSE;
    Bbox *box;

    // generate all view lists for all volumes, to make sure they are all up to date
    for (obj = tree->obj_list.head; obj != NULL; obj = obj->next)
    {
        switch (obj->type)
        {
        case OBJ_VOLUME:
            vol = (Volume * )obj;
            if (gen_view_list_vol(vol))
                rc = TRUE;

            // update the group bbox with the volume bbox
            union_bbox(&vol->bbox, &tree->bbox, &tree->bbox);
            break;

        case OBJ_GROUP:
            group = (Group *)obj;
            if (gen_view_list_tree_volumes(group))
                rc = TRUE;

            // update the group bbox centre
            box = &group->bbox;
            box->xc = (box->xmin + box->xmax) / 2;
            box->yc = (box->ymin + box->ymax) / 2;
            box->zc = (box->zmin + box->zmax) / 2;
            break;
        }
    }

    // clear and reinit the tree mesh if any volumes needed regenerating
    if (rc)
    {
        if (tree->mesh != NULL)
            mesh_destroy(tree->mesh);
        tree->mesh = NULL;
        tree->mesh_valid = FALSE;
    }
    return rc;
}

// Mesh merge operations. The mesh1 pointer may change if CGAL is being called to do
// separate (non-in-place) output as a workaround to CGAL issue #4522.
BOOL
mesh_merge_op(OPERATION op, Mesh **mesh1, Mesh *mesh2)
{
    BOOL rc;

    switch (op)
    {
    case OP_UNION:
    default:
        rc = mesh_union(mesh1, mesh2);
        break;
    case OP_INTERSECTION:
        rc = mesh_intersection(mesh1, mesh2);
        break;
    case OP_DIFFERENCE:
        rc = mesh_difference(mesh1, mesh2);
        break;
    }
    return rc;
}

// Generate mesh for a class of operations for a group tree (or the object tree)
void
gen_view_list_tree_surfaces_op(OPERATION op, Group *tree, Group *parent_tree)
{
    Object *obj;
    Face *f;
    Volume *vol;
    Group *group;
    POINT pt = { wWidth / 2, wHeight / 2 };
    char buf[64];

    for (obj = tree->obj_list.head; obj != NULL; obj = obj->next)
    {
        switch (obj->type)
        {
        case OBJ_VOLUME:
            vol = (Volume *)obj;
            if (vol->op != op)
                break;
            if (materials[vol->material].hidden)
                break;

            // update the triangle mesh for the volume
            if (vol->xform != NULL)
                link((Object *)vol->xform, &xform_list);
            for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
                gen_view_list_surface(f);
#ifdef DEBUG_WRITE_VOL_MESH
            mesh_write_off("vol", obj->ID, vol->mesh);
#endif
            if (vol->xform != NULL)
                delink((Object *)vol->xform, &xform_list);

            vol->mesh_valid = TRUE;
            if (!parent_tree->mesh_valid)
            {
                // First one: copy vol->mesh into tree->mesh
                parent_tree->mesh = mesh_copy(vol->mesh);
                parent_tree->mesh_valid = TRUE;
                vol->mesh_merged = TRUE;
#ifdef DEBUG_WRITE_VOL_MESH
                sprintf_s(buf, 64, "Copied vol %d\r\n", obj->ID);
                Log(buf);
#endif
            }
            else
            {
                show_hint_at(pt, obj_description(obj, buf, 64, FALSE), FALSE);
                process_messages();

                // Merge volume mesh to tree mesh
                vol->mesh_merged = mesh_merge_op(op, &parent_tree->mesh, vol->mesh);
                if (!vol->mesh_merged)
                    parent_tree->mesh_complete = FALSE;
#ifdef DEBUG_WRITE_VOL_MESH
                mesh_write_off("merge_vol", obj->ID, parent_tree->mesh);
#endif
            }
            break;

        case OBJ_GROUP:
            group = (Group *)obj;
            if (group->xform != NULL)
                link((Object *)group->xform, &xform_list);
            if (group->op == OP_NONE)
            {
                // Render contents of group as if in the parent
                gen_view_list_tree_surfaces_op(op, group, parent_tree);
            }
            else
            {
                if (group->op != op)
                    break;

                // Render group and merge it with parent using group op
                gen_view_list_tree_surfaces(group, group);
                group->mesh_valid = TRUE;
                if (!parent_tree->mesh_valid)
                {
                    // First one: copy vol->mesh into tree->mesh
                    parent_tree->mesh = mesh_copy(group->mesh);
                    parent_tree->mesh_valid = TRUE;
                    group->mesh_merged = TRUE;
#ifdef DEBUG_WRITE_VOL_MESH
                    sprintf_s(buf, 64, "Copied group %d\r\n", obj->ID);
                    Log(buf);
#endif
                }
                else
                {
                    show_hint_at(pt, obj_description(obj, buf, 64, FALSE), FALSE);
                    process_messages();

                    // Merge group mesh to tree mesh
                    group->mesh_merged = mesh_merge_op(op, parent_tree->mesh, group->mesh);
                    if (!group->mesh_merged)
                        parent_tree->mesh_complete = FALSE;
#ifdef DEBUG_WRITE_VOL_MESH
                    mesh_write_off("merge_group", obj->ID, parent_tree->mesh);
#endif
                }
            }
            if (group->xform != NULL)
                delink((Object *)group->xform, &xform_list);
            break;
        }
    }
}

// Generate mesh for entire tree (a group or the object tree)
// TODO_MESH Get it to throw up a message box if it can't complete the mesh, with a helpful-ish error message..
// Make sure it doesn't crash program!
void
gen_view_list_tree_surfaces(Group *tree, Group *parent_tree)
{
    // If the parent tree is up to date, we have nothing to do. (but don't do this
    // check if recursing)
    if (tree == parent_tree && parent_tree->mesh_valid)
        return;

    suppress_drawing = TRUE;
    parent_tree->mesh_complete = TRUE;

    // precedence order: unions, then differences, then intersections
    gen_view_list_tree_surfaces_op(OP_UNION, tree, parent_tree);
    gen_view_list_tree_surfaces_op(OP_DIFFERENCE, tree, parent_tree);
    gen_view_list_tree_surfaces_op(OP_INTERSECTION, tree, parent_tree);
    hide_hint();
    suppress_drawing = FALSE;
}

// Regenerate the view lists for all faces of a volume, and also do some special stuff that
// only volumes need (initialise the vol surface mesh). Return TRUE if volume was regenerated,
// or FALSE if everything was up to date.
BOOL
gen_view_list_vol(Volume *vol)
{
    Face *f;
    Bbox *box = &vol->bbox;

    for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
    {
        if (!f->view_valid)
            break;
    }
    if (f == NULL && vol->mesh_valid)
        return FALSE;     // all faces and mesh are valid, nothing to do

    if (f != NULL)
    {
        // if any face is not valid, invalidate them all
        for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
            f->view_valid = FALSE;
    }

    // clear out the point bucket and free all its points
    free_bucket_points(vol->point_bucket);

    // create a new mesh
    if (vol->mesh != NULL)
        mesh_destroy(vol->mesh);
    vol->mesh = mesh_new(vol->material);

    // generate view lists for all the faces
    for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
        gen_view_list_face(f);

    // update the volume bbox centre
    box->xc = (box->xmin + box->xmax) / 2;
    box->yc = (box->ymin + box->ymax) / 2;
    box->zc = (box->zmin + box->zmax) / 2;

    // update the triangle mesh for the volume. NOT HERE as the transforms don't work.
    //for (f = (Face *)vol->faces.head; f != NULL; f = (Face *)f->hdr.next)
    //    gen_view_list_surface(f);

    //vol->mesh_valid = TRUE;
    return TRUE;
}

#define SUPPORT_BARREL_FACES
#define SUPPORT_BEZIER_FACES

// Regenerate the unclipped view list for a face. While here, also calculate the outward
// normal for the face. 
void
gen_view_list_face(Face* face)
{
    int i, c, side_nsteps;
    float step, t;
    Edge* e, *e0, *e1;
    Point* last_point;
    Point* p, * v, * w, *s0, *s1;
    ArcEdge* ae0, *ae1;
    ListHead* list, **elists, **slists;
    ListHead internal;
    int first_arc;
    BOOL first_arc_forward;

    //char buf[256];

    // Index struct to control order of gathering view list point an barrel/bezier edges
    struct
    {
        ListHead* list;     // Which list to gather points into
        int       rev;      // 0 = forward, 1 = reverse
    } indx[4];

    if (face->view_valid)
        return;

    free_view_list_face(face);

    switch (face->type & ~FACE_CONSTRUCTION)
    {
    case FACE_RECT:
    case FACE_CIRCLE:
    case FACE_FLAT:
        // For flat faces, construct the view list directly, as there is only one facet. 
        // The view list just goes around all the edges in order until the starting point is
        // reached again.
        list = &face->view_list;

        // Add points at tail of list, to preserve order
        // First the start point
        p = point_newpv(face->initial_point);
        if (face->vol != NULL)
            expand_bbox(&face->vol->bbox, p);
        link_tail((Object*)p, list);

#if DEBUG_VIEW_LIST_RECT_FACE
        sprintf_s(buf, 256, "Face %d IP %d\r\n", face->hdr.ID, face->initial_point->hdr.ID);
        Log(buf);
        sprintf_s(buf, 256, "%d %d\r\n", ((StraightEdge*)face->edges[0])->endpoints[0]->hdr.ID, ((StraightEdge*)face->edges[0])->endpoints[1]->hdr.ID);
        Log(buf);
        sprintf_s(buf, 256, "%d %d\r\n", ((StraightEdge*)face->edges[1])->endpoints[0]->hdr.ID, ((StraightEdge*)face->edges[1])->endpoints[1]->hdr.ID);
        Log(buf);
        sprintf_s(buf, 256, "%d %d\r\n", ((StraightEdge*)face->edges[2])->endpoints[0]->hdr.ID, ((StraightEdge*)face->edges[2])->endpoints[1]->hdr.ID);
        Log(buf);
        sprintf_s(buf, 256, "%d %d\r\n", ((StraightEdge*)face->edges[3])->endpoints[0]->hdr.ID, ((StraightEdge*)face->edges[3])->endpoints[1]->hdr.ID);
        Log(buf);
#endif
        last_point = face->initial_point;
        c = 0;

        for (i = 0; i < face->n_edges; i++)
        {
            e = face->edges[i];

            // Then the subsequent points. Edges will follow in order, but their points
            // may be reversed.
            switch (e->type & ~EDGE_CONSTRUCTION)
            {
            case EDGE_STRAIGHT:
                if (last_point == e->endpoints[0])
                {
                    last_point = e->endpoints[1];
                }
                else if (last_point == e->endpoints[1])
                {
                    last_point = e->endpoints[0];
                }
                else
                {
                    // Starting a new contour in a list of edges (look at next contour array to see which point starts)
                    int idx = 0;

                    c++;
                    if (c < face->n_contours)
                        idx = face->contours[c].ip_index;

                    p = point_newpv(e->endpoints[idx]);
                    p->flags = FLAG_NEW_CONTOUR;
                    if (face->vol != NULL)
                        expand_bbox(&face->vol->bbox, p);
                    link_tail((Object*)p, list);
                    last_point = e->endpoints[1 - idx];
                }

                p = point_newpv(last_point);
                if (face->vol != NULL)
                    expand_bbox(&face->vol->bbox, p);
                link_tail((Object*)p, list);
                break;

            case EDGE_ARC:
                gen_view_list_arc((ArcEdge*)e);
                goto copy_view_list_flat;

            case EDGE_BEZIER:
                gen_view_list_bez((BezierEdge*)e);
            copy_view_list_flat:
                if (last_point == e->endpoints[0])
                {
                    last_point = e->endpoints[1];

                    // copy the view list forwards. Skip the first point as it has already been added
                    for (v = (Point*)e->view_list.head->next; v != NULL; v = (Point*)v->hdr.next)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }
                }
                else
                {
                    ASSERT(last_point == e->endpoints[1], "Point order messed up");
                    last_point = e->endpoints[0];

                    // copy the view list backwards, skipping the last point.
                    for (v = (Point*)e->view_list.tail->prev; v != NULL; v = (Point*)v->hdr.prev)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }
                }

                p = point_newpv(last_point);
                if (face->vol != NULL)
                    expand_bbox(&face->vol->bbox, p);
                link_tail((Object*)p, list);
                break;
            }
        }

        // calculate the normal vector.  Store a new refpt here too, in case something has moved.
        polygon_normal((Point*)face->view_list.head, &face->normal);
        face->normal.refpt = *face->edges[0]->endpoints[0];

        // Update the 2D view list
        update_view_list_2D(face);
        break;

    case FACE_CYLINDRICAL:
        // For these faces, there are 4 edges only. Two are curved (arcs or beziers) the other two straight.
        // Corral the view lists of the non-straight edges into two lists, pointing in the same direction.
        // The first curved edge is copied forward, the second backward (modulo any edge endpoint ordering issues)
        //              E0
        //          <-----------
        //          |  curve   |
        // side     |          | side
        // straight |          | straight
        //          |   E1     |
        //          <-----------
        //             curve

        elists = (ListHead**)malloc(2 * sizeof(ListHead*));
        elists[0] = (ListHead*)calloc(1, sizeof(ListHead));     // copy of view list of first curved edge
        elists[1] = (ListHead*)calloc(1, sizeof(ListHead));     // copy of view list of second curved edge

        last_point = face->initial_point;
        c = 0;

        for (i = 0; i < face->n_edges; i++)
        {
            e = face->edges[i];

            switch (e->type & ~EDGE_CONSTRUCTION)
            {
            case EDGE_STRAIGHT:
                // Update last_point to the beginning of the next edge, which must be a curved edge.
                if (last_point == e->endpoints[0])
                {
                    last_point = e->endpoints[1];
                }
                else
                {
                    ASSERT(last_point == e->endpoints[1], "Straight edge doesn't join up");
                    last_point = e->endpoints[0];
                }

                break;

            case EDGE_ARC:
                list = elists[c];
                gen_view_list_arc((ArcEdge*)e);
                goto copy_view_list_cyl;

            case EDGE_BEZIER:
                list = elists[c];
                gen_view_list_bez((BezierEdge*)e);

            copy_view_list_cyl:
                if (last_point == e->endpoints[c])
                {
                    last_point = e->endpoints[1 - c];

                    // copy the view list forwards. 
                    for (v = (Point*)e->view_list.head; v != NULL; v = (Point*)v->hdr.next)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }

                    // Copy the Bezier control points in the correct order
                    if ((e->type & ~EDGE_CONSTRUCTION) == EDGE_BEZIER)
                    {
                        BezierEdge* be = (BezierEdge*)e;

                        be->bezctl[0] = e->endpoints[0];
                        be->bezctl[1] = be->ctrlpoints[0];
                        be->bezctl[2] = be->ctrlpoints[1];
                        be->bezctl[3] = e->endpoints[1];
                    }
                }
                else
                {
                    ASSERT(last_point == e->endpoints[1 - c], "Point order messed up");
                    last_point = e->endpoints[c];

                    // copy the view list backwards.
                    for (v = (Point*)e->view_list.tail; v != NULL; v = (Point*)v->hdr.prev)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }
                    if ((e->type & ~EDGE_CONSTRUCTION) == EDGE_BEZIER)
                    {
                        BezierEdge* be = (BezierEdge*)e;

                        be->bezctl[0] = e->endpoints[1];
                        be->bezctl[1] = be->ctrlpoints[1];
                        be->bezctl[2] = be->ctrlpoints[0];
                        be->bezctl[3] = e->endpoints[0];
                    }
                }
                c++;    // update list index to process the other curved edge
                break;
            }
        }
        ASSERT(c == 2, "Should be exactly 2 curved edges in a cylinder face");

        // Join up corresponding groups of 2 points in each list into facets.

        for
        (
            v = (Point*)elists[0]->head, w = (Point*)elists[1]->head;
            v->hdr.next != NULL;
            v = (Point*)v->hdr.next, w = (Point*)w->hdr.next
        )
        {
            Point* vnext = (Point*)v->hdr.next;
            Point* wnext = (Point*)w->hdr.next;
            Plane norm;

            // A new facet point containing the normal
            normal3(v, w, vnext, &norm);  
            p = point_newv(norm.A, norm.B, norm.C);
            p->flags = FLAG_NEW_FACET;
            link_tail((Object*)p, &face->view_list);

            // Four points for the quad
            p = point_newpv(v);
            link_tail((Object*)p, &face->view_list);
            p = point_newpv(vnext);
            link_tail((Object*)p, &face->view_list);
            p = point_newpv(wnext);
            link_tail((Object*)p, &face->view_list);
            p = point_newpv(w);
            link_tail((Object*)p, &face->view_list);
        }

        free_point_list(elists[0]);
        free_point_list(elists[1]);
        free(elists);
        break;

#ifdef SUPPORT_BARREL_FACES
    case FACE_BARREL:
        // For these faces, there are 4 edges only. Two are arcs. 
        // Find the first arc.
        // Determine which is the first arc. If there are two of them, I need a better idea...
        e0 = face->edges[0];
        e1 = face->edges[1];
        if (e0->type != EDGE_ARC)
            first_arc = 1;
        else if (e1->type != EDGE_ARC)
            first_arc = 0;
        else  // they are both arcs, what to do? TODO_BARREL: Set it to 0 just for now.
        {
            first_arc = 0;
        }

        // Find the number of steps in the side edges (this count includes first and last points)
        side_nsteps = face->edges[1 - first_arc]->nsteps;

        // Corral the view lists of the arc edges into two lists, pointing in the same direction.
        // Also do the same with the side edges.
        // Create internal arc edges that interpolate between the first side edge and its opposite number,
        // build an array of edge list heads for them, then join successive list rows ito facets
        // in the same way as the cylinder case above.
        //          E0
        //      <-----------
        //      |   arc    |
        // side |          | side
        //  S1  |          |  S0
        //      v   E1     v
        //      <-----------
        //          arc
        //
        // leave room in pointer array for pointers to VL's of internal arcs
        elists = (ListHead**)malloc((side_nsteps + 1) * sizeof(ListHead*));
        elists[0] = (ListHead*)calloc(1, sizeof(ListHead));                 // copy of view list of first arc edge
        elists[side_nsteps] = (ListHead*)calloc(1, sizeof(ListHead));     // copy of view list of second arc edge 

        slists = (ListHead**)malloc(2 * sizeof(ListHead*));
        slists[0] = (ListHead*)calloc(1, sizeof(ListHead));     // copy of view list of first side edge
        slists[1] = (ListHead*)calloc(1, sizeof(ListHead));     // copy of view list of second side edge


        if (first_arc == 0)
        {
            // IP starts the arc. First edge is forward. (edges always go anticlockwise)
            //          
            //      <----------IP
            //      |   arc    |
            // side |          | side
            //      |          |
            //      v          v
            //      <-----------
            //          arc
            // E0F, S1F, EnR, S0R
            indx[0].list = elists[0];
            indx[0].rev = 0;
            indx[1].list = slists[1];
            indx[1].rev = 0;
            indx[2].list = elists[side_nsteps];
            indx[2].rev = 1;
            indx[3].list = slists[0];
            indx[3].rev = 1;
        }
        else
        {
            // IP starts the side edge. First edge is reverse.
            //          
            //      <-----------
            //      |   arc    |
            // side |          | side
            //      |          |
            //      v          v
            //      <---------IP
            //          arc
             // S0R, E0F, S1F, EnR
            indx[0].list = slists[0];
            indx[0].rev = 1;
            indx[1].list = elists[0];
            indx[1].rev = 0;
            indx[2].list = slists[1];
            indx[2].rev = 0;
            indx[3].list = elists[side_nsteps];
            indx[3].rev = 1;
        }

        last_point = face->initial_point;
        first_arc_forward = FALSE;
        for (i = 0; i < face->n_edges; i++)
        {
            e = face->edges[i];
            list = indx[i].list;
            c = indx[i].rev;

            switch (e->type & ~EDGE_CONSTRUCTION)
            {
            case EDGE_ARC:
                gen_view_list_arc((ArcEdge*)e);
                goto copy_view_list_barrel;

            case EDGE_BEZIER:
                gen_view_list_bez((BezierEdge*)e);

            copy_view_list_barrel:
                if (last_point == e->endpoints[c])
                {
                    last_point = e->endpoints[1 - c];
                    if (i == first_arc)
                        first_arc_forward = TRUE;

                    // copy the view list forwards. 
                    for (v = (Point*)e->view_list.head; v != NULL; v = (Point*)v->hdr.next)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }

                    // Copy the Bezier control points in the correct order
                    if ((e->type & ~EDGE_CONSTRUCTION) == EDGE_BEZIER)
                    {
                        BezierEdge* be = (BezierEdge*)e;

                        be->bezctl[0] = e->endpoints[0];
                        be->bezctl[1] = be->ctrlpoints[0];
                        be->bezctl[2] = be->ctrlpoints[1];
                        be->bezctl[3] = e->endpoints[1];
                    }
                }
                else
                {
                    ASSERT(last_point == e->endpoints[1 - c], "Point order messed up");
                    last_point = e->endpoints[c];

                    // copy the view list backwards.
                    for (v = (Point*)e->view_list.tail; v != NULL; v = (Point*)v->hdr.prev)
                    {
                        p = point_newpv(v);
                        if (face->vol != NULL)
                            expand_bbox(&face->vol->bbox, p);
                        link_tail((Object*)p, list);
                    }
                    if ((e->type & ~EDGE_CONSTRUCTION) == EDGE_BEZIER)
                    {
                        BezierEdge* be = (BezierEdge*)e;

                        be->bezctl[0] = e->endpoints[1];
                        be->bezctl[1] = be->ctrlpoints[1];
                        be->bezctl[2] = be->ctrlpoints[0];
                        be->bezctl[3] = e->endpoints[0];
                    }
                }
                break;

            default:
                ASSERT(FALSE, "Only arc and bezier edges should be in here");
                break;
            }
        }

        // Create internal arc edges:
        // - centre interpolated between boundary arc edge centres
        // - endpoints taken from side edge view list points
        // - plane adjusted so VL points in same direction as the list at elists[0]
        e0 = face->edges[first_arc];
        e1 = face->edges[first_arc + 2];
        ae0 = (ArcEdge*)e0;
        ae1 = (ArcEdge*)e1;
        step = 1.0f / side_nsteps;
        s0 = (Point *)slists[0]->head->next;
        s1 = (Point *)slists[1]->head->next;
        internal.head = NULL;
        internal.tail = NULL;
        for (i = 1, t = step; i < side_nsteps; i++, t += step)
        {
            ArcEdge* ae = (ArcEdge*)edge_new(EDGE_ARC);
            Edge* e = (Edge*)ae;

            // Don't add this edge to the object tree.
            e->hdr.ID = 0;
            objid--;
            ae->centre = point_newv
            (
                t * ae0->centre->x + (1 - t) * ae1->centre->x,
                t * ae0->centre->y + (1 - t) * ae1->centre->y,
                t * ae0->centre->z + (1 - t) * ae1->centre->z
            );
            e->endpoints[0] = point_newpv(s0);
            e->endpoints[1] = point_newpv(s1);
            // make sure it points in the same direction as the first_arc
            ae->clockwise = first_arc_forward ? ae0->clockwise : !ae0->clockwise;
            if (ae->clockwise)
                normal3(e->endpoints[1], ae->centre, e->endpoints[0], &ae->normal);
            else
                normal3(e->endpoints[0], ae->centre, e->endpoints[1], &ae->normal);

            // Make sure it has the same number of steps as the first_arc
            e->stepsize = 0;
            e->nsteps = e0->nsteps;
            e->stepping = TRUE;

            // Save the view list, and the edge itself, for later freeing
            gen_view_list_arc(ae);
            elists[i] = &e->view_list;
            link_tail((Object*)ae, &internal);

            // advance to next pair of VL points in side edges
            s0 = (Point*)s0->hdr.next;
            s1 = (Point*)s1->hdr.next;
        }

        // Link up boundary and internal edges into facets and store these in the face VL
        for (i = 1; i <= side_nsteps; i++)
        {
            for
            (
                v = (Point*)elists[i-1]->head, w = (Point*)elists[i]->head;
                v->hdr.next != NULL;
                v = (Point*)v->hdr.next, w = (Point*)w->hdr.next
            )
            {
                Point* vnext = (Point*)v->hdr.next;
                Point* wnext = (Point*)w->hdr.next;
                Plane norm;

                // A new facet point containing the normal
                normal3(v, w, vnext, &norm);
                p = point_newv(norm.A, norm.B, norm.C);
                p->flags = FLAG_NEW_FACET;
                link_tail((Object*)p, &face->view_list);

                // Four points for the quad
                p = point_newpv(v);
                link_tail((Object*)p, &face->view_list);
                p = point_newpv(vnext);
                link_tail((Object*)p, &face->view_list);
                p = point_newpv(wnext);
                link_tail((Object*)p, &face->view_list);
                p = point_newpv(w);
                link_tail((Object*)p, &face->view_list);
            }
        }

        // Free the internal edges now we no longer need them, and the point lists
        purge_list(&internal);
        free_point_list(elists[0]);
        free_point_list(elists[side_nsteps]);
        free(elists);
        free_point_list(slists[0]);
        free_point_list(slists[1]);
        free(slists);

        break;
#endif // SUPPORT_BARREL_FACES

#ifdef SUPPORT_BEZIER_FACES
    case FACE_BEZIER:
        // For these faces, there are 4 edges only. All of them are beziers. Construct the 4 internal
        // control points and use the resulting 16-point bezier surface to interpolate internal edges
        // as in the barrel case above.






        break;
#endif // SUPPORT_BEZIER_FACES
    }

    // The view list is valid, as is the 2D view list, the face normal, and the face's
    // contribution to the volume bounding bbox.
    face->view_valid = TRUE;
    return;
}

void
update_view_list_2D(Face *face)
{
    int i;
    Point *v;

    // Update the 2D view list as seen from the facing plane closest to the face normal,
    // to facilitate quick point-in-polygon testing.
    if (!IS_FLAT(face))
        return;

    for (i = 0, v = (Point *)face->view_list.head; v != NULL; v = (Point *)v->hdr.next, i++)
    {
        float a = fabsf(face->normal.A);
        float b = fabsf(face->normal.B);
        float c = fabsf(face->normal.C);

        if (c > b && c > a)
        {
            face->view_list2D[i].x = v->x;
            face->view_list2D[i].y = v->y;
        }
        else if (b > a && b > c)
        {
            face->view_list2D[i].x = v->x;
            face->view_list2D[i].y = v->z;
        }
        else
        {
            face->view_list2D[i].x = v->y;
            face->view_list2D[i].y = v->z;
        }

        if (i == face->n_alloc2D - 1)
        {
            face->n_alloc2D *= 2;
            face->view_list2D = realloc(face->view_list2D, face->n_alloc2D * sizeof(Point2D));
        }
    }
    face->view_list2D[i] = face->view_list2D[0];    // copy first point for fast poly testing
    face->n_view2D = i;
}

void
free_view_list_face(Face *face)
{
    free_point_list(&face->view_list);
    face->view_valid = FALSE;
    face->n_view2D = 0;
}

void
free_view_list_edge(Edge *edge)
{
    free_point_list(&edge->view_list);
    edge->view_valid = FALSE;
}

// Generate the view list for an arc edge.
void
gen_view_list_arc(ArcEdge *ae)
{
    Edge *edge = (Edge *)ae;
    Plane n = ae->normal;
    Point *p;
    double rad = length(ae->centre, edge->endpoints[0]);
    double t, theta;
    double step;
    double matrix[16];
    double v[4];
    double res[4];
    int i;

    if (edge->view_valid)
        return;

    free_view_list_edge(edge);

    // transform arc to XY plane, centre at origin, endpoint 0 on x axis
    look_at_centre_d(*ae->centre, *edge->endpoints[0], n, matrix);

    // angle between two vectors c-p0 and c-p1. If the points are the same, we are
    // drawing a full circle. (where "same" means coincident - they are always distinct structures)
    if (near_pt(edge->endpoints[0], edge->endpoints[1], SMALL_COORD))
        theta = ae->clockwise ? -2 * PI : 2 * PI;
    else
        theta = angle3(edge->endpoints[0], ae->centre, edge->endpoints[1], &n);

    // TODO_BARREL Make nsteps the independent variable, and calculate stepsize from that.
    // (for both arcs and beziers)

    // Calculate step for angle. The number of steps may be fixed in advance.
    if (edge->stepping && edge->nsteps > 0)
    {
        step = (fabs(theta) + 0.001) / edge->nsteps;
    }
    else
    {
        step = 2.0 * acos(1.0 - tolerance / rad);
    }
    edge->stepsize = (float)step;
    i = 0;

    if (ae->clockwise)  // Clockwise angles go negative
    {
#ifdef DEBUG_VIEW_LIST_ARC
        Log("Clockwise arc:");
#endif
        if (theta > 0)
            theta -= 2 * PI;

        // draw arc from p1 (on x axis) to p2. 
        for (t = 0, i = 0; t > theta; t -= step, i++)
        {
            v[0] = rad * cos(t);
            v[1] = rad * sin(t);
            v[2] = 0;
            v[3] = 1;
            mat_mult_by_col_d(matrix, v, res);
            p = point_newv((float)res[0], (float)res[1], (float)res[2]);
            link_tail((Object *)p, &edge->view_list);
#ifdef DEBUG_VIEW_LIST_ARC
            {
                char buf[64];
                sprintf_s(buf, 64, "%f %f %f\r\n", res[0], res[1], res[2]);
                Log(buf);
            }
#endif
        }
    }
    else
    {
#ifdef DEBUG_VIEW_LIST_ARC
        Log("Anticlockwise arc:");
#endif
        if (theta < 0)
            theta += 2 * PI;

        for (t = 0, i = 0; t < theta; t += step, i++)
        {
            v[0] = rad * cos(t);
            v[1] = rad * sin(t);
            v[2] = 0;
            v[3] = 1;
            mat_mult_by_col_d(matrix, v, res);
            p = point_newv((float)res[0], (float)res[1], (float)res[2]);
            link_tail((Object *)p, &edge->view_list);
#ifdef DEBUG_VIEW_LIST_ARC
            {
                char buf[64];
                sprintf_s(buf, 64, "%f %f %f\r\n", res[0], res[1], res[2]);
                Log(buf);
            }
#endif
        }
    }

    edge->nsteps = i;

    // Make sure the last point is in the view list
    p = point_newpv(edge->endpoints[1]);
    link_tail((Object *)p, &edge->view_list);

#ifdef CHECK_ZERO_LENGTH
    for (p = (Point*)edge->view_list.head; p->hdr.next != NULL; p = (Point*)p->hdr.next)
    {
        float len = length_squared(p, (Point*)p->hdr.next);
        ASSERT(len > SMALL_COORD, "Possible zero length edge in arc VL");
    }
#endif

    edge->view_valid = TRUE;
}

// Iterative bezier edge drawing.
void
iterate_bez
(
    BezierEdge *be,
    double x1, double y1, double z1,
    double x2, double y2, double z2,
    double x3, double y3, double z3,
    double x4, double y4, double z4
)
{
    Point *p;
    Edge *e = (Edge *)be;
    double t;

    // Number of steps has been given in advance, so work out the stepsize
    e->stepsize = 1.0f / e->nsteps;

    // the first point has already been output, so start at stepsize
    for (t = e->stepsize; t < 1.0001f; t += e->stepsize)
    {
        double mt = 1.0f - t;
        double c0 = mt * mt * mt;
        double c1 = 3 * mt * mt * t;
        double c2 = 3 * mt * t * t;
        double c3 = t * t * t;
        double x = c0 * x1 + c1 * x2 + c2 * x3 + c3 * x4;
        double y = c0 * y1 + c1 * y2 + c2 * y3 + c3 * y4;
        double z = c0 * z1 + c1 * z2 + c2 * z3 + c3 * z4;

        p = point_newv((float)x, (float)y, (float)z);
        link_tail((Object *)p, &e->view_list);
    }
}

// Length squared shortcut
#define LENSQ(x1, y1, z1, x2, y2, z2) ((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + (z2-z1)*(z2-z1))

// Recursive bezier edge drawing.
void
recurse_bez
(
    BezierEdge *be,
    double x1, double y1, double z1,
    double x2, double y2, double z2,
    double x3, double y3, double z3,
    double x4, double y4, double z4
)
{
    Point *p;
    Edge *e = (Edge *)be;

    // Calculate all the mid-points of the line segments
    double x12 = (x1 + x2) / 2;
    double y12 = (y1 + y2) / 2;
    double z12 = (z1 + z2) / 2;

    double x23 = (x2 + x3) / 2;
    double y23 = (y2 + y3) / 2;
    double z23 = (z2 + z3) / 2;

    double x34 = (x3 + x4) / 2;
    double y34 = (y3 + y4) / 2;
    double z34 = (z3 + z4) / 2;

    double x123 = (x12 + x23) / 2;
    double y123 = (y12 + y23) / 2;
    double z123 = (z12 + z23) / 2;

    double x234 = (x23 + x34) / 2;
    double y234 = (y23 + y34) / 2;
    double z234 = (z23 + z34) / 2;

    double x1234 = (x123 + x234) / 2;
    double y1234 = (y123 + y234) / 2;
    double z1234 = (z123 + z234) / 2;

    double x14 = (x1 + x4) / 2;
    double y14 = (y1 + y4) / 2;
    double z14 = (z1 + z4) / 2;

    // Do a length test < the grid unit, and a curve flatness test < the tolerance.
    // Test length squared (to save the sqrts)
    if
        (
        LENSQ(x1, y1, z1, x4, y4, z4) < grid_snap * grid_snap
        ||
        LENSQ(x1234, y1234, z1234, x14, y14, z14) < tolerance * tolerance
        )
    {
        // Add (x4, y4, z4) as a point to the view list
        p = point_newv((float)x4, (float)y4, (float)z4);
        link_tail((Object *)p, &e->view_list);
        e->nsteps++;
    }
    else
    {
        // Continue subdivision
        recurse_bez(be, x1, y1, z1, x12, y12, z12, x123, y123, z123, x1234, y1234, z1234);
        recurse_bez(be, x1234, y1234, z1234, x234, y234, z234, x34, y34, z34, x4, y4, z4);
    }
}

// Generate the view list for a bezier edge.
void
gen_view_list_bez(BezierEdge *be)
{
    Edge *e = (Edge *)be;
    Point *p;

    if (e->view_valid)
        return;

    free_view_list_edge(e);

    // Put the first endpoint on the view list
    p = point_newpv(e->endpoints[0]);
    link_tail((Object *)p, &e->view_list);

    // Perform fixed step division if number of steps given in advance
    if (e->stepping && e->nsteps > 0)
    {
        iterate_bez
            (
            be,
            e->endpoints[0]->x, e->endpoints[0]->y, e->endpoints[0]->z,
            be->ctrlpoints[0]->x, be->ctrlpoints[0]->y, be->ctrlpoints[0]->z,
            be->ctrlpoints[1]->x, be->ctrlpoints[1]->y, be->ctrlpoints[1]->z,
            e->endpoints[1]->x, e->endpoints[1]->y, e->endpoints[1]->z
            );
    }
    else
    {
        // Subdivide the bezier
        e->nsteps = 0;
        recurse_bez
            (
            be,
            e->endpoints[0]->x, e->endpoints[0]->y, e->endpoints[0]->z,
            be->ctrlpoints[0]->x, be->ctrlpoints[0]->y, be->ctrlpoints[0]->z,
            be->ctrlpoints[1]->x, be->ctrlpoints[1]->y, be->ctrlpoints[1]->z,
            e->endpoints[1]->x, e->endpoints[1]->y, e->endpoints[1]->z
            );
        e->stepsize = 1.0f / e->nsteps;
    }

    e->view_valid = TRUE;
}


// callbacks for rendering tessellated stuff to GL
void 
render_beginData(GLenum type, void * polygon_data)
{
    Plane *norm = (Plane *)polygon_data;

    glNormal3f(norm->A, norm->B, norm->C);  // TODO_XFORM - need to transform this too! But it probably doesn't matter.
    glBegin(type);
}

void 
render_vertexData(void * vertex_data, void * polygon_data)
{
    Point *v = (Point *)vertex_data;
    double tx, ty, tz;

    transform_list_xyz(&xform_list, v->x, v->y, v->z, &tx, &ty, &tz);
    glVertex3d(tx, ty, tz);
}

void 
render_endData(void * polygon_data)
{
    glEnd();
}

void 
render_combineData(GLdouble coords[3], void *vertex_data[4], GLfloat weight[4], void **outData, void * polygon_data)
{
    // Allocate a new Point for the new vertex, and (TODO:) hang it off the face's spare vertices list.
    // It will be freed when the view list is regenerated.
    *outData = point_newv((float)coords[0], (float)coords[1], (float)coords[2]);;
}

void render_errorData(GLenum errno, void * polygon_data)
{
    ASSERT(FALSE, "tesselator error");
}

// Shortcut to pass a Point to gluTessVertex, both as coords and the vertex data pointer.
void
tess_vertex(GLUtesselator *tess, Point *p)
{
    double coords[3];

    coords[0] = p->x;
    coords[1] = p->y;
    coords[2] = p->z;
    gluTessVertex(tess, coords, p);
}

// initialise tessellators for rendering
void
init_triangulator(void)
{
    rtess = gluNewTess();
    gluTessCallback(rtess, GLU_TESS_BEGIN_DATA, (void(__stdcall *)(void))render_beginData);
    gluTessCallback(rtess, GLU_TESS_VERTEX_DATA, (void(__stdcall *)(void))render_vertexData);
    gluTessCallback(rtess, GLU_TESS_END_DATA, (void(__stdcall *)(void))render_endData);
    gluTessCallback(rtess, GLU_TESS_COMBINE_DATA, (void(__stdcall *)(void))render_combineData);
    gluTessCallback(rtess, GLU_TESS_ERROR_DATA, (void(__stdcall *)(void))render_errorData);

    init_clip_tess();
}

// Shade in a face by triangulating its view list. The view list is assumed up to date.
void
face_shade(GLUtesselator *tess, Face *face, PRESENTATION pres, BOOL locked)
{
    Point   *v, *vfirst;
    Plane norm;

#ifdef DEBUG_FACE_SHADE
    Log("Face view list:\r\n");
    for (v = face->view_list; v->hdr.next != NULL; v = (Point *)v->hdr.next)
    {
        char buf[64];
        sprintf_s(buf, 64, "%d %f %f %f\r\n", v->flags, v->x, v->y, v->z);
        Log(buf);
    }
#endif       
    color((Object *)face, face->type & FACE_CONSTRUCTION, pres, locked);

    // If there are no facets, just use the face normal
    norm = face->normal;
    v = (Point *)face->view_list.head;
    while (v != NULL)
    {
        if (v->flags == FLAG_NEW_FACET)
        {
            norm.A = v->x;
            norm.B = v->y;
            norm.C = v->z;
            v = (Point *)v->hdr.next;
        }
        vfirst = v;
        gluTessBeginPolygon(tess, &norm);
        gluTessBeginContour(tess);
        while (VALID_VP(v))
        {
            if (v->flags == FLAG_NEW_CONTOUR)
            {
                gluTessEndContour(tess);
                gluTessBeginContour(tess);
            }

            tess_vertex(tess, v);

            // Skip coincident points for robustness (don't create zero-area triangles)
            while (v->hdr.next != NULL && near_pt(v, (Point *)v->hdr.next, SMALL_COORD))
                v = (Point *)v->hdr.next;

            v = (Point *)v->hdr.next;
                
            // If face(t) is closed, skip the closing point. Watch for dups at the end.
            while (v != NULL && near_pt(v, vfirst, SMALL_COORD))
                v = (Point *)v->hdr.next;
        }
        gluTessEndContour(tess);
        gluTessEndPolygon(tess);
    }
}

