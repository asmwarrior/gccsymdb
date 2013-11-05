/* vim: foldmarker=<([{,}])> foldmethod=marker
 * Copyright, code convention, guide, #include <([{
 * Copyright (C) zyf.zeroos@gmail.com, released on GPL license. Go first from
 * doc.txt.
 *
 * Code convention:
 *   1) If there's a class in a fold, the fold is treated as a class-fold, and
 *   all funtions starting with `classname_' are treated as public.
 *   2) All in common fold are public.
 *
 * Guide: Definition extraction process is surrounding with cache, macro.
 *   1) Class cache caches all itokens from gcc. It also caches all macroes by
 *   cache.auxiliary, so I use them to reversely relocate itoken to chtoken
 *   (cache_itoken_to_chtoken).
 *   2) Class macro records macro data from gcc and dump them to
 *   cache.auxiliary, it's also in the charge of macro-cancel, macro-cascaded
 *   and macro-cascaded-cancel expansion cases, see doc.txt.
 *
 *   3) Fold plugin-callbacks: implements new PLUGIN_XXXX event. Dump token
 *   data to class cache/macro, parse DEF_XXX when special event occurs. I
 *   list all possible syntax cases before every function, and use them
 *   reversely search user-definition from cache.itokens.
 *   4) Fold cpp-callbacks: implements cpp_callback::XXXX, collects macro data
 *   for class macro, DEF_MACRO and file dependence.
 *   5) Class def: cooperate plugin-callbacks to parse definition in
 *   cache.itokens and is in charge of Definition fold of init.sql.
 *   6) Class file: is in charge of File fold of init.sql.
 *   7) Class ifdef: is in charge of Ifdef table of init.sql.
 *   8) Class funp_alias: is in charge of FunpAlias table of init.sql.
 *   9) Class mo: now, is also charge of Macro table.
 *
 * GDB Guide:
 *   1) To support debug, I place a class `bug' in common fold, to use it,
 *     gdb -x gdb.script
 *     gdb> dlc
 *     gdb> call bug_init("filename", offset)
 *   only leader expanded token and common token can be break.
 *   2) file::dump_includee and cache::dump_cache.
 * */

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "gcc/c-tree.h"
#include "c-family/c-common.h"
#include "c-family/c-pragma.h"
#include "input.h"
#include "dyn-string.h"
#include <sys/stat.h>
#include <time.h>
#include "libcpp/include/cpplib.h"
#include "libcpp/internal.h"
#include <sqlite3.h>
/* }])> */

/* common <([{ */
typedef const struct cpp_token *cpp_token_p;
DEF_VEC_I (int);
DEF_VEC_ALLOC_I (int, heap);

static dyn_string_t gbuf;	/* Global temporary variable. */

/* sqlite auxiliary <([{ */
static struct sqlite3 *db;

static void
db_error (int cond)
{
  if (cond)
    {
      sqlite3_exec (db, "end transaction;", NULL, 0, NULL);
      sqlite3_close (db);
      fprintf (stderr, "SQLite3 error: %s\n", sqlite3_errmsg (db));
      exit (1);
    }
}

inline static void
revalidate_sql (struct sqlite3_stmt *stmt)
{
  db_error (sqlite3_clear_bindings (stmt));
  db_error (sqlite3_reset (stmt));
}

inline static void
execute_sql (struct sqlite3_stmt *stmt)
{
  db_error (sqlite3_step (stmt) != SQLITE_DONE);
  revalidate_sql (stmt);
}

/* }])> */

/* control_panel <([{ */
static struct
{
  dyn_string_t db_file;
  dyn_string_t prj_dir;
  dyn_string_t cwd;
  dyn_string_t main_file;
  bool can_update_file;
} control_panel;

static const char *
canonical_path (const char *f)
{
  static char path[PATH_MAX + 1];
  char *str = lrealpath (f);
  int len = dyn_string_length (control_panel.prj_dir);
  if (strncmp (str, dyn_string_buf (control_panel.prj_dir), len) == 0)
    strcpy (path, str + len);
  else
    strcpy (path, str);
  free (str);
  return path;
}

static void
control_panel_init (const char *main_file)
{
  int nrow, ncolumn;
  char *error_msg, **result;

  /* control_panel.db_file has been initilized in plugin_init. */
  control_panel.prj_dir = dyn_string_new (PATH_MAX + 1);
  control_panel.cwd = dyn_string_new (PATH_MAX + 1);
  control_panel.main_file = dyn_string_new (PATH_MAX + 1);
  dyn_string_copy_cstr (control_panel.cwd, getpwd ());
  dyn_string_copy_cstr (control_panel.main_file, canonical_path (main_file));
  /* initilize control data. */
  db_error (sqlite3_get_table (db,
			       "select projectRootPath, canUpdateFile from ProjectOverview;",
			       &result, &nrow, &ncolumn, &error_msg));
  dyn_string_copy_cstr (control_panel.prj_dir, result[2]);
  control_panel.can_update_file = strcmp (result[3], "t") == 0 ? true : false;
  sqlite3_free_table (result);
}

static void
control_panel_tini (void)
{
  dyn_string_delete (control_panel.main_file);
  dyn_string_delete (control_panel.cwd);
  dyn_string_delete (control_panel.prj_dir);
  dyn_string_delete (control_panel.db_file);
}

/* }])> */

/* bug <([{ */
static struct
{
  char file_name[PATH_MAX + 1];
  int file_offset;
  int file_id;
} bug =
{
.file_id = -1,};

/* My plugin callback. */
static void
bug_trap_file (const char *fn, int fid)
{
  if (bug.file_id == 0 && strcmp (fn, bug.file_name) == 0)
    bug.file_id = fid;
}

static int file_get_current_fid (void);
static void
bug_trap_token (int off)
{
  if (file_get_current_fid () == bug.file_id && off == bug.file_offset)
    asm volatile ("int $3");
}

static void __attribute__ ((used)) bug_init (char *fn, int off)
{
  strcpy (bug.file_name, fn);
  bug.file_offset = off;
  bug.file_id = 0;
}

/* }])> */

static void
print_token (cpp_token_p token, dyn_string_t str)
{
  unsigned char *head, *tail;
  dyn_string_resize (str, cpp_token_len (token));
  head = (unsigned char *) dyn_string_buf (str);
  tail = cpp_spell_token (parse_in, token, head, false);
  *tail = '\0';
  dyn_string_length (str) = tail - head;
}

/* }])> */

/* file <([{ */
/* The class is in charge of file and FileDependence table. */
typedef struct
{
  int id;
  dyn_string_t name;
} include_unit;
DEF_VEC_O (include_unit);
DEF_VEC_ALLOC_O (include_unit, heap);

static struct
{
  VEC (include_unit, heap) * includee;	/* file-depend stack. */

  struct sqlite3_stmt *select_chfile;
  struct sqlite3_stmt *insert_chfile;
  struct sqlite3_stmt *select_filedep;
  struct sqlite3_stmt *insert_filedep;
} file;

static long long
get_mtime (const char *file)
{
  struct stat filestat;
  if (stat (file, &filestat) != 0)
    {
      perror (NULL);
      sqlite3_close (db);
      exit (1);
    }
  gcc_assert (sizeof (filestat.st_mtime) <= sizeof (long long));
  return (long long) filestat.st_mtime;
}

/* debug purpose. */
static void __attribute__ ((used)) dump_includee (void)
{
  int ix;
  include_unit *p;
  FOR_EACH_VEC_ELT (include_unit, file.includee, ix, p)
    printf ("%d:%s >> ", p->id, dyn_string_buf (p->name));
  printf ("\n");
}

static int
file_get_fid_from_db (const char *file_name)
{
  int ret = 0;
  db_error (sqlite3_bind_text
	    (file.select_chfile, 1, file_name, strlen (file_name),
	     SQLITE_STATIC));
  if (sqlite3_step (file.select_chfile) == SQLITE_ROW)
    ret = sqlite3_column_int (file.select_chfile, 0);
  else
    gcc_assert (false);
  revalidate_sql (file.select_chfile);
  return ret;
}

static int
file_get_current_fid (void)
{
  return VEC_last (include_unit, file.includee)->id;
}

static void
insert_filedep (int hid, int offset)
{
  int previd = file_get_current_fid ();
  if (previd == hid)
    /* Rare case, file includes itself. */
    return;
  db_error (sqlite3_bind_int (file.select_filedep, 1, previd));
  db_error (sqlite3_bind_int (file.select_filedep, 2, hid));
  db_error (sqlite3_bind_int (file.select_filedep, 3, offset));
  if (sqlite3_step (file.select_filedep) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (file.insert_filedep, 1, previd));
      db_error (sqlite3_bind_int (file.insert_filedep, 2, hid));
      db_error (sqlite3_bind_int (file.insert_filedep, 3, offset));
      execute_sql (file.insert_filedep);
    }
  revalidate_sql (file.select_filedep);
}

static void
insert_file (const char *fn, int *file_id, long long *mtime, bool sysp)
{
  long long new_mtime = get_mtime (fn);
  fn = canonical_path (fn);
  int size = strlen (fn);
  db_error (sqlite3_bind_text
	    (file.select_chfile, 1, fn, size, SQLITE_STATIC));
reinsert:
  if (sqlite3_step (file.select_chfile) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_text (file.insert_chfile, 1,
				   fn, size, SQLITE_STATIC));
      db_error (sqlite3_bind_int64 (file.insert_chfile, 2, new_mtime));
      if (sysp)
	db_error (sqlite3_bind_text (file.insert_chfile, 3,
				     "t", sizeof ("t"), SQLITE_STATIC));
      else
	db_error (sqlite3_bind_text (file.insert_chfile, 3,
				     "f", sizeof ("f"), SQLITE_STATIC));
      execute_sql (file.insert_chfile);
      sqlite3_reset (file.select_chfile);
      db_error (sqlite3_step (file.select_chfile) != SQLITE_ROW);
    }
  *file_id = sqlite3_column_int (file.select_chfile, 0);
  *mtime = sqlite3_column_int64 (file.select_chfile, 1);
  if (new_mtime > *mtime)
    {
      if (!control_panel.can_update_file)
	{
	  fprintf
	    (stderr, "Update single file is disabled "
	     "according to ProjectOverview::canUpdateFile parameter.");
	  gcc_assert (false);
	}
      sprintf (dyn_string_buf (gbuf), "delete from chFile where id = %d;",
	       *file_id);
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
      sqlite3_reset (file.select_chfile);
      goto reinsert;
    }
  revalidate_sql (file.select_chfile);
  bug_trap_file (fn, *file_id);
}

static void
file_insert_filedep (int token_offset, const char *fname, bool sys)
{
  int id;
  long long mtime;
  insert_file (fname, &id, &mtime, sys);
  insert_filedep (id, token_offset);
}

static bool mo_isvalid (void);
static void ifdef_push (void);
static void
file_push (const char *f, bool sys)
{
  int id;
  long long mtime;
  gcc_assert (!mo_isvalid ());
  insert_file (f, &id, &mtime, sys);
  include_unit *p = VEC_safe_push (include_unit, heap, file.includee, NULL);
  p->id = id;
  p->name = dyn_string_new (32);
  dyn_string_copy_cstr (p->name, f);
  ifdef_push ();
}

static void ifdef_pop (void);
static void
file_pop (void)
{
  ifdef_pop ();
  gcc_assert (!mo_isvalid ());
  include_unit *p = VEC_last (include_unit, file.includee);
  dyn_string_delete (p->name);
  VEC_pop (include_unit, file.includee);
}

static void
file_init (const char *main_file)
{
  file.includee = VEC_alloc (include_unit, heap, 10);

  db_error (sqlite3_prepare_v2 (db,
				"select id, mtime from chFile where name = ?;",
				-1, &file.select_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into chFile values (NULL, ?, ?, ?);",
				-1, &file.insert_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FileDependence where chID = ? and hID = ? and offset = ?;",
				-1, &file.select_filedep, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FileDependence values (?, ?, ?);",
				-1, &file.insert_filedep, 0));

  file_push (main_file, false);
}

static void
file_tini (void)
{
  file_pop ();
  sqlite3_finalize (file.insert_filedep);
  sqlite3_finalize (file.select_filedep);
  sqlite3_finalize (file.insert_chfile);
  sqlite3_finalize (file.select_chfile);

  VEC_free (include_unit, heap, file.includee);
}

/* }])> */

/* macro <([{ */
static struct
{
  /* expanded_count field is updated for debug only. */
  int expanded_count;
  int macro_count;
  bool valid;

  /* For Macro table of init.sql */
  int process;
  int letFileID;
  int letOffset;
  int defFileID;
  int defFileOffset;
  dyn_string_t expandedTokens;
  dyn_string_t macroTokens;
  struct sqlite3_stmt *insert_macro;
} mo =
{
0, 0, false};

static void
mo_append_expanded_token (cpp_token_p token)
{
  if (mo.process == 2)
    {
      print_token (token, gbuf);
      dyn_string_append (mo.expandedTokens, gbuf);
      dyn_string_append_cstr (mo.expandedTokens, " ");
    }
  mo.expanded_count++;
}

static void
mo_append_macro_token (cpp_token_p token)
{
  if (mo.process == 2)
    {
      print_token (token, gbuf);
      dyn_string_append (mo.macroTokens, gbuf);
      dyn_string_append_cstr (mo.macroTokens, " ");
    }
  mo.macro_count++;
}

static void
mo_leave (void)
{
  if (mo.process == 2)
    {
      db_error (sqlite3_bind_int (mo.insert_macro, 1, mo.defFileID));
      db_error (sqlite3_bind_int (mo.insert_macro, 2, mo.defFileOffset));
      db_error (sqlite3_bind_text
		(mo.insert_macro, 3, dyn_string_buf (mo.expandedTokens),
		 dyn_string_length (mo.expandedTokens), SQLITE_STATIC));
      db_error (sqlite3_bind_text
		(mo.insert_macro, 4, dyn_string_buf (mo.macroTokens),
		 dyn_string_length (mo.macroTokens), SQLITE_STATIC));
      execute_sql (mo.insert_macro);
      dyn_string_copy_cstr (mo.expandedTokens, "");
      dyn_string_copy_cstr (mo.macroTokens, "");
      mo.process = 1;
    }
  mo.expanded_count = mo.macro_count = 0;
  gcc_assert (mo.valid == true);
  mo.valid = false;
}

static void
mo_enter (cpp_reader * pfile, cpp_token_p token)
{
  if (mo.process == 1 &&
      file_get_current_fid () == mo.letFileID
      && token->file_offset == mo.letOffset)
    {
      cpp_macro *macro = token->val.node.node->value.macro;
      int file_offset = macro->file_offset;
      source_location sl = macro->line;
      const struct line_map *lm = linemap_lookup (pfile->line_table, sl);
      if (strcmp (lm->to_file, "<command-line>") != 0)
	{
	  mo.defFileID = file_get_fid_from_db (canonical_path (lm->to_file));
	  mo.defFileOffset = file_offset;
	  mo.process = 2;
	}
    }
  mo_append_expanded_token (token);
  gcc_assert (mo.valid == false);
  mo.valid = true;
}

static bool
mo_isvalid (void)
{
  return mo.valid;
}

static void
mo_init (void)
{
  int nrow, ncolumn;
  char *error_msg, **result;
  db_error (sqlite3_get_table (db,
			       "select letFileID, letOffset from Macro;",
			       &result, &nrow, &ncolumn, &error_msg));
  if (nrow == 1)
    {
      mo.process = 1;
      mo.letFileID = atoi (result[2]);
      mo.letOffset = atoi (result[3]);
    }
  sqlite3_free_table (result);

  db_error (sqlite3_prepare_v2 (db,
				"insert into Macro values (NULL, NULL, ?, ?, ?, ?);",
				-1, &mo.insert_macro, 0));
  mo.expandedTokens = dyn_string_new (128);
  mo.macroTokens = dyn_string_new (128);
}

static void
mo_tini (void)
{
  dyn_string_delete (mo.macroTokens);
  dyn_string_delete (mo.expandedTokens);
  sqlite3_finalize (mo.insert_macro);
}

/* }])> */

/* def <([{ */
__extension__ enum definition_flag
{
  DEF_VAR = 1,
  DEF_FUNC,
  DEF_MACRO,
  DEF_TYPEDEF,
  DEF_STRUCT,
  DEF_UNION,
  DEF_ENUM,
  DEF_ENUM_MEMBER,
  DEF_USER,
};

static struct
{
  struct sqlite3_stmt *select_def;
  struct sqlite3_stmt *insert_def;
} def;

static int
insert_def (enum definition_flag flag, dyn_string_t str, int offset)
{
  int fid = file_get_current_fid ();
  int defid;
  db_error (sqlite3_bind_int (def.select_def, 1, fid));
  db_error (sqlite3_bind_text
	    (def.select_def, 2, dyn_string_buf (str),
	     dyn_string_length (str), SQLITE_STATIC));
  db_error (sqlite3_bind_int (def.select_def, 3, flag));
  db_error (sqlite3_bind_int (def.select_def, 4, offset));
  if (sqlite3_step (def.select_def) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (def.insert_def, 1, fid));
      db_error (sqlite3_bind_text
		(def.insert_def, 2, dyn_string_buf (str),
		 dyn_string_length (str), SQLITE_STATIC));
      db_error (sqlite3_bind_int (def.insert_def, 3, flag));
      db_error (sqlite3_bind_int (def.insert_def, 4, offset));
      execute_sql (def.insert_def);
      defid = sqlite3_last_insert_rowid (db);
    }
  else
    defid = sqlite3_column_int (def.select_def, 0);
  revalidate_sql (def.select_def);
  return defid;
}

static void fcallf_callerid (int);
static void faccessv_callerid (int);
static int
def_append (enum definition_flag flag, dyn_string_t str, int offset)
{
  int defid = insert_def (flag, str, offset);
  if (flag == DEF_FUNC)
    {
      fcallf_callerid (defid);
      faccessv_callerid (defid);
    }
  return defid;
}

static void
def_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select id from Definition "
				"where fileID = ? and name = ? and flag = ? and fileOffset = ?;",
				-1, &def.select_def, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into Definition values (NULL, ?, ?, ?, ?);",
				-1, &def.insert_def, 0));
}

static void
def_tini (void)
{
  sqlite3_finalize (def.insert_def);
  sqlite3_finalize (def.select_def);
}

/* }])> */

/* f-call-f <([{ */
static struct
{
  int caller_id;

  struct sqlite3_stmt *select_fcallf;
  struct sqlite3_stmt *insert_fcallf;
} fcallf;

static void
fcallf_callerid (int caller_id)
{
  fcallf.caller_id = caller_id;
}

static void
fcallf_insert (dyn_string_t str, int offset)
{
  int fid = file_get_current_fid ();
  gcc_assert (fcallf.caller_id != -1);
  db_error (sqlite3_bind_int (fcallf.select_fcallf, 1, fcallf.caller_id));
  db_error (sqlite3_bind_int (fcallf.select_fcallf, 2, fid));
  db_error (sqlite3_bind_text
	    (fcallf.select_fcallf, 3, dyn_string_buf (str),
	     dyn_string_length (str), SQLITE_STATIC));
  db_error (sqlite3_bind_int (fcallf.select_fcallf, 4, offset));
  if (sqlite3_step (fcallf.select_fcallf) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (fcallf.insert_fcallf, 1, fcallf.caller_id));
      db_error (sqlite3_bind_int (fcallf.insert_fcallf, 2, fid));
      db_error (sqlite3_bind_text
		(fcallf.insert_fcallf, 3, dyn_string_buf (str),
		 dyn_string_length (str), SQLITE_STATIC));
      db_error (sqlite3_bind_int (fcallf.insert_fcallf, 4, offset));
      execute_sql (fcallf.insert_fcallf);
    }
  revalidate_sql (fcallf.select_fcallf);
}

static void
fcallf_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FunctionCall where "
				" callerID = ? and fileID = ? and name = ? and fileOffset = ?;",
				-1, &fcallf.select_fcallf, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FunctionCall values (?, ?, ?, ?);",
				-1, &fcallf.insert_fcallf, 0));
  fcallf.caller_id = -1;
}

static void
fcallf_tini (void)
{
  sqlite3_finalize (fcallf.insert_fcallf);
  sqlite3_finalize (fcallf.select_fcallf);
}

/* }])> */

/* f-access-v <([{ */
__extension__ enum access_flag
{
  ACCESS_READ = 1,
  ACCESS_WRITE = 2,
  ACCESS_ADDR = 4,
  ACCESS_POINTER_READ = 8,	/* i = *p */
  ACCESS_POINTER_WRITE = 16,	/* *p = i */
};

static struct
{
  int caller_id;

  struct sqlite3_stmt *select_faccess;
  struct sqlite3_stmt *insert_faccess;
  struct sqlite3_stmt *update_faccess;

  struct sqlite3_stmt *select_fpattern;
  struct sqlite3_stmt *insert_fpattern;
} faccessv;

static void
faccessv_callerid (int caller_id)
{
  faccessv.caller_id = caller_id;
}

static void
faccessv_insert_fpattern (dyn_string_t str, int flag)
{
  gcc_assert (faccessv.caller_id != -1);
  db_error (sqlite3_bind_int
	    (faccessv.select_fpattern, 1, faccessv.caller_id));
  db_error (sqlite3_bind_text
	    (faccessv.select_fpattern, 2, dyn_string_buf (str),
	     dyn_string_length (str), SQLITE_STATIC));
  db_error (sqlite3_bind_int (faccessv.select_fpattern, 3, flag));
  if (sqlite3_step (faccessv.select_fpattern) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int
		(faccessv.insert_fpattern, 1, faccessv.caller_id));
      db_error (sqlite3_bind_text
		(faccessv.insert_fpattern, 2, dyn_string_buf (str),
		 dyn_string_length (str), SQLITE_STATIC));
      db_error (sqlite3_bind_int (faccessv.insert_fpattern, 3, flag));
      execute_sql (faccessv.insert_fpattern);
    }
  revalidate_sql (faccessv.select_fpattern);
}

static void
faccessv_insert (dyn_string_t str, int flag, int offset)
{
  int fid = file_get_current_fid ();
  gcc_assert (faccessv.caller_id != -1);
  db_error (sqlite3_bind_int
	    (faccessv.select_faccess, 1, faccessv.caller_id));
  db_error (sqlite3_bind_int (faccessv.select_faccess, 2, fid));
  db_error (sqlite3_bind_text
	    (faccessv.select_faccess, 3, dyn_string_buf (str),
	     dyn_string_length (str), SQLITE_STATIC));
  db_error (sqlite3_bind_int (faccessv.select_faccess, 4, offset));
  if (sqlite3_step (faccessv.select_faccess) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int
		(faccessv.insert_faccess, 1, faccessv.caller_id));
      db_error (sqlite3_bind_int (faccessv.insert_faccess, 2, fid));
      db_error (sqlite3_bind_text
		(faccessv.insert_faccess, 3, dyn_string_buf (str),
		 dyn_string_length (str), SQLITE_STATIC));
      db_error (sqlite3_bind_int (faccessv.insert_faccess, 4, flag));
      db_error (sqlite3_bind_int (faccessv.insert_faccess, 5, offset));
      execute_sql (faccessv.insert_faccess);
    }
  else
    {
      int old_flag = sqlite3_column_int (faccessv.select_faccess, 0);
      if ((old_flag & flag) != flag)
	{
	  db_error (sqlite3_bind_int
		    (faccessv.update_faccess, 1, old_flag | flag));
	  db_error (sqlite3_bind_int
		    (faccessv.update_faccess, 2, faccessv.caller_id));
	  db_error (sqlite3_bind_int (faccessv.update_faccess, 3, fid));
	  db_error (sqlite3_bind_text
		    (faccessv.update_faccess, 4, dyn_string_buf (str),
		     dyn_string_length (str), SQLITE_STATIC));
	  db_error (sqlite3_bind_int (faccessv.update_faccess, 5, offset));
	  execute_sql (faccessv.update_faccess);
	}
    }
  revalidate_sql (faccessv.select_faccess);
}

static void
faccessv_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select flag from FunctionAccess where "
				" funcID = ? and fileID = ? and name = ? "
				" and fileOffset = ?;",
				-1, &faccessv.select_faccess, 0));
  db_error (sqlite3_prepare_v2 (db,
				"update FunctionAccess set flag = ? where "
				" funcID = ? and fileID = ? and name = ? "
				" and fileOffset = ?;",
				-1, &faccessv.update_faccess, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FunctionAccess values (?, ?, ?, ?, ?);",
				-1, &faccessv.insert_faccess, 0));

  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FunctionPattern where "
				" funcID = ? and name = ? and flag = ?;",
				-1, &faccessv.select_fpattern, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FunctionPattern values (?, ?, ?);",
				-1, &faccessv.insert_fpattern, 0));
}

static void
faccessv_tini (void)
{
  sqlite3_finalize (faccessv.insert_fpattern);
  sqlite3_finalize (faccessv.select_fpattern);

  sqlite3_finalize (faccessv.insert_faccess);
  sqlite3_finalize (faccessv.update_faccess);
  sqlite3_finalize (faccessv.select_faccess);
}

/* }])> */

/* ifdef <([{ */
__extension__ enum ifdef_flag
{
  CLAUSE_ITSELF = 1,
  SKIPPED,
  NO_SKIPPED,
};

typedef struct
{
  int start;
  int end;
} pair;
DEF_VEC_O (pair);
DEF_VEC_ALLOC_O (pair, heap);

static struct
{
  VEC (pair, heap) * units;

  struct sqlite3_stmt *select_ifdef;
  struct sqlite3_stmt *insert_ifdef;
} ifdef;

static void
ifdef_append (enum ifdef_flag flag, int offset)
{
  pair *unit = VEC_last (pair, ifdef.units);
  unit->end = offset;
  int fileid = file_get_current_fid ();
  db_error (sqlite3_bind_int (ifdef.select_ifdef, 1, fileid));
  db_error (sqlite3_bind_int (ifdef.select_ifdef, 2, flag));
  db_error (sqlite3_bind_int (ifdef.select_ifdef, 3, unit->start));
  db_error (sqlite3_bind_int (ifdef.select_ifdef, 4, unit->end));
  if (sqlite3_step (ifdef.select_ifdef) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (ifdef.insert_ifdef, 1, fileid));
      db_error (sqlite3_bind_int (ifdef.insert_ifdef, 2, flag));
      db_error (sqlite3_bind_int (ifdef.insert_ifdef, 3, unit->start));
      db_error (sqlite3_bind_int (ifdef.insert_ifdef, 4, unit->end));
      execute_sql (ifdef.insert_ifdef);
    }
  revalidate_sql (ifdef.select_ifdef);
  unit->start = offset;
}

static void
ifdef_push (void)
{
  pair *unit = VEC_safe_push (pair, heap, ifdef.units, NULL);
  unit->start = 0;
  unit->end = -1;
}

static void
ifdef_pop (void)
{
  ifdef_append (NO_SKIPPED, 0x10000000);
  VEC_pop (pair, ifdef.units);
}

static void
ifdef_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from Ifdef "
				"where fileID = ? and flag = ? "
				"and startOffset = ? and endOffset = ?;",
				-1, &ifdef.select_ifdef, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into Ifdef values (?, ?, ?, ?);",
				-1, &ifdef.insert_ifdef, 0));
  ifdef.units = VEC_alloc (pair, heap, 10);
}

static void
ifdef_tini (void)
{
  VEC_free (pair, heap, ifdef.units);
  sqlite3_finalize (ifdef.insert_ifdef);
  sqlite3_finalize (ifdef.select_ifdef);
}

/* }])> */

/* offsetof <([{ */
typedef const char *const_char_p;
DEF_VEC_P (const_char_p);
DEF_VEC_ALLOC_P (const_char_p, heap);

static struct
{
  VEC (const_char_p, heap) * prefix;
  int structid;

  struct sqlite3_stmt *select_offsetof;
  struct sqlite3_stmt *insert_offsetof;
} offsetof;

static bool
offsetof_prepare (int structid)
{
  offsetof.structid = structid;
  db_error (sqlite3_bind_int
	    (offsetof.select_offsetof, 1, offsetof.structid));
  bool result = sqlite3_step (offsetof.select_offsetof) != SQLITE_ROW;
  revalidate_sql (offsetof.select_offsetof);
  return result;
}

static void
offsetof_push (const char *prefix)
{
  VEC_safe_push (const_char_p, heap, offsetof.prefix, prefix);
}

static void
offsetof_pop (void)
{
  VEC_pop (const_char_p, offsetof.prefix);
}

static void
offsetof_commit (const char *member, int offset)
{
  int ix;
  const_char_p p;
  dyn_string_copy_cstr (gbuf, "");
  FOR_EACH_VEC_ELT (const_char_p, offsetof.prefix, ix, p)
  {
    dyn_string_append_cstr (gbuf, p);
    dyn_string_append_cstr (gbuf, ".");
  }
  dyn_string_append_cstr (gbuf, member);
  db_error (sqlite3_bind_int
	    (offsetof.insert_offsetof, 1, offsetof.structid));
  db_error (sqlite3_bind_text
	    (offsetof.insert_offsetof, 2, dyn_string_buf (gbuf),
	     dyn_string_length (gbuf), SQLITE_STATIC));
  db_error (sqlite3_bind_int (offsetof.insert_offsetof, 3, offset));
  execute_sql (offsetof.insert_offsetof);
}

static void
offsetof_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from Offsetof "
				"where structID = ?;",
				-1, &offsetof.select_offsetof, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into Offsetof values (?, ?, ?);",
				-1, &offsetof.insert_offsetof, 0));
  offsetof.prefix = VEC_alloc (const_char_p, heap, 10);
}

static void
offsetof_tini (void)
{
  VEC_free (const_char_p, heap, offsetof.prefix);
  sqlite3_finalize (offsetof.insert_offsetof);
  sqlite3_finalize (offsetof.select_offsetof);
}

/* }])> */

/* funp alias <([{ */
static struct
{
  struct sqlite3_stmt *select_funpalias;
  struct sqlite3_stmt *insert_funpalias;
} funp_alias;

void
funp_alias_append (const char *struct_name, const char *mfp_name,
		   const char *fun_decl, int offset)
{
  int fileid = file_get_current_fid ();
  dyn_string_copy_cstr (gbuf, struct_name);
  dyn_string_append_cstr (gbuf, "::");
  dyn_string_append_cstr (gbuf, mfp_name);
  db_error (sqlite3_bind_int (funp_alias.select_funpalias, 1, fileid));
  db_error (sqlite3_bind_text
	    (funp_alias.select_funpalias, 2, dyn_string_buf (gbuf),
	     dyn_string_length (gbuf), SQLITE_STATIC));
  db_error (sqlite3_bind_text
	    (funp_alias.select_funpalias, 3, fun_decl, -1, SQLITE_STATIC));
  db_error (sqlite3_bind_int (funp_alias.select_funpalias, 4, offset));
  if (sqlite3_step (funp_alias.select_funpalias) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (funp_alias.insert_funpalias, 1, fileid));
      db_error (sqlite3_bind_text
		(funp_alias.insert_funpalias, 2, dyn_string_buf (gbuf),
		 dyn_string_length (gbuf), SQLITE_STATIC));
      db_error (sqlite3_bind_text
		(funp_alias.insert_funpalias, 3, fun_decl, -1,
		 SQLITE_STATIC));
      db_error (sqlite3_bind_int (funp_alias.insert_funpalias, 4, offset));
      execute_sql (funp_alias.insert_funpalias);
    }
  revalidate_sql (funp_alias.select_funpalias);
}

void
funp_alias_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FunpAlias "
				"where fileID = ?"
				" and mfp = ? and funDecl = ?"
				" and offset = ?;",
				-1, &funp_alias.select_funpalias, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FunpAlias values (?, ?, ?, ?);",
				-1, &funp_alias.insert_funpalias, 0));
}

void
funp_alias_tini (void)
{
  sqlite3_finalize (funp_alias.insert_funpalias);
  sqlite3_finalize (funp_alias.select_funpalias);
}

/* }])> */

/* cpp callbacks <([{ */
/* The fold isn't class fold. */
static struct
{
  int start_offset;
  int end_offset;
  bool valid;
} ifdef_helper;

static struct
{
  int macro_tristate;
} define_helper;

static void
cb_lex_token (cpp_reader * pfile, cpp_token_p token)
{
  if (token->type == CPP_EOF)
    return;
  if (mo_isvalid ())
    mo_append_expanded_token (token);
  else
    {
      print_token (token, gbuf);

      if (ifdef_helper.valid)
	ifdef_helper.end_offset = token->file_offset + gbuf->length;
      else if (strcmp (dyn_string_buf (gbuf), "ifdef") == 0
	       || strcmp (dyn_string_buf (gbuf), "ifndef") == 0
	       || strcmp (dyn_string_buf (gbuf), "if") == 0
	       || strcmp (dyn_string_buf (gbuf), "else") == 0
	       || strcmp (dyn_string_buf (gbuf), "elif") == 0
	       || strcmp (dyn_string_buf (gbuf), "endif") == 0)
	{
	  ifdef_helper.valid = true;
	  ifdef_helper.start_offset = token->file_offset;
	  ifdef_append (pfile->state.skipping == 1 ? SKIPPED : NO_SKIPPED,
			ifdef_helper.start_offset);
	  if (strcmp (dyn_string_buf (gbuf), "else") == 0
	      || strcmp (dyn_string_buf (gbuf), "endif") == 0)
	    ifdef_helper.end_offset = token->file_offset + gbuf->length;
	}

      if (pfile->state.skipping == 1)
	return;

      if (define_helper.macro_tristate == 1
	  && strcmp (dyn_string_buf (gbuf), "define") == 0)
	{
	  define_helper.macro_tristate = 2;
	  return;
	}
      else if (define_helper.macro_tristate == 2)
	{
	  def_append (DEF_MACRO, gbuf, token->file_offset);
	  define_helper.macro_tristate = 0;
	}
    }
}

static void
cb_start_directive (cpp_reader * pfile)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  define_helper.macro_tristate = 1;
  ifdef_helper.start_offset = ifdef_helper.end_offset = -1;
  ifdef_helper.valid = false;
  cb->lex_token = cb_lex_token;
  /* We don't care about macro expansion in directive, it's reset back in
   * cb_end_directive. */
  cb->macro_start_expand = NULL;
  cb->macro_end_expand = NULL;
}

static void cb_macro_start (cpp_reader *, cpp_token_p, const cpp_hashnode *);
static void cb_macro_end (cpp_reader *, bool);
static void
cb_end_directive (cpp_reader * pfile)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  cb->macro_start_expand = cb_macro_start;
  cb->macro_end_expand = cb_macro_end;
  cb->lex_token = NULL;
  if (ifdef_helper.valid)
    ifdef_append (CLAUSE_ITSELF, ifdef_helper.end_offset);
}

static void
cb_macro_start (cpp_reader * pfile, cpp_token_p token,
		const cpp_hashnode * node)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  gcc_assert (token->val.node.node == node);
  cb->lex_token = cb_lex_token;
  cb->macro_end_expand = cb_macro_end;
  if (pfile->context->prev == NULL && !mo_isvalid ())
    {
      bug_trap_token (token->file_offset);
      mo_enter (pfile, token);
    }
}

static void
cb_macro_end (cpp_reader * pfile, bool in_expansion)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  if (!in_expansion)
    {
      mo_leave ();
      cb->lex_token = NULL;
      cb->macro_end_expand = NULL;
    }
}

/* Here, cb_file_change isn't enought because it's fail to notice us when a
 * header which has a guard macro is included repeatedly.
 *     ---- a.h ----
 *     #ifndef A_H
 *     #define A_H
 *     ...
 *     #endif
 *     ---- a.c ----
 *     #include "a.h"
 *     #include "a.h"
 * So we need cb_direct_include which can record the lines to database.
 */
typedef void (*CB_FILE_CHANGE) (cpp_reader *, const struct line_map *);
static CB_FILE_CHANGE orig_file_change;
static void
cb_file_change (cpp_reader * pfile, const struct line_map *map)
{
  if (map != NULL)
    {
      if (map->reason == LC_ENTER && map->included_from != -1)
	file_push (map->to_file, map->sysp);
      else if (map->reason == LC_LEAVE)
	file_pop ();
    }
  if (orig_file_change != NULL)
    orig_file_change (pfile, map);
}

static void
cb_direct_include (int file_offset, const char *fname, bool sys)
{
  file_insert_filedep (file_offset, fname, sys);
}

/* }])> */

/* plugin callbacks <([{ */
/* The fold isn't class fold. */
/* The var is used to block some callbacks temporarily. */
static struct
{
  bool call_func;
  bool access_var;
  bool enum_spec;
} block_list =
{
.access_var = false,.call_func = false,.enum_spec = false};

/* in_pragma is used by symdb_cpp_token and symdb_c_token. */
static bool in_pragma = false;

/* For symdb_begin_expression and symdb_end_expression. */
static struct
{
  int token_offset;
  int nested_level;
    VEC (tree, heap) * gvar;
} expr =
{
.nested_level = 0};

static void
pcallbacks_init (void)
{
  expr.gvar = VEC_alloc (tree, heap, 10);
}

static void
pcallbacks_tini (void)
{
  VEC_free (tree, heap, expr.gvar);
}

static void plugin_tini (void *gcc_data, void *user_data);
static void
symdb_unit_init (void *gcc_data, void *user_data)
{
  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  orig_file_change = cb->file_change;
  cb->file_change = cb_file_change;
  /* The case is `echo "" | gcc -xc -'. */
  if (strcmp (main_input_filename, "") == 0)
    {
      plugin_tini (NULL, NULL);
      return;
    }

  db_error ((sqlite3_open_v2
	     (dyn_string_buf (control_panel.db_file), &db,
	      SQLITE_OPEN_READWRITE, NULL)));
  db_error ((sqlite3_exec
	     (db, "begin exclusive transaction;", NULL, 0, NULL)));

  gbuf = dyn_string_new (1024);
  pcallbacks_init ();
  control_panel_init (main_input_filename);
  offsetof_init ();
  ifdef_init ();
  file_init (main_input_filename);
  def_init ();
  fcallf_init ();
  faccessv_init ();
  funp_alias_init ();
  mo_init ();
}

static void
symdb_unit_tini (void *gcc_data, void *user_data)
{
  mo_tini ();
  funp_alias_tini ();
  faccessv_tini ();
  fcallf_tini ();
  def_tini ();
  file_tini ();
  ifdef_tini ();
  offsetof_tini ();
  control_panel_tini ();
  pcallbacks_tini ();
  dyn_string_delete (gbuf);

  db_error ((sqlite3_exec (db, "end transaction;", NULL, 0, NULL)));
  sqlite3_close (db);
}

static void
symdb_cpp_token (void *gcc_data, void *user_data)
{
  cpp_token_p token = (cpp_token_p) gcc_data;
  static cpp_token fake = {.type = CPP_STRING,.val.str.text =
      (unsigned char *) "",.val.str.len = 1
  };
  if (token == NULL)
    token = &fake;
  if (token->type == CPP_EOF)
    return;
  if (token->type == CPP_PRAGMA_EOL)
    {
      in_pragma = false;
      return;
    }
  if (token->type == CPP_PRAGMA || in_pragma)
    {
      in_pragma = true;
      return;
    }
  bug_trap_token (token->file_offset);
  if (mo_isvalid ())
    mo_append_macro_token (token);
}

/*
 * The plugin callback isn't used anymore.
 */
static void
symdb_c_token (void *gcc_data, void *user_data)
{
  // c_token_p token = gcc_data;
  if (in_pragma)
    return;
}

static bool
is_fun_p (tree node)
{
  tree type = TREE_TYPE (node);
  if (TREE_CODE (type) != POINTER_TYPE)
    return false;
  type = TREE_TYPE (type);
  if (TREE_CODE (type) != FUNCTION_TYPE)
    return false;
  return true;
}

static bool
var_is_mfp (tree var, tree * struct_name, tree * mfp)
{
  if (TREE_CODE (var) != COMPONENT_REF)
    return false;
  gcc_assert (TREE_OPERAND_LENGTH (var) == 3);
  *mfp = TREE_OPERAND (var, 1);
  if (!is_fun_p (*mfp))
    return false;
  gcc_assert (TREE_OPERAND (var, 2) == NULL);
  gcc_assert (TREE_CODE (*mfp) == FIELD_DECL);
  gcc_assert (TREE_CODE_CLASS (TREE_CODE (*mfp)) == tcc_declaration);
  *struct_name = TREE_TYPE (TREE_OPERAND (var, 0));
  return true;
}

static const char *
get_typename (tree type)
{
  const char *result = "";
  if (TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE)
    result = IDENTIFIER_POINTER (TYPE_NAME (type));
  return result;
}

static bool
is_anonymous_type (tree type)
{
  return (TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE)
    && TYPE_NAME (type) == NULL;
}

static void
loop_struct (tree field, int base)
{
  if (field == NULL)
    /* The struct has nothing. */
    return;
  do
    {
      gcc_assert (TREE_CODE (field) == FIELD_DECL);
      /* From __builtin_offsetof stack snapshot.
       *   size_binop_loc
       *   fold_offsetof_1
       *   fold_offsetof
       *   c_parser_postfix_expression
       * In brief, later is the result.
       */
      int offset = tree_low_cst (DECL_FIELD_OFFSET (field), 1) +
	tree_low_cst (DECL_FIELD_BIT_OFFSET (field),
		      1) / BITS_PER_UNIT + base;
      tree type = TREE_TYPE (field);

      const char *tmp = NULL;
      if (DECL_NAME (field) != NULL)
	{
	  tmp = IDENTIFIER_POINTER (DECL_NAME (field));
	  offsetof_commit (tmp, offset);
	}

      if ((TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE)
	  && !TYPE_FILE_SCOPE (type))
	{
	  /* Enumerate anonymous struct/union. */
	  if (tmp != NULL)
	    offsetof_push (tmp);
	  loop_struct (TYPE_FIELDS (type), offset);
	  if (tmp != NULL)
	    offsetof_pop ();
	}
    }
  while ((field = TREE_CHAIN (field)) != NULL);
}

static void
struct_offsetof (int structid, tree node)
{
  tree field;
  if (!offsetof_prepare (structid))
    return;

  /* From sizeof stack snapshot.
   *   size_binop_loc
   *   c_sizeof_or_alignof_type
   *   c_expr_sizeof_type
   *   c_parser_sizeof_expression
   * In brief, later is the result.
   */
  int size = tree_low_cst (TYPE_SIZE_UNIT (node), 1);
  offsetof_commit ("", size);

  switch (TREE_CODE (node))
    {
    case RECORD_TYPE:
    case UNION_TYPE:
      field = TYPE_FIELDS (node);
      loop_struct (field, 0);
      break;
    default:
      break;
    }
}

/*
 * postfix-expression:
 *   primary-expression
 *   postfix-expression ( argument-expression-list[opt] )
 *
 * primary-expression:
 *   identifier
 *   (expression) << a function pointer is called.
 */
static void
symdb_call_func (void *gcc_data, void *user_data)
{
  void **pair = (void **) gcc_data;
  tree decl = (tree) pair[0], struct_name = NULL, mfp;
  int file_offset = (int) pair[1];
  bool is_mfp = false;
  if (block_list.call_func)
    return;
  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      if (DECL_BUILT_IN (decl))
	goto done;
    }
  else
    {
      while (TREE_CODE (decl) == INDIRECT_REF)
	{
	  gcc_assert (TREE_OPERAND_LENGTH (decl) == 1);
	  decl = TREE_OPERAND (decl, 0);
	}
      if (var_is_mfp (decl, &struct_name, &mfp))
	is_mfp = true;
      else
	goto done;
    }
  if (is_mfp)
    {
      if (struct_name != NULL)
	dyn_string_copy_cstr (gbuf, get_typename (struct_name));
      else
	dyn_string_copy_cstr (gbuf, "");
      dyn_string_append_cstr (gbuf, "::");
      dyn_string_append_cstr (gbuf, IDENTIFIER_POINTER (DECL_NAME (mfp)));
    }
  else
    dyn_string_copy_cstr (gbuf, IDENTIFIER_POINTER (DECL_NAME (decl)));
  fcallf_insert (gbuf, file_offset);

done:;
}

/*
 * enumerator-list:
 *   enumerator
 *   enumerator-list , enumerator
 * enumerator:
 *   enumeration-constant
 *   enumeration-constant = constant-expression
 */
static void
symdb_enumerator (void *gcc_data, void *user_data)
{
  tree enum_decl = (tree) gcc_data;

  if (block_list.enum_spec)
    return;

  tree tmp = TREE_PURPOSE (enum_decl);
  gcc_assert (TREE_CODE (tmp) == CONST_DECL);
  dyn_string_copy_cstr (gbuf, IDENTIFIER_POINTER (DECL_NAME (tmp)));
  def_append (DEF_ENUM_MEMBER, gbuf, DECL_FILE_OFFSET (tmp));
}

/*
 * function-definition:
 *   declaration-specifiers[opt] declarator compound-statement
 * declarator:
 *   pointer[opt] direct-declarator
 * direct-declarator:
 *   direct-declarator ( parameter-type-list )
 *   ( attributes[opt] declarator )
 * parameter-type-list:
 *   parameter-list
 *   parameter-list , ...
 * parameter-list:
 *   parameter-declaration
 *   parameter-list , parameter-declaration
 * parameter-declaration:
 *   declaration-specifiers declarator attributes[opt]
 */
static void
symdb_extern_func (void *gcc_data, void *user_data)
{
  const struct c_declarator *da = (const struct c_declarator *) gcc_data;
  for (; da->kind != cdk_id; da = da->declarator);
  gcc_assert (da->declarator == NULL);

  dyn_string_copy_cstr (gbuf, IDENTIFIER_POINTER (da->u.id));
  def_append (DEF_FUNC, gbuf, da->file_offset);

  block_list.enum_spec = true;
  block_list.call_func = false;
  block_list.access_var = false;
}

/*
* declarator:
*   pointer[opt] direct-declarator
* direct-declarator:
*   identifier
*   ( attributes[opt] declarator )
*   direct-declarator array-declarator
*   direct-declarator ( parameter-type-list )
*   direct-declarator ( identifier-list[opt] ) << function call.
* parameter-type-list:
*   parameter-list
*   parameter-list , ...
* parameter-list:
*   parameter-declaration
*   parameter-list , parameter-declaration
* parameter-declaration:
*   declaration-specifiers declarator attributes[opt]
*   declaration-specifiers abstract-declarator[opt] attributes[opt]
* identifier-list:
*   identifier
*   identifier-list , identifier
*
* Our callback hook makes sure there's not function-call case at all. And we
* also don't care about function-declaration. So 
*   direct-declarator ( parameter-type-list )
* only are available to function pointer.
*/
static void
symdb_extern_var (void *gcc_data, void *user_data)
{
  void **pair = (void **) gcc_data;
  const struct c_declspecs *ds = pair[0];
  const struct c_declarator *da = pair[1];
  enum definition_flag df = DEF_VAR;

  if (ds->storage_class == csc_extern)
    /* User needn't the kind of definition at all. */
    goto done;
  else if (ds->storage_class == csc_typedef)
    df = DEF_TYPEDEF;

  for (; da->declarator != NULL; da = da->declarator)
    {
      if (da->kind == cdk_function)
	{
	  if (da->declarator->kind != cdk_pointer && df != DEF_TYPEDEF)
	    // Just function declaration.
	    goto done;
	}

      if (da->kind == cdk_id)
	break;
    }

  dyn_string_copy_cstr (gbuf, IDENTIFIER_POINTER (da->u.id));
  int id = def_append (df, gbuf, da->file_offset);

  if (is_anonymous_type (ds->type))
    struct_offsetof (id, ds->type);

done:;
}

/*
 * declaration-specifiers:
 *   storage-class-specifier declaration-specifiers[opt]
 *   type-specifier declaration-specifiers[opt]
 *   type-qualifier declaration-specifiers[opt]
 *   function-specifier declaration-specifiers[opt]
 *
 * type-specifier:
 *   typeof-specifier:
 *   struct-or-union-specifier:
 *   enum-specifier:
 *
 * typeof-specifier:
 *   typeof ( expression )
 *   typeof ( type-name )
 *
 * struct-or-union-specifier:
 *   struct-or-union attributes[opt] identifier[opt] { struct-contents } attributes[opt]
 *   struct-or-union attributes[opt] identifier
 *
 * enum-specifier:
 *   enum attributes[opt] identifier[opt] { enumerator-list } attributes[opt]
 *   enum attributes[opt] identifier[opt] { enumerator-list , } attributes[opt]
 *   enum attributes[opt] identifier
 */
static void
symdb_declspecs (void *gcc_data, void *user_data)
{
  const struct c_declspecs *ds = (const struct c_declspecs *) gcc_data;
  enum definition_flag df;
  tree type = ds->type;

  if (TYPE_FILE_OFFSET (type) == -1)
    /* Not definition. */
    goto done;

  if (ds->typespec_kind == ctsk_typeof)
    goto done;

  switch (TREE_CODE (type))
    {
    case ENUMERAL_TYPE:
      df = DEF_ENUM;
      break;
    case RECORD_TYPE:
      df = DEF_STRUCT;
      break;
    case UNION_TYPE:
      df = DEF_UNION;
      break;
    default:
      goto done;
    }

  const char *str = get_typename (type);
  if (strcmp (str, "") != 0)
    {
      dyn_string_copy_cstr (gbuf, str);
      int id = def_append (df, gbuf, TYPE_FILE_OFFSET (type));
      struct_offsetof (id, type);
    }
  /* else is anonymous struct/union/enum. */

done:;
}

static void
symdb_extern_decl (void *gcc_data, void *user_data)
{
  block_list.enum_spec = false;
  block_list.call_func = true;
  block_list.access_var = true;
}

static void
constructor_loop (tree node, int offset)
{
  tree struct_name = TREE_TYPE (node);
  if (TREE_CODE (struct_name) != RECORD_TYPE
      && TREE_CODE (struct_name) != UNION_TYPE)
    {
      gcc_assert (TREE_CODE (struct_name) == ARRAY_TYPE);
      return;
    }

  int cnt;
  tree index, value;
  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (node), cnt, index, value)
  {
    gcc_assert (TREE_CODE (index) == FIELD_DECL);
    if (TREE_CODE (value) == NOP_EXPR)
      {
	if (!is_fun_p (value))
	  continue;
	gcc_assert (TREE_OPERAND_LENGTH (value) == 1);
	value = TREE_OPERAND (value, 0);
      }
    if (TREE_CODE (value) == ADDR_EXPR)
      {
	if (!is_fun_p (value))
	  continue;
	gcc_assert (TREE_OPERAND_LENGTH (value) == 1);
	tree arg0 = TREE_OPERAND (value, 0);
	if (TREE_CODE (arg0) != FUNCTION_DECL)
	  continue;
	gcc_assert (TREE_CODE_CLASS (TREE_CODE (arg0)) == tcc_declaration);
	gcc_assert (TREE_CODE_CLASS (TREE_CODE (index)) == tcc_declaration);
	const char *a = IDENTIFIER_POINTER (DECL_NAME (index));
	const char *b = IDENTIFIER_POINTER (DECL_NAME (arg0));
	const char *c = get_typename (struct_name);
	funp_alias_append (c, a, b, offset);
      }
    else if (TREE_CODE (value) == CONSTRUCTOR)
      constructor_loop (value, offset);
  }
}

static void
modify_expr (tree node, int offset)
{
  if (!is_fun_p (node))
    return;
  gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
  tree struct_name, mfp = NULL;
  if (!var_is_mfp (TREE_OPERAND (node, 0), &struct_name, &mfp))
    return;
  tree tmp = TREE_OPERAND (node, 1);
  if (TREE_CODE (tmp) == NOP_EXPR)
    {
      gcc_assert (TREE_OPERAND_LENGTH (tmp) == 1);
      tmp = TREE_OPERAND (tmp, 0);
    }
  if (TREE_CODE (tmp) != ADDR_EXPR)
    return;
  gcc_assert (TREE_OPERAND_LENGTH (tmp) == 1);
  tree funcdecl = TREE_OPERAND (tmp, 0);
  if (TREE_CODE (funcdecl) != FUNCTION_DECL)
    return;
  gcc_assert (TREE_CODE_CLASS (TREE_CODE (funcdecl)) == tcc_declaration);
  const char *a = IDENTIFIER_POINTER (DECL_NAME (mfp));
  const char *b = IDENTIFIER_POINTER (DECL_NAME (funcdecl));
  const char *c = get_typename (struct_name);
  funp_alias_append (c, a, b, offset);
}

/*
 * The feature supports
 * 1) var.mfp = fun-decl # Note, not var.mfp = a-fun-pointer.
 * 2) var = { initializer-list } # as above, assign a mfp with fun-decl.
 *
 * 1)
 * expression:
 *   assignment-expression
 *
 * 2)
 * initializer:
 *   { initializer-list }
 *   { initializer-list , }
 * initializer-list:
 *   designation[opt] initializer
 *   initializer-list , designation[opt] initializer
 * designation:
 *   designator-list = 
 * designator-list:
 *   designator
 *   designator-list designator
 * designator:
 *   . identifier
 *
 * And you mfp can't be 2-level function pointer or array.
 */
static void
symdb_funp_alias (void *gcc_data, void *user_data)
{
  void **pair = (void **) gcc_data;
  tree node = (tree) pair[0];
  int file_offset = (int) pair[1];
  switch (TREE_CODE (node))
    {
    case CONSTRUCTOR:
      constructor_loop (node, file_offset);
      break;
    case MODIFY_EXPR:
      modify_expr (node, file_offset);
      break;
    default:
      break;
    }
}

static void loop_expr (tree);
static int
print_gvar (tree node, bool * indirect)
{
  tree tmp = node;
  int ret, code, pos = VEC_length (tree, expr.gvar);
  bool print = false;

  *indirect = false;
  while (TREE_CODE (tmp) == INDIRECT_REF)
    {
      tmp = TREE_OPERAND (tmp, 0);
      *indirect = true;
    }
  if (*indirect && TREE_CODE (tmp) == POINTER_PLUS_EXPR)
    {
      ret = 0;
      goto nofound;
    }

  code = TREE_CODE (tmp);
  switch (TREE_CODE_CLASS (code))
    {
    case tcc_expression:
    case tcc_comparison:
    case tcc_unary:
    case tcc_binary:
    case tcc_vl_exp:
      ret = 0;
      goto nofound;
    case tcc_declaration:
    case tcc_reference:
    case tcc_constant:
      break;
    default:
      gcc_assert (false);
    }

  // Iterator a leaf node, some expressions can occur in it.
  while (true)
    {
      switch (TREE_CODE (tmp))
	{
	case VAR_DECL:
	  if (DECL_CONTEXT (tmp))
	    {			// local variable.
	      ret = -1;
	      goto nofound;
	    }
	  VEC_safe_push (tree, heap, expr.gvar, tmp);
	  goto found;
	case INDIRECT_REF:
	  VEC_safe_push (tree, heap, expr.gvar, tmp);
	  tmp = TREE_OPERAND (tmp, 0);
	  code = TREE_CODE (tmp);
	  switch (code)
	    {
	    case POINTER_PLUS_EXPR:	/* astrut.p[i] */
	      loop_expr (TREE_OPERAND (tmp, 1));
	      tmp = TREE_OPERAND (tmp, 0);
	      break;
	    case CALL_EXPR:
	    case COND_EXPR:
	      ret = -16;
	      goto nofound;
	    default:
	      break;
	    }
	  break;
	case ARRAY_REF:
	case COMPONENT_REF:
	  VEC_safe_push (tree, heap, expr.gvar, tmp);
	  tmp = TREE_OPERAND (tmp, 0);
	  break;
	case PARM_DECL:
	  ret = -2;
	  goto nofound;
	case FUNCTION_DECL:
	  ret = -3;
	  goto nofound;
	case INTEGER_CST:
	case REAL_CST:
	case FIXED_CST:
	case COMPLEX_CST:
	case VECTOR_CST:
	case STRING_CST:
	  ret = -4;
	  goto nofound;
	case COMPOUND_EXPR:
	  loop_expr (TREE_OPERAND (tmp, 0));
	  tmp = TREE_OPERAND (tmp, 1);
	  break;
	case MODIFY_EXPR:
	  loop_expr (TREE_OPERAND (tmp, 1));
	  tmp = TREE_OPERAND (tmp, 0);
	  break;
	case NOP_EXPR:
	case CONVERT_EXPR:
	  loop_expr (TREE_OPERAND (tmp, 0));
	  ret = -16;
	  goto nofound;
	case BIT_FIELD_REF:
	  ret = -16;
	  goto nofound;
	default:
	  gcc_assert (false);
	}
    }

found:
  while (VEC_length (tree, expr.gvar) != pos)
    {
      tmp = VEC_pop (tree, expr.gvar);
      switch (TREE_CODE (tmp))
	{
	case VAR_DECL:
	  dyn_string_copy_cstr (gbuf, IDENTIFIER_POINTER (DECL_NAME (tmp)));
	  break;
	case INDIRECT_REF:
	  print = true;
	  break;
	case ARRAY_REF:
	  dyn_string_append_cstr (gbuf, "[]");
	  break;
	case COMPONENT_REF:
	  {
	    gcc_assert (TREE_OPERAND_LENGTH (tmp) == 3);
	    gcc_assert (TREE_OPERAND (tmp, 2) == NULL);
	    tree arg1 = TREE_OPERAND (tmp, 1);
	    gcc_assert (TREE_CODE (arg1) == FIELD_DECL);
	    if (print)
	      {
		print = false;
		dyn_string_append_cstr (gbuf, "->");
	      }
	    else
	      dyn_string_append_cstr (gbuf, ".");
	    dyn_string_append_cstr (gbuf,
				    IDENTIFIER_POINTER (DECL_NAME (arg1)));
	  }
	  break;
	default:
	  break;
	}
    }
  ret = 1;
  goto done;

nofound:
  while (VEC_length (tree, expr.gvar) != pos)
    VEC_pop (tree, expr.gvar);

done:
  return ret;
}

static void
do_lhs (tree node)
{
  tree tmp = node;
  bool indirect;
  int ret = print_gvar (tmp, &indirect);
  if (ret == 1)
    faccessv_insert (gbuf,
		     indirect ? ACCESS_POINTER_WRITE : ACCESS_WRITE,
		     expr.token_offset);
  else if (ret == 0)
    loop_expr (tmp);
}

static void
loop_expr (tree node)
{
  tree tmp = node, arg0, arg1, arg2;
  call_expr_arg_iterator iter;
  bool indirect;
  int ret = print_gvar (tmp, &indirect);
  if (ret == 1)
    {
      faccessv_insert (gbuf,
		       indirect ? ACCESS_POINTER_READ : ACCESS_READ,
		       expr.token_offset);
      return;
    }
  else if (ret < 0)
    return;

  switch (TREE_CODE (node))
    {
    case MODIFY_EXPR:		/* =, +=, >>= and so on */
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg0 = TREE_OPERAND (node, 0);
      do_lhs (arg0);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      break;
    case ADDR_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 1);
      arg0 = TREE_OPERAND (node, 0);
      ret = print_gvar (arg0, &indirect);
      if (ret == 0)
	loop_expr (arg0);
      else if (ret == 1)
	faccessv_insert (gbuf, ACCESS_ADDR, expr.token_offset);
      break;
    case COND_EXPR:		/* .. ? .. : .. */
      gcc_assert (TREE_OPERAND_LENGTH (node) == 3);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      arg2 = TREE_OPERAND (node, 2);
      loop_expr (arg2);
      break;
    case POINTER_PLUS_EXPR:	/* *(p + i) or p[i] */
    case COMPOUND_EXPR:	/* i, j */
      ;
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      break;
    case POSTINCREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case PREDECREMENT_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg0 = TREE_OPERAND (node, 0);
      ret = print_gvar (arg0, &indirect);
      if (ret == 0)
	loop_expr (arg0);
      else if (ret == 1)
	faccessv_insert (gbuf,
			 indirect ? ACCESS_POINTER_READ | ACCESS_POINTER_WRITE
			 : ACCESS_READ | ACCESS_WRITE, expr.token_offset);
      break;
    case INDIRECT_REF:		/* p->i */
    case NEGATE_EXPR:		/* -i */
    case FLOAT_EXPR:		/* f = i */
    case FIX_TRUNC_EXPR:	/* i = f */
    case VA_ARG_EXPR:
      ;
    case CONVERT_EXPR:		/* cast: (struct abc*) i */
    case NOP_EXPR:		/* cast: (struct abc*) p */
      ;
    case BIT_NOT_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 1);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      break;
    case EXACT_DIV_EXPR:	/* int *p, *q; p - q */
      ;
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case BIT_IOR_EXPR:		/* a | b */
    case BIT_AND_EXPR:
    case BIT_XOR_EXPR:
      ;
    case TRUTH_ANDIF_EXPR:	/* a && b */
    case TRUTH_ORIF_EXPR:	/* a || b */
      ;
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case RDIV_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      break;
    case CALL_EXPR:
      gcc_assert (CALL_EXPR_FN (node) != NULL);
      gcc_assert (CALL_EXPR_STATIC_CHAIN (node) == NULL);
      FOR_EACH_CALL_EXPR_ARG (tmp, iter, node) loop_expr (tmp);
      break;
      /* Some inner codes */
    case MAX_EXPR:
    case MIN_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      break;
    case SAVE_EXPR:
    case NON_LVALUE_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 1);
      arg0 = TREE_OPERAND (node, 0);
      loop_expr (arg0);
      break;
    case C_MAYBE_CONST_EXPR:
      gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
      arg1 = TREE_OPERAND (node, 1);
      loop_expr (arg1);
      break;
    default:
      gcc_assert (false);
    }
}

static void
symdb_begin_expression (void *gcc_data, void *user_data)
{
  if (block_list.access_var)
    return;
  if (expr.nested_level++ == 0)
    expr.token_offset = (int) gcc_data;
}

static void
symdb_end_expression (void *gcc_data, void *user_data)
{
  if (block_list.access_var)
    return;
  tree node = (tree) gcc_data;
  if (--expr.nested_level != 0)
    return;
  gcc_assert (VEC_length (tree, expr.gvar) == 0);
  loop_expr (node);
  void *pair[2];
  pair[0] = node;
  pair[1] = (void *) expr.token_offset;
  symdb_funp_alias (pair, NULL);
}

static void
set_func (const char *fname, tree body)
{
  tree arg0, arg1;
  bool indirect;
  if (TREE_CODE (body) != MODIFY_EXPR)
    return;
  arg0 = TREE_OPERAND (body, 0);
  arg1 = TREE_OPERAND (body, 1);
  if (TREE_CODE (arg1) != PARM_DECL)
    return;
  if (print_gvar (arg0, &indirect) == 1)
    faccessv_insert_fpattern (gbuf,
			      indirect ? ACCESS_POINTER_WRITE : ACCESS_WRITE);
}

static void
get_func (const char *fname, tree body)
{
  tree tmp, arg0, arg1;
  bool indirect;
  if (TREE_CODE (body) != RETURN_EXPR)
    return;
  tmp = TREE_OPERAND (body, 0);
  if (TREE_CODE (tmp) != MODIFY_EXPR)
    return;
  arg0 = TREE_OPERAND (tmp, 0);
  if (TREE_CODE (arg0) != RESULT_DECL)
    return;
  arg1 = TREE_OPERAND (tmp, 1);
  int code = TREE_CODE (arg1);
  if (code == ADDR_EXPR)
    {
      arg0 = TREE_OPERAND (arg1, 0);
      if (print_gvar (arg0, &indirect) == 1)
	faccessv_insert_fpattern (gbuf, ACCESS_ADDR);
    }
  else
    {
      if (print_gvar (arg1, &indirect) == 1)
	faccessv_insert_fpattern (gbuf,
				  indirect ? ACCESS_POINTER_READ :
				  ACCESS_READ);
    }
}

static void
symdb_fnbody_pattern (void *gcc_data, void *user_data)
{
  tree node = (tree) gcc_data, tmp;
  gcc_assert (TREE_CODE (node) == BIND_EXPR);
  tree vars = TREE_OPERAND (node, 0);
  if (vars != NULL)
    /* You have declared a local var. */
    return;
  tree body = TREE_OPERAND (node, 1);
  tree block = TREE_OPERAND (node, 2);

  tmp = BLOCK_SUPERCONTEXT (block);
  gcc_assert (TREE_CODE (tmp) == FUNCTION_DECL);
  tree args = DECL_ARGUMENT_FLD (tmp);
  tree result = DECL_RESULT_FLD (tmp);
  gcc_assert (TREE_CODE (result) == RESULT_DECL);
  if (TREE_CODE (TREE_TYPE (result)) == VOID_TYPE)
    {
      if (args != NULL && TREE_CHAIN (args) == NULL)
	/* Only a parameter is accepted. */
	set_func (IDENTIFIER_POINTER (DECL_NAME (tmp)), body);
    }
  else
    {
      if (args != NULL)
	return;
      get_func (IDENTIFIER_POINTER (DECL_NAME (tmp)), body);
    }
}

/* }])> */

int plugin_is_GPL_compatible;

static void
plugin_tini (void *gcc_data, void *user_data)
{
  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  cb->file_change = orig_file_change;
  cb->macro_start_expand = NULL;
  cb->macro_end_expand = NULL;
  cb->start_directive = NULL;
  cb->end_directive = NULL;

  unregister_callback ("symdb", PLUGIN_START_UNIT);
  unregister_callback ("symdb", PLUGIN_FINISH_UNIT);
  unregister_callback ("symdb", PLUGIN_FINISH);
  unregister_callback ("symdb", PLUGIN_CPP_TOKEN);
  unregister_callback ("symdb", PLUGIN_C_TOKEN);
  unregister_callback ("symdb", PLUGIN_EXTERN_DECL);
  unregister_callback ("symdb", PLUGIN_CALL_FUNCTION);
  unregister_callback ("symdb", PLUGIN_ENUMERATOR);
  unregister_callback ("symdb", PLUGIN_EXTERN_FUNC);
  unregister_callback ("symdb", PLUGIN_EXTERN_VAR);
  unregister_callback ("symdb", PLUGIN_EXTERN_DECLSPECS);
  unregister_callback ("symdb", PLUGIN_EXTERN_INITIALIZER);
  unregister_callback ("symdb", PLUGIN_BEGIN_EXPRESSION);
  unregister_callback ("symdb", PLUGIN_END_EXPRESSION);
  unregister_callback ("symdb", PLUGIN_FNBODY_PATTERN);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  /* When `-E' is passed, symdb_unit_init is skipped. */
  if (flag_preprocess_only)
    {
      fprintf (stderr,
	       "`-E' or `-save-temps' aren't coexisted with symdb.so.\n");
      fprintf (stderr,
	       "And such like `gcc x.S' which calls those parameters implicitly.\n");
      return 0;
    }
  /* We only accept a param -- `dbfile', using ProjectOverview table of
   * database to do more configs. */
  gcc_assert (plugin_info->argc == 1
	      && strcmp (plugin_info->argv[0].key, "dbfile") == 0);
  /* Due to gcc internal architecture, control_panel.db_file is initialized
   * here. */
  control_panel.db_file = dyn_string_new (PATH_MAX + 1);
  dyn_string_copy_cstr (control_panel.db_file, plugin_info->argv[0].value);

  register_callback ("symdb", PLUGIN_START_UNIT, &symdb_unit_init, NULL);
  register_callback ("symdb", PLUGIN_FINISH_UNIT, &symdb_unit_tini, NULL);
  register_callback ("symdb", PLUGIN_FINISH, &plugin_tini, NULL);
  register_callback ("symdb", PLUGIN_CPP_TOKEN, &symdb_cpp_token, NULL);
  register_callback ("symdb", PLUGIN_C_TOKEN, &symdb_c_token, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_DECL, &symdb_extern_decl, NULL);
  register_callback ("symdb", PLUGIN_CALL_FUNCTION, &symdb_call_func, NULL);
  register_callback ("symdb", PLUGIN_ENUMERATOR, &symdb_enumerator, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_FUNC, &symdb_extern_func, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_VAR, &symdb_extern_var, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_DECLSPECS,
		     &symdb_declspecs, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_INITIALIZER, &symdb_funp_alias,
		     NULL);
  register_callback ("symdb", PLUGIN_BEGIN_EXPRESSION,
		     &symdb_begin_expression, NULL);
  register_callback ("symdb", PLUGIN_END_EXPRESSION, &symdb_end_expression,
		     NULL);
  register_callback ("symdb", PLUGIN_FNBODY_PATTERN, &symdb_fnbody_pattern,
		     NULL);

  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  cb->macro_start_expand = cb_macro_start;
  cb->macro_end_expand = cb_macro_end;
  cb->start_directive = cb_start_directive;
  cb->end_directive = cb_end_directive;
  cb->direct_include = cb_direct_include;
  /* cb->lex_token is loaded/unloaded dynamically in my plugin. */
  /* Note: cb->file_change callback is delayed to install in symdb_unit_init
   * for there's an inner hook -- cb_file_change of gcc/c-family/c-opts.c. */
  return 0;
}
