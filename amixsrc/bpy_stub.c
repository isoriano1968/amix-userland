/* bpy_stub.c -- Blender 2.31a for AMIX without Python.  The rest of Blender
 * reaches Python only through these 17 functions (python/BPY_extern.h);
 * here they do nothing, so scripts and script links are simply absent. */
#include <stddef.h>

struct SpaceText; struct Text; struct ID; struct ScriptLink; struct _object;

void BPY_start_python(void) { }
void BPY_end_python(void) { }
void BPY_syspath_append_pythondir(void) { }
int BPY_Err_getLinenumber(void) { return 0; }
const char *BPY_Err_getFilename(void) { return ""; }
struct _object *BPY_txt_do_python(struct SpaceText *st) { (void) st; return NULL; }
void BPY_free_compiled_text(struct Text *text) { (void) text; }
void BPY_clear_bad_scriptlinks(struct Text *byebye) { (void) byebye; }
void BPY_do_all_scripts(short event) { (void) event; }
void BPY_do_pyscript(struct ID *id, short event) { (void) id; (void) event; }
void BPY_free_scriptlink(struct ScriptLink *slink) { (void) slink; }
void BPY_copy_scriptlink(struct ScriptLink *scriptlink) { (void) scriptlink; }
int BPY_call_importloader(char *name) { (void) name; return 0; }
int BPY_spacetext_is_pywin(struct SpaceText *st) { (void) st; return 0; }
void BPY_spacetext_do_pywin_draw(struct SpaceText *st) { (void) st; }
void BPY_spacetext_do_pywin_event(struct SpaceText *st, unsigned short event, short val)
{ (void) st; (void) event; (void) val; }
void init_syspath(void) { }
