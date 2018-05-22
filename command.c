#include "stdafx.h"
#include "LoftyCAD.h"
#include <CommCtrl.h>
#include <CommDlg.h>

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void CALLBACK
check_file_changed(HWND hWnd)
{
    if (drawing_changed)
    {
        int rc = MessageBox(hWnd, "File modified. Save it?", curr_filename, MB_YESNOCANCEL | MB_ICONWARNING);

        if (rc == IDCANCEL)
        {
            return;
        }
        else if (rc == IDYES)
        {
            if (curr_filename[0] == '\0')
                SendMessage(hWnd, WM_COMMAND, ID_FILE_SAVEAS, 0);
            else
                serialise_tree(&object_tree, curr_filename);
        }
    }

    clean_checkpoints(curr_filename);
    clear_selection(&selection);
    clear_selection(&clipboard);
    purge_tree(object_tree.obj_list);
    DestroyWindow(hWnd);
}

// Process WM_COMMAND, INITMENUPOPUP and the like, from TK window proc
int CALLBACK
Command(int message, int wParam, int lParam)
{
    HMENU hMenu;
    OPENFILENAME ofn;
    char window_title[256];
    char new_filename[256];
    Object *obj, *sel_obj;

    switch (message)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_EXIT:
            check_file_changed(auxGetHWND());
            break;

        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), auxGetHWND(), About);
            break;

        case ID_PREFERENCES_SNAPTOGRID:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 0);
            if (snapping_to_grid)
            {
                snapping_to_grid = FALSE;
                CheckMenuItem(hMenu, ID_PREFERENCES_SNAPTOGRID, MF_UNCHECKED);
            }
            else
            {
                snapping_to_grid = TRUE;
                CheckMenuItem(hMenu, ID_PREFERENCES_SNAPTOGRID, MF_CHECKED);
            }
            break;

        case ID_PREFERENCES_SNAPTOANGLE:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 0);
            if (snapping_to_angle)
            {
                snapping_to_angle = FALSE;
                CheckMenuItem(hMenu, ID_PREFERENCES_SNAPTOANGLE, MF_UNCHECKED);
            }
            else
            {
                snapping_to_angle = TRUE;
                CheckMenuItem(hMenu, ID_PREFERENCES_SNAPTOANGLE, MF_CHECKED);
            }
            break;

        case ID_VIEW_TOOLS:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_tools)
            {
                ShowWindow(hWndToolbar, SW_HIDE);
                view_tools = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_TOOLS, MF_UNCHECKED);
            }
            else
            {
                ShowWindow(hWndToolbar, SW_SHOW);
                view_tools = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_TOOLS, MF_CHECKED);
            }
            break;

        case ID_VIEW_DEBUGLOG:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_debug)
            {
                ShowWindow(hWndDebug, SW_HIDE);
                view_debug = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_DEBUGLOG, MF_UNCHECKED);
            }
            else
            {
                ShowWindow(hWndDebug, SW_SHOW);
                view_debug = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_DEBUGLOG, MF_CHECKED);
            }
            break;

        case ID_VIEW_HELP:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_help)
            {
                ShowWindow(hWndHelp, SW_HIDE);
                view_help = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_HELP, MF_UNCHECKED);
            }
            else
            {
                ShowWindow(hWndHelp, SW_SHOW);
                view_help = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_HELP, MF_CHECKED);
            }
            break;

        case ID_VIEW_TREE:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_tree)
            {
                ShowWindow(hWndTree, SW_HIDE);
                view_tree = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_TREE, MF_UNCHECKED);
            }
            else
            {
                ShowWindow(hWndTree, SW_SHOW);
                view_tree = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_TREE, MF_CHECKED);
            }
            break;

        case ID_VIEW_CONSTRUCTIONEDGES:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_constr)
            {
                view_constr = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_CONSTRUCTIONEDGES, MF_UNCHECKED);
            }
            else
            {
                view_constr = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_CONSTRUCTIONEDGES, MF_CHECKED);
            }
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_EDGE), view_constr);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_RECT), view_constr);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_CIRCLE), view_constr);
            break;

        case ID_VIEW_RENDEREDVIEW:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_rendered)
            {
                view_rendered = FALSE;
                glEnable(GL_BLEND);
                CheckMenuItem(hMenu, ID_VIEW_RENDEREDVIEW, MF_UNCHECKED);
            }
            else
            {
                view_rendered = TRUE;
                glDisable(GL_BLEND);
                CheckMenuItem(hMenu, ID_VIEW_RENDEREDVIEW, MF_CHECKED);
            }
            EnableWindow(GetDlgItem(hWndToolbar, IDB_EDGE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_RECT), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CIRCLE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_ARC_EDGE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_BEZIER_EDGE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_EXTRUDE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_EDGE), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_RECT), !view_rendered);
            EnableWindow(GetDlgItem(hWndToolbar, IDB_CONST_CIRCLE), !view_rendered);
            break;

        case ID_VIEW_TOP:
            facing_plane = &plane_XY;
            facing_index = PLANE_XY;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane XY\r\n");
#endif
            trackball_InitQuat(quat_XY);
            break;

        case ID_VIEW_FRONT:
            facing_plane = &plane_YZ;
            facing_index = PLANE_YZ;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane YZ\r\n");
#endif
            trackball_InitQuat(quat_YZ);
            break;

        case ID_VIEW_LEFT:
            facing_plane = &plane_XZ;
            facing_index = PLANE_XZ;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane XZ\r\n");
#endif
            trackball_InitQuat(quat_XZ);
            break;

        case ID_VIEW_BOTTOM:
            facing_plane = &plane_mXY;
            facing_index = PLANE_MINUS_XY;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane -XY\r\n");
#endif
            trackball_InitQuat(quat_mXY);
            break;

        case ID_VIEW_BACK:
            facing_plane = &plane_mYZ;
            facing_index = PLANE_MINUS_YZ;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane -YZ\r\n");
#endif
            trackball_InitQuat(quat_mYZ);
            break;

        case ID_VIEW_RIGHT:
            facing_plane = &plane_mXZ;
            facing_index = PLANE_MINUS_XZ;
#ifdef DEBUG_COMMAND_FACING
            Log("Facing plane -XZ\r\n");
#endif
            trackball_InitQuat(quat_mXZ);
            break;

        case ID_VIEW_ORTHO:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (!view_ortho)
            {
                view_ortho = TRUE;
                CheckMenuItem(hMenu, ID_VIEW_PERSPECTIVE, MF_UNCHECKED);
                CheckMenuItem(hMenu, ID_VIEW_ORTHO, MF_CHECKED);
            }
            break;

        case ID_VIEW_PERSPECTIVE:
            hMenu = GetSubMenu(GetMenu(auxGetHWND()), 2);
            if (view_ortho)
            {
                view_ortho = FALSE;
                CheckMenuItem(hMenu, ID_VIEW_ORTHO, MF_UNCHECKED);
                CheckMenuItem(hMenu, ID_VIEW_PERSPECTIVE, MF_CHECKED);
            }
            break;

        case ID_FILE_NEW:
        case ID_FILE_OPEN:
            if (drawing_changed)
            {
                int rc = MessageBox(auxGetHWND(), "File modified. Save it?", curr_filename, MB_YESNOCANCEL | MB_ICONWARNING);

                if (rc == IDCANCEL)
                    break;
                else if (rc == IDYES)
                    serialise_tree(&object_tree, curr_filename);     // TODO curr_filename NULL --> save file box
            }

            clear_selection(&selection);
            purge_tree(object_tree.obj_list);
            object_tree.obj_list = NULL;
            drawing_changed = FALSE;
            clean_checkpoints(curr_filename);
            generation = 0;
            curr_filename[0] = '\0';
            curr_title[0] = '\0';
            SetWindowText(auxGetHWND(), "LoftyCAD");

            if (LOWORD(wParam) == ID_FILE_NEW)
                break;

            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = auxGetHWND();
            ofn.lpstrFilter = "LoftyCAD Files\0*.LCD\0All Files\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrDefExt = "lcd";
            ofn.lpstrFile = curr_filename;
            ofn.nMaxFile = 256;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn))
            {
                deserialise_tree(&object_tree, curr_filename);
                drawing_changed = FALSE;
                strcpy_s(window_title, 256, curr_filename);
                strcat_s(window_title, 256, " - ");
                strcat_s(window_title, 256, curr_title);
                SetWindowText(auxGetHWND(), window_title);
                hMenu = GetSubMenu(GetMenu(auxGetHWND()), 0);
                hMenu = GetSubMenu(hMenu, 8);
                insert_filename_to_MRU(hMenu, curr_filename);
                populate_treeview();
            }

            break;

        case ID_FILE_SAVE:
            if (curr_filename[0] != '\0')
            {
                serialise_tree(&object_tree, curr_filename);
                drawing_changed = FALSE;
                break;
            }
            // If no filename, fall through to save as...
        case ID_FILE_SAVEAS:
            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = auxGetHWND();
            ofn.lpstrFilter = "LoftyCAD Files\0*.LCD\0All Files\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrDefExt = "lcd";
            ofn.lpstrFile = curr_filename;
            ofn.nMaxFile = 256;
            ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
            if (GetSaveFileName(&ofn))
            {
                serialise_tree(&object_tree, curr_filename);
                drawing_changed = FALSE;
                strcpy_s(window_title, 256, curr_filename);
                strcat_s(window_title, 256, " - ");
                strcat_s(window_title, 256, curr_title);
                SetWindowText(auxGetHWND(), window_title);
                hMenu = GetSubMenu(GetMenu(auxGetHWND()), 0);
                hMenu = GetSubMenu(hMenu, 8);
                insert_filename_to_MRU(hMenu, curr_filename);
            }

            break;

        case ID_FILE_EXPORT:
            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = auxGetHWND();
            ofn.lpstrFilter = "STL Files\0*.STL\0All Files\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrDefExt = "stl";
            strcpy_s(new_filename, 256, curr_filename);
            new_filename[strlen(new_filename) - 4] = '\0';   // TODO do this better
            ofn.lpstrFile = new_filename;
            ofn.nMaxFile = 256;
            ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
            if (GetSaveFileName(&ofn))
            {
                export_object_tree(&object_tree, new_filename);
            }

            break;

        case ID_PREFERENCES_SETTINGS:
            display_help("Preferences");
            DialogBox(hInst, MAKEINTRESOURCE(IDD_PREFS), auxGetHWND(), prefs_dialog);
            strcpy_s(window_title, 256, curr_filename);
            strcat_s(window_title, 256, " - ");
            strcat_s(window_title, 256, curr_title);
            SetWindowText(auxGetHWND(), window_title);
            break;

        case ID_MRU_FILE1:
        case ID_MRU_FILE2:
        case ID_MRU_FILE3:
        case ID_MRU_FILE4:
            if (get_filename_from_MRU(LOWORD(wParam) - ID_MRU_BASE, new_filename))
            {
                if (drawing_changed)
                {
                    int rc = MessageBox(auxGetHWND(), "File modified. Save it?", curr_filename, MB_YESNOCANCEL | MB_ICONWARNING);

                    if (rc == IDCANCEL)
                        break;
                    else if (rc == IDYES)
                        serialise_tree(&object_tree, curr_filename);
                }

                clear_selection(&selection);
                purge_tree(object_tree.obj_list);
                object_tree.obj_list = NULL;
                drawing_changed = FALSE;
                curr_filename[0] = '\0';
                curr_title[0] = '\0';
                SetWindowText(auxGetHWND(), "LoftyCAD");

                if (!deserialise_tree(&object_tree, new_filename))
                {
                    MessageBox(auxGetHWND(), "File not found", new_filename, MB_OK | MB_ICONWARNING);
                }
                else
                {
                    strcpy_s(curr_filename, 256, new_filename);
                    strcpy_s(window_title, 256, curr_filename);
                    strcat_s(window_title, 256, " - ");
                    strcat_s(window_title, 256, curr_title);
                    SetWindowText(auxGetHWND(), window_title);
                    populate_treeview();
                }
            }
            break;

        case ID_EDIT_CUT:
            clear_selection(&clipboard);
            clipboard = selection;
            selection = NULL;

            // You can't cut a component, only a top-level object (in the object tree).
            // Check that they are in fact top-level before unlinking them.
            for (obj = clipboard; obj != NULL; obj = obj->next)
            {
                if (is_top_level_object(obj->prev, &object_tree))
                    delink_group(obj->prev, &object_tree);
                // TODO else: remove the object in the clipboard here, because re-pasting will put it in the same place...
            }
            clip_xoffset = 0;
            clip_yoffset = 0;
            clip_zoffset = 0;
            drawing_changed = TRUE;
            write_checkpoint(&object_tree, curr_filename);
            break;

        case ID_EDIT_COPY:
            // As above, but don't unlink the objects. The clipboard is a reference to 
            // existing objects, not a copy of them. TODO: Make sure this is still OK.
            // Excel does it like this, and it drives me nuts sometimes.
            clear_selection(&clipboard);
            clipboard = selection;
            selection = NULL;
            clip_xoffset = 10;
            clip_yoffset = 10;
            clip_zoffset = 0;
            break;

        case ID_EDIT_PASTE:
            clear_selection(&selection);

            // Link the objects in the clipboard to the object tree. Do this by making a 
            // copy of them, as we might want to paste the same thing multiple times.
            // Each successive copy will go in at an increasing offset in the facing plane.
            for (obj = clipboard; obj != NULL; obj = obj->next)
            {
                Object * new_obj = copy_obj(obj->prev, clip_xoffset, clip_yoffset, clip_zoffset);

                clear_move_copy_flags(obj->prev);
                link_tail_group(new_obj, &object_tree);
                sel_obj = obj_new();
                sel_obj->next = selection;
                selection = sel_obj;
                sel_obj->prev = new_obj;
            }

            clip_xoffset += 10;  // very temporary (it needs to be scaled rasonably, and in the facing plane)
            clip_yoffset += 10;
            drawing_changed = TRUE;
            write_checkpoint(&object_tree, curr_filename);
            break;

        case ID_EDIT_DELETE:
            // You can't delete a component, only a top-level object (in the object tree).
            // Check that they are in fact top-level before deleting them.
            for (obj = selection; obj != NULL; obj = obj->next)
            {
                if (is_top_level_object(obj->prev, &object_tree))
                {
                    delink_group(obj->prev, &object_tree);
                    purge_obj(obj->prev);
                }
            }
            clear_selection(&selection);
            drawing_changed = TRUE;
            write_checkpoint(&object_tree, curr_filename);
            break;

        case ID_EDIT_SELECTALL:
            // Put all top-level objects on the selection list.
            clear_selection(&selection);
            for (obj = object_tree.obj_list; obj != NULL; obj = obj->next)
            {
                sel_obj = obj_new();
                sel_obj->next = selection;
                selection = sel_obj;
                sel_obj->prev = obj;
            }
            break;

        case ID_EDIT_SELECTNONE:
            clear_selection(&selection);
            break;

        case ID_EDIT_UNDO:
            generation--;
            read_checkpoint(&object_tree, curr_filename, generation);
            break;

        case ID_EDIT_REDO:
            generation++;
            read_checkpoint(&object_tree, curr_filename, generation);
            break;
        }
        break;

    case WM_INITMENUPOPUP:
        if ((HMENU)wParam == GetSubMenu(GetMenu(auxGetHWND()), 0))
        {
            EnableMenuItem((HMENU)wParam, ID_FILE_SAVE, drawing_changed ? MF_ENABLED : MF_GRAYED);
        }
        else if ((HMENU)wParam == GetSubMenu(GetMenu(auxGetHWND()), 1))
        {
            EnableMenuItem((HMENU)wParam, ID_EDIT_UNDO, generation > 0 ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, ID_EDIT_REDO, generation < latest_generation ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, ID_EDIT_CUT, selection != NULL ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, ID_EDIT_COPY, selection != NULL ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, ID_EDIT_DELETE, selection != NULL ? MF_ENABLED : MF_GRAYED);
        }
        else if ((HMENU)wParam == GetSubMenu(GetMenu(auxGetHWND()), 2))
        {
            EnableMenuItem((HMENU)wParam, ID_VIEW_CONSTRUCTIONEDGES, view_rendered ? MF_GRAYED : MF_ENABLED);
        }

        break;
    }
    return 0;
}

