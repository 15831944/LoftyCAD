#include "stdafx.h"
#include "LoftyCAD.h"
#include <CommCtrl.h>
#include <CommDlg.h>
#include <stdio.h>


// Search for a closed chain of connected edges (having coincident endpoints within tol)
// and if found, make a group out of them.
Group *
group_connected_edges(Edge *edge)
{
    Object *obj, *nextobj;
    Edge *e;
    Group *group;
    int n_edges = 0;
    int pass;
    typedef struct End          // an end of the growing chain
    {
        int which_end;          // endpoint 0 or 1
        Edge *edge;             // which edge it is on
    } End;
    End end0, end1;
    BOOL legit = FALSE;
    int max_passes = 0;

    if (((Object *)edge)->type != OBJ_EDGE)
        return NULL;

    // Put the first edge in the list, removing it from the object tree.
    // It had better be in the object tree to start with! Check to be sure,
    // and while here, find out how many top-level edges there are as an
    // upper bound on passes later on.
    for (obj = object_tree.obj_list.head; obj != NULL; obj = obj->next)
    {
        if (obj->type == OBJ_EDGE)
        {
            max_passes++;
            if (obj == (Object *)edge)
                legit = TRUE;
        }
    }
    if (!legit)
        return NULL;

    // make a group for the edges
    group = group_new();
    delink_group((Object *)edge, &object_tree);
    link_group((Object *)edge, group);
    end0.edge = edge;
    end0.which_end = 0;
    end1.edge = edge;
    end1.which_end = 1;

    // Make passes over the tree, greedy-grouping edges as they are found to connect.
    // If the chain is closed, we should add at least 2 edges per pass, and could add
    // a lot more than that with favourable ordering.
    //
    // While here, link the points themselves into another list, 
    // so a normal can be found early (we may need to reverse
    // the order if the edges were drawn the wrong way round).
    // We can do this with edge endpoints because their next/prev
    // pointers are not used for anything else.
    for (pass = 0; pass < max_passes; pass++)
    {
        BOOL advanced = FALSE;

        for (obj = object_tree.obj_list.head; obj != NULL; obj = nextobj)
        {
            nextobj = obj->next;

            if (obj->type != OBJ_EDGE)
                continue;

            e = (Edge *)obj;
            if (near_pt(e->endpoints[0], end0.edge->endpoints[end0.which_end], snap_tol))
            {
                // endpoint 0 of obj connects to end0. Put obj in the list.
                delink_group(obj, &object_tree);
                link_group(obj, group);

                // Update end0 to point to the other end of the new edge we just added
                end0.edge = e;
                end0.which_end = 1;
                n_edges++;
                advanced = TRUE;
            }

            // Check for endpoint 1 connecting to end0 similarly
            else if (near_pt(e->endpoints[1], end0.edge->endpoints[end0.which_end], snap_tol))
            {
                delink_group(obj, &object_tree);
                link_group(obj, group);
                end0.edge = e;
                end0.which_end = 0;
                n_edges++;
                advanced = TRUE;
            }

            // And the same for end1. New edges are linked at the tail. 
            else if (near_pt(e->endpoints[0], end1.edge->endpoints[end1.which_end], snap_tol))
            {
                delink_group(obj, &object_tree);
                link_tail_group(obj, group);
                end1.edge = e;
                end1.which_end = 1;
                n_edges++;
                advanced = TRUE;
            }

            else if (near_pt(e->endpoints[1], end1.edge->endpoints[end1.which_end], snap_tol))
            {
                delink_group(obj, &object_tree);
                link_tail_group(obj, group);
                end1.edge = e;
                end1.which_end = 0;
                n_edges++;
                advanced = TRUE;
            }

            if (near_pt(end0.edge->endpoints[end0.which_end], end1.edge->endpoints[end1.which_end], snap_tol))
            {
                // We have closed the chain. Mark the group as a closed edge group by
                // setting its lock to Edges (it's hacky, but the lock is written to the
                // file, and it's not used for anything else)
                group->hdr.lock = LOCK_EDGES;
                return group;
            }
        }

        // Every pass should advance at least one of the ends. If we can't close,
        // return the edges unchanged to the object tree and return NULL.
        if (!advanced)
            break;
    }

    // We have not been able to close the chain. Ungroup everything we've grouped
    // so far and return.
    for (obj = group->obj_list.head; obj != NULL; obj = nextobj)
    {
        nextobj = obj->next;
        delink_group(obj, group);
        link_tail_group(obj, &object_tree);
    }
    purge_obj((Object *)group);

    return NULL;
}

// Make a face object out of a group of connected edges, sharing points as we go.
// The original group is deleted and the face is put into the object tree.
Face *
make_face(Group *group)
{
    Face *face = NULL;
    Edge *e, *next_edge, *prev_edge;
    Plane norm;
    BOOL reverse = FALSE;
    int initial, final, n_edges;
    ListHead plist = { NULL, NULL };
    Point *pt;
    int i;

    // Check that the group is locked at Edges (made by group-connected_edges)
    if (group->hdr.lock != LOCK_EDGES)
        return NULL;

    // Determine normal of points gathered up so far. From this we decide
    // which order to build the final edge array on the face. Join the
    // endpoints up into a list (the next/prev pointers aren't used for
    // anything else)
    e = (Edge *)group->obj_list.head;
    next_edge = (Edge *)group->obj_list.head->next;
    if (near_pt(e->endpoints[0], next_edge->endpoints[0], snap_tol))
    {
        initial = 1;
        pt = e->endpoints[0];
    }
    else if (near_pt(e->endpoints[0], next_edge->endpoints[1], snap_tol))
    {
        initial = 1;
        pt = e->endpoints[0];
    }
    else if (near_pt(e->endpoints[1], next_edge->endpoints[0], snap_tol))
    {
        initial = 0;
        pt = e->endpoints[1];
    }
    else if (near_pt(e->endpoints[1], next_edge->endpoints[1], snap_tol))
    {
        initial = 0;
        pt = e->endpoints[1];
    }
    else
    {
        ASSERT(FALSE, "The edges aren't connected");
        return NULL;
    }

    link_tail((Object *)e->endpoints[initial], &plist);
    n_edges = 1;

    for (; e->hdr.next != NULL; e = next_edge)
    {
        next_edge = (Edge *)e->hdr.next;

        if (e->hdr.next->type != OBJ_EDGE)
            return NULL;

        if (near_pt(next_edge->endpoints[0], pt, snap_tol))
        {
            next_edge->endpoints[0] = pt;       // share the point
            link_tail((Object *)next_edge->endpoints[0], &plist);
            final = 1;
        }
        else if (near_pt(next_edge->endpoints[1], pt, snap_tol))
        {
            next_edge->endpoints[1] = pt;
            link_tail((Object *)next_edge->endpoints[1], &plist);
            final = 0;
        }
        else
        {
            return FALSE;
        }
        pt = next_edge->endpoints[final];
        n_edges++;
    }

    // Share the last point back to the beginning
    ASSERT(near_pt(((Edge *)group->obj_list.head)->endpoints[initial], pt, snap_tol), "The edges don't close at the starting point");
    next_edge->endpoints[final] = ((Edge *)group->obj_list.head)->endpoints[initial];

    // Get the normal and see if we need to reverse
    polygon_normal((Point *)plist.head, &norm);
    if (dot(norm.A, norm.B, norm.C, facing_plane->A, facing_plane->B, facing_plane->C) < 0)
    {
        reverse = TRUE;
        norm.A = -norm.A;
        norm.B = -norm.B;
        norm.C = -norm.C;
    }

    // make the face
    face = face_new(FACE_FLAT, norm);
    face->n_edges = n_edges;
    face->max_edges = 1;
    while (face->max_edges <= n_edges)
        face->max_edges <<= 1;          // round up to a power of 2
    if (face->max_edges > 16)           // does it need any more than the default 16?
        face->edges = realloc(face->edges, face->max_edges * sizeof(Edge *));

    // Populate the edge list, sharing points along the way. 
    if (reverse)
    {
        face->initial_point = next_edge->endpoints[final];
        for (i = 0, e = next_edge; e != NULL; e = prev_edge, i++)
        {
            prev_edge = (Edge *)e->hdr.prev;
            face->edges[i] = e;
            delink_group((Object *)e, group);
        }
    }
    else
    {
        face->initial_point = ((Edge *)group->obj_list.head)->endpoints[initial];
        for (i = 0, e = (Edge *)group->obj_list.head; e != NULL; e = next_edge, i++)
        {
            next_edge = (Edge *)e->hdr.next;
            face->edges[i] = e;
            delink_group((Object *)e, group);
        }
    }

    // Delete the group
    ASSERT(group->obj_list.head == NULL, "Edge group is not empty");
    delink_group((Object *)group, &object_tree);
    purge_obj((Object *)group);

    // Finally, update the view list for the face
    gen_view_list_face(face);

    return face;
}

// Insert an edge at a corner point; either a single straight (chamfer) or an arc (round).
// Optionally, restrict the radius or chamfer to prevent rounds colliding at short edges.
void
insert_chamfer_round(Point *pt, Face *parent, float size, EDGE edge_type, BOOL restricted)
{
    Edge *e[2] = { NULL, NULL };
    int i, k, end[2], eindex[2];
    Point orig_pt = *pt;
    Edge *ne;
    float len0, len1, backoff;

    if (parent->hdr.type != OBJ_FACE)
        return;     // We can't do this

    // Find the two edges that share this point
    k = 0;
    for (i = 0; i < parent->n_edges; i++)
    {
        if (parent->edges[i]->endpoints[0] == pt)
        {
            e[k] = parent->edges[i];
            eindex[k] = i;
            end[k] = 0;
            k++;
        }
        else if (parent->edges[i]->endpoints[1] == pt)
        {
            e[k] = parent->edges[i];
            eindex[k] = i;
            end[k] = 1;
            k++;
        }
    }

    if (k != 2)
    {
        ASSERT(FALSE, "Point should belong to 2 edges");
        return;   // something is wrong; the point does not belong to two edges
    }

    ASSERT(pt == e[0]->endpoints[end[0]], "Wtf?");
    ASSERT(pt == e[1]->endpoints[end[1]], "Wtf?");

    // Back the original point up by "size" along its edge. Watch short edges.
    len0 = length(e[0]->endpoints[0], e[0]->endpoints[1]);
    len1 = length(e[1]->endpoints[0], e[1]->endpoints[1]);
    backoff = size;
    if (restricted)
    {
        if (backoff > len0 / 3)
            backoff = len0 / 3;
        if (backoff > len1 / 3)
            backoff = len1 / 3;
    }
    new_length(e[0]->endpoints[1 - end[0]], e[0]->endpoints[end[0]], len0 - backoff);

    // Add a new point for the other edge
    e[1]->endpoints[end[1]] = point_newp(&orig_pt);

    // Back this one up too
    new_length(e[1]->endpoints[1 - end[1]], e[1]->endpoints[end[1]], len1 - backoff);

    // Add a new edge
    ne = edge_new(edge_type);
    ne->endpoints[0] = e[0]->endpoints[end[0]];
    ne->endpoints[1] = e[1]->endpoints[end[1]];
    if (edge_type == EDGE_ARC)
    {
        ArcEdge *ae = (ArcEdge *)ne;

        ae->centre = point_new(0, 0, 0);
        ae->normal = parent->normal;
        if (!centre_2pt_tangent_circle(ne->endpoints[0], ne->endpoints[1], &orig_pt, &parent->normal, ae->centre, &ae->clockwise))
            ne->type = EDGE_STRAIGHT;
    }

    // Grow the edges array if it won't take the new edge
    if (parent->n_edges >= parent->max_edges)
    {
        while (parent->max_edges <= parent->n_edges)
            parent->max_edges <<= 1;          // round up to a power of 2
        parent->edges = realloc(parent->edges, parent->max_edges * sizeof(Edge *));
    }

    // Shuffle the edges array down and insert it
    // Special case if e[] = 0 and n_edges-1, insert at the end
    if (eindex[0] == 0 && eindex[1] == parent->n_edges - 1)
    {
        parent->edges[parent->n_edges] = ne;
    }
    else
    {
        for (k = parent->n_edges - 1; k >= eindex[1]; --k)
            parent->edges[k + 1] = parent->edges[k];
        parent->edges[eindex[1]] = ne;
    }
    parent->n_edges++;
}

// handle context menu when right-clicking on a highlightable object.
void CALLBACK
right_click(AUX_EVENTREC *event)
{
    POINT pt;

    if (view_rendered)
        return;

    picked_obj = Pick(event->data[0], event->data[1], FALSE);

    // If we don't pick anything, attempt a forced pick (we might have clicked a locked volume)
    if (picked_obj == NULL)
        picked_obj = Pick(event->data[0], event->data[1], TRUE);
    if (picked_obj == NULL)
        return;

    pt.x = event->data[AUX_MOUSEX];
    pt.y = event->data[AUX_MOUSEY];
    ClientToScreen(auxGetHWND(), &pt);

    contextmenu(picked_obj, pt);
}

// Display a context menu for an object at a screen location.
void
contextmenu(Object *picked_obj, POINT pt)
{
    HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_CONTEXT));
    int rc;
    Object *parent, *sel_obj, *o, *o_next;
    char buf[128];
    LOCK old_parent_lock;
    BOOL group_changed = FALSE;
    BOOL dims_changed = FALSE;
    BOOL sel_changed = FALSE;
    BOOL xform_changed = FALSE;
    BOOL inserted = FALSE;
    Group *group;
    Face *face;
    Point *p, *nextp;
    int i;
    OPENFILENAME ofn;
    char group_filename[256];

    // Display the object ID at the top of the menu
    hMenu = GetSubMenu(hMenu, 0);
    switch (picked_obj->type)
    {
    case OBJ_POINT:
        sprintf_s(buf, 128, "Point %d", picked_obj->ID);
        break;
    case OBJ_EDGE:
        sprintf_s(buf, 128, "Edge %d", picked_obj->ID);
        break;
    case OBJ_FACE:
        sprintf_s(buf, 128, "Face %d", picked_obj->ID);
        break;
    case OBJ_VOLUME:
        sprintf_s(buf, 128, "Volume %d", picked_obj->ID);
        break;
    case OBJ_GROUP:
        sprintf_s(buf, 128, "Group %d: %s", picked_obj->ID, ((Group *)picked_obj)->title);
        break;
    }
    ModifyMenu(hMenu, 0, MF_BYPOSITION | MF_GRAYED | MF_STRING, 0, buf);

    // Find the top-level parent. Disable irrelevant menu items
    parent = find_parent_object(&object_tree, picked_obj, FALSE);
    switch (parent->type)
    {
    case OBJ_EDGE:
        EnableMenuItem(hMenu, ID_LOCKING_FACES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_LOCKING_VOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SELECTPARENTVOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_UNGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SAVEGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_MAKEFACE, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_CHAMFERCORNER, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_ROUNDCORNER, MF_GRAYED);
        break;

    case OBJ_FACE:
        EnableMenuItem(hMenu, ID_LOCKING_VOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SELECTPARENTVOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_GROUPEDGES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_UNGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SAVEGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_MAKEFACE, MF_GRAYED);
        break;

    case OBJ_VOLUME:
        EnableMenuItem(hMenu, ID_OBJ_GROUPEDGES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_UNGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SAVEGROUP, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_MAKEFACE, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_CHAMFERCORNER, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_ROUNDCORNER, MF_GRAYED);
        break;

    case OBJ_GROUP:
        EnableMenuItem(hMenu, ID_LOCKING_VOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_LOCKING_FACES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_LOCKING_EDGES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_LOCKING_POINTS, MF_GRAYED);
        EnableMenuItem(hMenu, ID_LOCKING_UNLOCKED, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_SELECTPARENTVOLUME, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_GROUPEDGES, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_CHAMFERCORNER, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_ROUNDCORNER, MF_GRAYED);
        break;
    }

    EnableMenuItem(hMenu, ID_OBJ_GROUPSELECTED, is_selected_direct(picked_obj, &o) ? MF_ENABLED : MF_GRAYED);

    // Disable "enter dimensions" for objects that have no dimensions that can be easily changed
    if (!has_dims(picked_obj))
    {
        EnableMenuItem(hMenu, ID_OBJ_ALWAYSSHOWDIMS, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_ENTERDIMENSIONS, MF_GRAYED);
    }
    else
    {
        CheckMenuItem(hMenu, ID_OBJ_ALWAYSSHOWDIMS, picked_obj->show_dims ? MF_CHECKED : MF_UNCHECKED);
    }

    // Rounding and chamfering depend on whether the object picked is a point or a face.
    // The parent has to be at least a face (checked above)
    switch (picked_obj->type)
    {
    case OBJ_EDGE:
    case OBJ_VOLUME:
    case OBJ_GROUP:
        EnableMenuItem(hMenu, ID_OBJ_CHAMFERCORNER, MF_GRAYED);
        EnableMenuItem(hMenu, ID_OBJ_ROUNDCORNER, MF_GRAYED);
        break;
    }

    // We can only do transforms on a volume or a group.
    switch (picked_obj->type)
    {
    case OBJ_POINT:
    case OBJ_EDGE:
    case OBJ_FACE:
        EnableMenuItem(hMenu, ID_OBJ_TRANSFORM, MF_GRAYED);
        break;
    }

    // Check the right lock state for the parent
    old_parent_lock = parent->lock;
    switch (parent->lock)
    {
    case LOCK_VOLUME:
        CheckMenuItem(hMenu, ID_LOCKING_VOLUME, MF_CHECKED);
        break;
    case LOCK_FACES:
        CheckMenuItem(hMenu, ID_LOCKING_FACES, MF_CHECKED);
        break;
    case LOCK_EDGES:
        CheckMenuItem(hMenu, ID_LOCKING_EDGES, MF_CHECKED);
        break;
    case LOCK_POINTS:
        CheckMenuItem(hMenu, ID_LOCKING_POINTS, MF_CHECKED);
        break;
    case LOCK_NONE:
        CheckMenuItem(hMenu, ID_LOCKING_UNLOCKED, MF_CHECKED);
        break;
    }

    display_help("Context menu");

    // Display and track the menu
    rc = TrackPopupMenu
        (
        hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x,
        pt.y,
        0,
        auxGetHWND(),
        NULL
        );

    change_state(app_state);  // back to displaying usual state text
    switch (rc)
    {
    case 0:             // no item chosen
        break;

        // Locking options. If an object is selected, and we have locked it at
        // its own level, we must remove it from selection (it cannot be selected)
    case ID_LOCKING_UNLOCKED:
        parent->lock = LOCK_NONE;
        if (parent->lock == picked_obj->type)
            remove_from_selection(picked_obj);
        break;
    case ID_LOCKING_POINTS:
        parent->lock = LOCK_POINTS;
        if (parent->lock == picked_obj->type)
            remove_from_selection(picked_obj);
        break;
    case ID_LOCKING_EDGES:
        parent->lock = LOCK_EDGES;
        if (parent->lock == picked_obj->type)
            remove_from_selection(picked_obj);
        break;
    case ID_LOCKING_FACES:
        parent->lock = LOCK_FACES;
        if (parent->lock == picked_obj->type)
            remove_from_selection(picked_obj);
        break;
    case ID_LOCKING_VOLUME:
        parent->lock = LOCK_VOLUME;
        if (parent->lock == picked_obj->type)
            remove_from_selection(picked_obj);
        break;

    case ID_OBJ_SELECTPARENTVOLUME:
        sel_changed = TRUE;
        clear_selection(&selection);
        link_single(parent, &selection);
        break;

    case ID_OBJ_ENTERDIMENSIONS:
        show_dims_at(pt, picked_obj, TRUE);
        break;

    case ID_OBJ_ALWAYSSHOWDIMS:
        dims_changed = TRUE;
        if (picked_obj->show_dims)
        {
            picked_obj->show_dims = FALSE;
            CheckMenuItem(hMenu, ID_OBJ_ALWAYSSHOWDIMS, MF_UNCHECKED);
        }
        else
        {
            picked_obj->show_dims = TRUE;
            CheckMenuItem(hMenu, ID_OBJ_ALWAYSSHOWDIMS, MF_CHECKED);
        }
        break;

    case ID_OBJ_GROUPEDGES:
        group = group_connected_edges((Edge *)picked_obj);
        if (group != NULL)
        {
            link_group((Object *)group, &object_tree);
            clear_selection(&selection);
            group_changed = TRUE;
        }
        break;

    case ID_OBJ_MAKEFACE:
        face = make_face((Group *)picked_obj);
        if (face != NULL)
        {
            link_group((Object *)face, &object_tree);
            clear_selection(&selection);
            group_changed = TRUE;
        }
        break;

    case ID_OBJ_GROUPSELECTED:
        group = group_new();
        link_group((Object *)group, &object_tree);
        for (sel_obj = selection.head; sel_obj != NULL; sel_obj = sel_obj->next)
        {
            // TODO - bug here if a component is selected. You should onnly ever be able to 
            // put the parent in the group.
            delink_group(sel_obj->prev, &object_tree);
            link_tail_group(sel_obj->prev, group);
        }
        clear_selection(&selection);
        group_changed = TRUE;
        break;

    case ID_OBJ_UNGROUP:
        group = (Group *)picked_obj;
        delink_group(picked_obj, &object_tree);
        for (o = group->obj_list.head; o != NULL; o = o_next)
        {
            o_next = o->next;
            delink_group(o, group);
            link_tail_group(o, &object_tree);
        }
        purge_obj(picked_obj);
        clear_selection(&selection);
        group_changed = TRUE;
        break;

    case ID_OBJ_SAVEGROUP:
        memset(&ofn, 0, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = auxGetHWND();
        ofn.lpstrFilter = "LoftyCAD Files (*.LCD)\0*.LCD\0All Files\0*.*\0\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = "lcd";
        strcpy_s(group_filename, 256, "GROUP");
        ofn.lpstrFile = group_filename;
        ofn.nMaxFile = 256;
        ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
        if (GetSaveFileName(&ofn))
        {
            group = (Group *)picked_obj;
            serialise_tree(group, group_filename);
        }

        break;

        // The following operations work on a point, or on all points in a face.
    // The parent must be a face.
    case ID_OBJ_CHAMFERCORNER:
        switch (picked_obj->type)
        {
        case OBJ_POINT:
            face = (Face *)parent;
            insert_chamfer_round((Point *)picked_obj, face, chamfer_rad, EDGE_STRAIGHT, FALSE);
            face->type = FACE_FLAT;
            face->view_valid = FALSE;
            break;
        case OBJ_FACE:
            face = (Face *)picked_obj;
            p = face->initial_point;
            for (i = 0; i < face->n_edges - 1; i++)
            {
                // find the next point before the edge is modified
                if (p == face->edges[i]->endpoints[0])
                    nextp = face->edges[i]->endpoints[1];
                else
                {
                    ASSERT(p == face->edges[i]->endpoints[1], "Points not connected properly");
                    nextp = face->edges[i]->endpoints[0];
                }
                // insert the extra edge
                insert_chamfer_round(p, face, chamfer_rad, EDGE_STRAIGHT, TRUE);
                p = nextp;
                if (i > 0)
                    i++;
            }
            face->type = FACE_FLAT;
            face->view_valid = FALSE;
        }
        inserted = TRUE;
        break;

    case ID_OBJ_ROUNDCORNER:
        switch (picked_obj->type)
        {
        case OBJ_POINT:
            face = (Face *)parent;
            insert_chamfer_round((Point *)picked_obj, face, round_rad, EDGE_ARC, FALSE);
            face->type = FACE_FLAT;
            face->view_valid = FALSE;
            break;
        case OBJ_FACE:
            face = (Face *)picked_obj;

            // TODO - find shortest edge and ensure round radius <= (shortest_len / 2 + tol)
            // to avoid distorting the face



            p = face->initial_point;
            for (i = 0; i < face->n_edges - 1; i++)
            {
                if (p == face->edges[i]->endpoints[0])
                    nextp = face->edges[i]->endpoints[1];
                else
                {
                    ASSERT(p == face->edges[i]->endpoints[1], "Points not connected properly");
                    nextp = face->edges[i]->endpoints[0];
                }
                insert_chamfer_round(p, face, round_rad, EDGE_ARC, TRUE);
                p = nextp;
                if (i > 0)
                    i++;
            }
            face->type = FACE_FLAT;
            face->view_valid = FALSE;
        }
        inserted = TRUE;
        break;

    case ID_OBJ_TRANSFORM:
        xform_changed = 
            DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_TRANSFORM), auxGetHWND(), transform_dialog, (LPARAM)picked_obj);
        if (xform_changed)
            invalidate_all_view_lists(parent, picked_obj, 0, 0, 0);
        break;
    }

    if (parent->lock != old_parent_lock || group_changed || dims_changed || sel_changed || xform_changed || inserted)
    {
        // we have changed the drawing - write an undo checkpoint
        update_drawing();
    }
}

// Transform dialog procedure.
int WINAPI
transform_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char buf[16];
    char title[128];
    static Object *obj;
    Bbox *box = NULL;
    static Transform *xform = NULL;

    switch (msg)
    {
    case WM_INITDIALOG:
        obj = (Object *)lParam;
        switch (obj->type)
        {
        case OBJ_VOLUME:
            sprintf_s(title, 128, "Volume %d", obj->ID);
            xform = ((Volume *)obj)->xform;
            box = &((Volume *)obj)->bbox;
            break;
        case OBJ_GROUP:
            sprintf_s(title, 128, "Group %d: %s", obj->ID, ((Group *)picked_obj)->title);
            xform = ((Group *)obj)->xform;
            box = &((Group *)obj)->bbox;
            break;
        default:
            ASSERT(FALSE, "We must have a volume or a group");
            break;
        }

        SendDlgItemMessage(hWnd, IDC_TITLE, WM_SETTEXT, 0, (LPARAM)title);

        if (xform == NULL)
        {
            sprintf_s(buf, 16, "%.2f", box->xc);
            SendDlgItemMessage(hWnd, IDC_CENTRE_X, WM_SETTEXT, 0, (LPARAM)buf);
            sprintf_s(buf, 16, "%.2f", box->yc);
            SendDlgItemMessage(hWnd, IDC_CENTRE_Y, WM_SETTEXT, 0, (LPARAM)buf);
            sprintf_s(buf, 16, "%.2f", box->zc);
            SendDlgItemMessage(hWnd, IDC_CENTRE_Z, WM_SETTEXT, 0, (LPARAM)buf);
            break;
        }

        CheckDlgButton(hWnd, IDC_CHECK_SCALE, xform->enable_scale ? MF_CHECKED : MF_UNCHECKED);
        sprintf_s(buf, 16, "%.2f", xform->sx);
        SendDlgItemMessage(hWnd, IDC_SCALE_X, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->sy);
        SendDlgItemMessage(hWnd, IDC_SCALE_Y, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->sz);
        SendDlgItemMessage(hWnd, IDC_SCALE_Z, WM_SETTEXT, 0, (LPARAM)buf);

        CheckDlgButton(hWnd, IDC_CHECK_ROTATE, xform->enable_rotation ? MF_CHECKED : MF_UNCHECKED);
        sprintf_s(buf, 16, "%.2f", xform->rx);
        SendDlgItemMessage(hWnd, IDC_ROTATE_X, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->ry);
        SendDlgItemMessage(hWnd, IDC_ROTATE_Y, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->rz);
        SendDlgItemMessage(hWnd, IDC_ROTATE_Z, WM_SETTEXT, 0, (LPARAM)buf);

        sprintf_s(buf, 16, "%.2f", xform->xc);
        SendDlgItemMessage(hWnd, IDC_CENTRE_X, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->yc);
        SendDlgItemMessage(hWnd, IDC_CENTRE_Y, WM_SETTEXT, 0, (LPARAM)buf);
        sprintf_s(buf, 16, "%.2f", xform->zc);
        SendDlgItemMessage(hWnd, IDC_CENTRE_Z, WM_SETTEXT, 0, (LPARAM)buf);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDC_APPLY:
            if (xform == NULL)
            {
                xform = xform_new();
                ((Volume *)obj)->xform = xform;     // Groups have same structure so safe to do this
                box = &((Volume *)obj)->bbox;
                xform->xc = box->xc;
                xform->yc = box->yc;
                xform->zc = box->zc;
            }

            xform->enable_scale = IsDlgButtonChecked(hWnd, IDC_CHECK_SCALE);
            SendDlgItemMessage(hWnd, IDC_SCALE_X, WM_GETTEXT, 16, (LPARAM)buf);
            xform->sx = (float)atof(buf);
            if (xform->sx == 0)
                xform->sx = 1;
            SendDlgItemMessage(hWnd, IDC_SCALE_Y, WM_GETTEXT, 16, (LPARAM)buf);
            xform->sy = (float)atof(buf);
            if (xform->sy == 0)
                xform->sy = 1;
            SendDlgItemMessage(hWnd, IDC_SCALE_Z, WM_GETTEXT, 16, (LPARAM)buf);
            xform->sz = (float)atof(buf);
            if (xform->sz == 0)
                xform->sz = 1;

            xform->enable_rotation = IsDlgButtonChecked(hWnd, IDC_CHECK_ROTATE);
            SendDlgItemMessage(hWnd, IDC_ROTATE_X, WM_GETTEXT, 16, (LPARAM)buf);
            xform->rx = (float)atof(buf);
            SendDlgItemMessage(hWnd, IDC_ROTATE_Y, WM_GETTEXT, 16, (LPARAM)buf);
            xform->ry = (float)atof(buf);
            SendDlgItemMessage(hWnd, IDC_ROTATE_Z, WM_GETTEXT, 16, (LPARAM)buf);
            xform->rz = (float)atof(buf);

            evaluate_transform(xform);
            if (LOWORD(wParam) == IDOK)    // TODO XFORM - do inval and update here, if too hard get rid of Apply button
                EndDialog(hWnd, 1);
            break;

        case IDCANCEL:
            EndDialog(hWnd, 0);
            break;

        case IDC_REMOVE:
            free(xform);
            switch (obj->type)
            {
            case OBJ_VOLUME:
                ((Volume *)obj)->xform = NULL;
                break;
            case OBJ_GROUP:
                ((Group *)obj)->xform = NULL;
                break;
            }
            EndDialog(hWnd, 1);     // TODO XFORM - behave as Apply here and stay in dialog
            break;
        }
    }

    return 0;
}