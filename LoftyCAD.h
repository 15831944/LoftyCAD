#pragma once

#include <GL/gl.h>
#include <GL/glu.h>
#include "glaux/glaux.h"
#include <math.h>
#include <stdio.h>
#include "resource.h"
#include "trackbal.h"
#include "objtree.h"
#include "geometry.h"
#include "draw3d.h"
#include "dimensions.h"
#include "registry.h"
#include "triangulate.h"


// Version
#define LOFTYCAD_VERSION "1.4"
#define LOFTYCAD_BRANCH "(Material Girl)"

// States the app can be in.
typedef enum STATE
{
    STATE_NONE,                 // Default state when orbiting with the trackball, panning or zooming
    STATE_MOVING,               // Moving an object, or the selection
    STATE_DRAGGING_SELECT,      // Dragging out a selection window

    // Starting states are entered when a toolbar button is pressed.
    // In starting states, snapping targets are displayed at the mouse position.
    STATE_STARTING_EDGE,        // Starting to draw (toolbar button pressed, but mouse not yet down)
    STATE_STARTING_RECT,
    STATE_STARTING_CIRCLE,
    STATE_STARTING_BEZIER,
    STATE_STARTING_ARC,
    STATE_STARTING_EXTRUDE,
    STATE_STARTING_TEXT,
    STATE_STARTING_SCALE,
    STATE_STARTING_ROTATE,

    STATE_DRAWING_EDGE,         // Actually drawing something (left mouse down and dragging)
    STATE_DRAWING_RECT,         // NOTE: These must be in the same order as the starting states
    STATE_DRAWING_CIRCLE,
    STATE_DRAWING_BEZIER,
    STATE_DRAWING_ARC,
    STATE_DRAWING_EXTRUDE,
    STATE_DRAWING_TEXT,
    STATE_DRAWING_SCALE,
    STATE_DRAWING_ROTATE,

    STATE_MAX,                  // Must be the highest numbered

    // The offset between members of the above two sets of possible states.
    STATE_DRAWING_OFFSET = STATE_DRAWING_EDGE - STATE_STARTING_EDGE
} STATE;

typedef enum
{
    BLEND_OPAQUE,               // No blending, objects are opaque.
    BLEND_MULTIPLY,             // Multiply blending (works independent of front-to-back ordering)
    BLEND_ALPHA                 // Alpha blending (note: may be errors, as objects are NOT sorted front-to-back)
} BlendMode;

typedef enum
{
    DIRN_X = 1,                 // Scale in X, Y or Z. Bit field to allow combinations.
    DIRN_Y = 2,
    DIRN_Z = 4
} SCALED;

// Externs
extern HINSTANCE hInst;

extern GLint wWidth, wHeight;
extern int toolbar_bottom;
extern float xTrans, yTrans, zTrans;
extern int zoom_delta;

extern int	left_mouseX, left_mouseY;
extern int	orig_left_mouseX, orig_left_mouseY;
extern int	right_mouseX, right_mouseY;
extern int key_status;
extern BOOL	left_mouse;
extern BOOL	right_mouse;

extern HWND hWndToolbar;
extern HWND hWndDebug;
extern HWND hWndTree;
extern HWND hWndDims;
extern HWND hWndHelp;
extern BOOL view_tools;
extern BOOL view_debug;
extern BOOL view_tree;
extern BOOL view_help;
extern BOOL view_rendered;
extern BOOL view_constr;
extern BOOL view_halo;
extern BOOL view_ortho;
extern BOOL micro_moved;

extern STATE app_state;
extern BOOL construction;
extern ListHead selection;
extern ListHead clipboard;
extern ListHead saved_list;
extern Group object_tree;
extern BOOL drawing_changed;
extern Object *curr_obj;
extern Object *picked_obj;
extern Object *raw_picked_obj;
extern Point picked_point;
extern Point new_point;
extern Point last_point;
extern Plane *picked_plane;
extern Plane *facing_plane;
extern PLANE facing_index;
extern Plane centre_facing_plane;
extern Text *curr_text;
extern SCALED scaled_dirn;
extern SCALED scaled;
extern float total_angle;
extern float halo_rad;
extern BOOL suppress_drawing;

extern char curr_filename[];
extern float grid_snap;
extern float tolerance;
extern float snap_tol;
extern int tol_log;
extern int angle_snap;
extern BOOL snapping_to_grid;
extern BOOL snapping_to_angle;
extern float chamfer_rad;
extern float round_rad;
extern float half_size;
extern float clip_xoffset, clip_yoffset, clip_zoffset;
extern int generation;
extern int latest_generation;
extern int max_generation;

extern Object *treeview_highlight;

extern Plane plane_XY;
extern Plane plane_XZ;
extern Plane plane_YZ;
extern Plane plane_mXY;
extern Plane plane_mXZ;
extern Plane plane_mYZ;

extern float quat_XY[4];
extern float quat_YZ[4];
extern float quat_XZ[4];
extern float quat_mXY[4];
extern float quat_mYZ[4];
extern float quat_mXZ[4];

extern float bucket_size;
extern int n_buckets;

extern BlendMode view_blend;

#define MAX_MATERIAL 32
extern Material materials[MAX_MATERIAL];

// base of menu ID's for materials
#define ID_MATERIAL_BASE 70000


// Debug stuff

// Debugging defines
#undef DEBUG_FACE_SHADE
#undef DEBUG_DRAW_RECT_NORMAL
#undef DEBUG_POSITION_ZOOM
#undef DEBUG_PICK
#undef DEBUG_PICK_ALL
#undef DEBUG_LEFT_UP_FACING
#undef DEBUG_LEFT_UP_MODELVIEW
#undef DEBUG_COMMAND_FACING
#undef DEBUG_TOOLBAR_FACING
#undef DEBUG_REVERSE_RECT_FACE
#undef DEBUG_VIEW_LIST_ARC
#define DEBUG_HIGHLIGHTING_ENABLED
#undef DEBUG_WRITE_VOL_MESH


#ifdef BREAK_ON_ASSERT
#ifdef _DEBUG
#define ASSERT_BREAK  DebugBreak()
#else
#define ASSERT_BREAK
#endif
#else
#define ASSERT_BREAK
#endif

// Where do assert and log messages go
#undef USE_DEBUGSTR_LOG

#ifdef USE_DEBUGSTR_LOG
#define Log(msg)            OutputDebugString(msg);
#define LogShow(msg)        OutputDebugString(msg);
#else
#define Log(msg)            SendDlgItemMessage(hWndDebug, IDC_DEBUG, EM_REPLACESEL, 0, (LPARAM)(msg));
#define LogShow(msg)        \
{                       \
    ShowWindow(hWndDebug, SW_SHOW);                                                     \
    CheckMenuItem(GetSubMenu(GetMenu(auxGetHWND()), 2), ID_VIEW_DEBUGLOG, MF_CHECKED);  \
    view_debug = TRUE;                                                                  \
    SendDlgItemMessage(hWndDebug, IDC_DEBUG, EM_REPLACESEL, 0, (LPARAM)(msg));          \
    SendDlgItemMessage(hWndDebug, IDC_DEBUG, EM_REPLACESEL, 0, (LPARAM)"\r\n");         \
    ASSERT_BREAK;                                                                       \
}
#endif

#define ASSERT(exp, msg)    do { if (!(exp)) LogShow(msg) } while(0)


// Debug externs
extern BOOL debug_view_bbox;
extern BOOL debug_view_normals;
extern BOOL debug_view_viewlist;

// Some forwards
Object * Pick(GLint x_pick, GLint y_pick, BOOL force_pick);
void Pick_all_in_rect(GLint x_pick, GLint y_pick, GLint width, GLint height);
void CALLBACK Draw(BOOL picking, GLint x_pick, GLint y_pick, GLint w_pick, GLint h_pick);
void CALLBACK Position(BOOL picking, GLint x_pick, GLint y_pick, GLint w_pick, GLint h_pick);
HWND init_help_window(void);
void display_help(char *key);
void display_help_state(STATE state);
void change_state(STATE new_state);
void display_cursor(STATE new_state);

BOOL is_selected_direct(Object *obj, Object **prev_in_list);
BOOL is_selected_parent(Object *obj);
BOOL remove_from_selection(Object *obj);
void clear_selection(ListHead *sel_list);

char *obj_description(Object *obj, char *descr, int descr_len, BOOL verbose);
void populate_treeview(void);
void CALLBACK right_click(AUX_EVENTREC *event);
void CALLBACK check_file_changed(HWND hWnd);
void update_drawing(void);
void contextmenu(Object *picked_obj, POINT pt);
void load_materials_menu(HMENU hMenu, BOOL show_all_checks, int which_check);

// Forwards for window procedures and similar
int CALLBACK Command(int message, int wParam, int lParam);
int WINAPI help_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI debug_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI font_hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI toolbar_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI dimensions_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI prefs_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI treeview_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI transform_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
int WINAPI materials_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

