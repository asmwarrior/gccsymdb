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
 *   6) Class file: is in charge of File fold and FileDefinition table of
 *   init.sql.
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
#include "input.h"
#include "dyn-string.h"
#include <sys/stat.h>
#include <time.h>
#include "libcpp/include/cpplib.h"
#include "libcpp/internal.h"
#include <sqlite3.h>
#include "gcc/c-parser.c"
/* }])> */

/* common <([{ */
typedef const struct cpp_token *cpp_token_p;
typedef const struct c_token *c_token_p;

typedef struct
{
  long long start;
  union
  {
    long long end;
    long long length;
  };
} lpair;
DEF_VEC_O (lpair);
DEF_VEC_ALLOC_O (lpair, heap);

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
/* The class is in charge of file tables and FileDefinition table. */
typedef struct
{
  int id;
  dyn_string_t name;
} include_unit;
DEF_VEC_O (include_unit);
DEF_VEC_ALLOC_O (include_unit, heap);

static struct
{
  VEC (lpair, heap) * scopes;	/* for FileDefinition table. */
  VEC (include_unit, heap) * includee;

  struct sqlite3_stmt *select_chfile;
  struct sqlite3_stmt *insert_chfile;
  struct sqlite3_stmt *select_filedep;
  struct sqlite3_stmt *insert_filedep;
  struct sqlite3_stmt *select_filedef;
  struct sqlite3_stmt *insert_filedef;
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
file_get_fid_from_cache (const char *file_name)
{
  int ix;
  include_unit *p;
  FOR_EACH_VEC_ELT (include_unit, file.includee, ix, p)
  {
    if (strcmp (dyn_string_buf (p->name), file_name) == 0)
      return p->id;
  }
  gcc_assert (false);
}

static int
file_get_current_fid (void)
{
  return VEC_last (include_unit, file.includee)->id;
}

static void
insert_filedep (int hid)
{
  int previd = file_get_current_fid ();
  if (previd == hid)
    return;
  db_error (sqlite3_bind_int (file.select_filedep, 1, previd));
  db_error (sqlite3_bind_int (file.select_filedep, 2, hid));
  if (sqlite3_step (file.select_filedep) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (file.insert_filedep, 1, previd));
      db_error (sqlite3_bind_int (file.insert_filedep, 2, hid));
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
	  printf
	    ("Update single file is disabled "
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
file_insert_defid (long long defid)
{
  int fileid = file_get_current_fid ();
  db_error (sqlite3_bind_int (file.select_filedef, 1, fileid));
  db_error (sqlite3_bind_int64 (file.select_filedef, 2, defid));
  db_error (sqlite3_bind_int64 (file.select_filedef, 3, defid));
  if (sqlite3_step (file.select_filedef) != SQLITE_ROW)
    {
      lpair *s;
      if (VEC_length (lpair, file.scopes) != 0)
	{
	  s = VEC_last (lpair, file.scopes);
	  if (defid == s->end + 1)
	    {
	      s->end++;
	      goto done;
	    }
	}
      s = VEC_safe_push (lpair, heap, file.scopes, NULL);
      s->start = s->end = defid;
    }
done:
  revalidate_sql (file.select_filedef);
}

static void
flush_scopes (void)
{
  int ix, fileid = file_get_current_fid ();
  lpair *p;
  FOR_EACH_VEC_ELT_REVERSE (lpair, file.scopes, ix, p)
  {
    db_error (sqlite3_bind_int (file.insert_filedef, 1, fileid));
    db_error (sqlite3_bind_int64 (file.insert_filedef, 2, p->start));
    db_error (sqlite3_bind_int64 (file.insert_filedef, 3, p->end));
    execute_sql (file.insert_filedef);
  }
  VEC_truncate (lpair, file.scopes, 0);
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
  if (VEC_length (include_unit, file.includee) != 0)
    {
      flush_scopes ();
      insert_filedep (id);
    }
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
  flush_scopes ();
  include_unit *p = VEC_last (include_unit, file.includee);
  dyn_string_delete (p->name);
  VEC_pop (include_unit, file.includee);
}

static void
file_init (const char *main_file)
{
  file.scopes = VEC_alloc (lpair, heap, 10);
  file.includee = VEC_alloc (include_unit, heap, 10);

  db_error (sqlite3_prepare_v2 (db,
				"select id, mtime from chFile where name = ?;",
				-1, &file.select_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into chFile values (NULL, ?, ?, ?);",
				-1, &file.insert_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FileDependence where chFileID = ? and hID = ?;",
				-1, &file.select_filedep, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FileDependence values (?, ?);",
				-1, &file.insert_filedep, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from FileDefinition "
				"where fileID = ? and startDefID <= ? and endDefID >= ?;",
				-1, &file.select_filedef, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FileDefinition values (?, ?, ?);",
				-1, &file.insert_filedef, 0));

  file_push (main_file, false);
}

static void
file_tini (void)
{
  file_pop ();
  sqlite3_finalize (file.insert_filedef);
  sqlite3_finalize (file.select_filedef);
  sqlite3_finalize (file.insert_filedep);
  sqlite3_finalize (file.select_filedep);
  sqlite3_finalize (file.insert_chfile);
  sqlite3_finalize (file.select_chfile);

  VEC_free (include_unit, heap, file.includee);
  VEC_free (lpair, heap, file.scopes);
}

/* }])> */

/* macro <([{ */
static struct
{
  /* expanded_count field is updated for debug only. */
  int expanded_count;
  int macro_count;
  bool cancel;
  bool cascaded;
  bool valid;

  /* For Macro table of init.sql */
  int process;
  int letFileID;
  int letOffset;
  int defFileID;
  int defLine;
  int defColumn;
  dyn_string_t expandedTokens;
  dyn_string_t macroTokens;
  struct sqlite3_stmt *insert_macro;
} mo =
{
0, 0, false, false, false, false};

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

static int
mo_get_macro_count (void)
{
  return mo.macro_count;
}

static void
mo_substract_macro_count (int value)
{
  mo.macro_count -= value;
}

static void
mo_leave (void)
{
  if (mo.process == 2)
    {
      db_error (sqlite3_bind_int (mo.insert_macro, 1, mo.defFileID));
      db_error (sqlite3_bind_int (mo.insert_macro, 2, mo.defLine));
      db_error (sqlite3_bind_int (mo.insert_macro, 3, mo.defColumn));
      db_error (sqlite3_bind_text
		(mo.insert_macro, 4, dyn_string_buf (mo.expandedTokens),
		 dyn_string_length (mo.expandedTokens), SQLITE_STATIC));
      db_error (sqlite3_bind_text
		(mo.insert_macro, 5, dyn_string_buf (mo.macroTokens),
		 dyn_string_length (mo.macroTokens), SQLITE_STATIC));
      execute_sql (mo.insert_macro);
      dyn_string_copy_cstr (mo.expandedTokens, "");
      dyn_string_copy_cstr (mo.macroTokens, "");
      mo.process = 1;
    }
  mo.expanded_count = mo.macro_count = 0;
  mo.cancel = mo.cascaded = false;
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
      source_location sl = macro->line;
      const struct line_map *lm = linemap_lookup (pfile->line_table, sl);
      if (strcmp (lm->to_file, "<command-line>") != 0)
	{
	  mo.defFileID = file_get_fid_from_cache (lm->to_file);
	  mo.defLine = SOURCE_LINE (lm, sl);
	  mo.defColumn = SOURCE_COLUMN (lm, sl);
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

static bool
mo_maybe_cascaded (cpp_reader * pfile, bool func)
{
  cpp_context *context = pfile->context;
  gcc_assert (context->prev != NULL);
  if (!func)
    return false;
  do
    {
      /* I places a lots of assert to make sure gcc internal data is compatible
       * with my plugin. */
      if (context->macro == NULL)
	{
	  /* Three cases: 1) expand_args; 2) paste_all_tokens; 3)
	   * enter_macro_context handles pragma. However, only paste_all_tokens
	   * calls _cpp_push_token_context. */
	  if (context->direct_p)
	    {
	      /* And paste_all_tokens only push a token to the context. */
	      gcc_assert (FIRST (context).token == LAST (context).token);
	      continue;
	    }
	  return false;
	}
      cpp_macro *macro = context->macro->value.macro;
      if (!context->direct_p)
	{
	  gcc_assert (macro->paramc != 0);
	  /* replace_args. */
	  if (FIRST (context).ptoken + 1 == LAST (context).ptoken &&
	      *FIRST (context).ptoken == &pfile->avoid_paste)
	    /* The case is macro argument is the tail of macro content. */
	    continue;
	  if (FIRST (context).ptoken == LAST (context).ptoken)
	    continue;
	}
      else
	{
	  gcc_assert (macro->paramc == 0);
	  /* The macro isn't funlike. */
	  if (FIRST (context).token == LAST (context).token)
	    continue;
	}
      return false;
    }
  while ((context = context->prev) && context->prev != NULL);
  mo.cascaded = func;
  return true;
}

static bool
mo_cascaded (void)
{
  if (mo.cascaded)
    {
      mo.cascaded = false;
      return true;
    }
  return false;
}

static void
mo_maybe_cancel (bool cancel)
{
  mo.cancel = cancel;
}

static bool
mo_cancel (void)
{
  return mo.cancel;
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
				"insert into Macro values (NULL, NULL, ?, ?, ?, ?, ?);",
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

/* cache <([{ */
__extension__ enum symdb_flag
{
  CPP_TYPE_SHIFT = 0,
  CPP_TYPE_MASK = 0xff,
  C_TYPE_SHIFT = 8,
  C_TYPE_MASK = 0xffff00,
};

typedef struct
{
  enum symdb_flag flag;
  dyn_string_t value;
  int file_offset;
} db_token;
DEF_VEC_O (db_token);
DEF_VEC_ALLOC_O (db_token, heap);

typedef struct
{
  /* leader expanded token. */
  db_token leader;
  lpair scope;
} let;
DEF_VEC_O (let);
DEF_VEC_ALLOC_O (let, heap);

static struct
{
  VEC (db_token, heap) * itokens;
  VEC (let, heap) * auxiliary;
  db_token *last_cpp_token;
} cache;

/* debug purpose. */
static void __attribute__ ((used)) dump_cache (void)
{
  int ix;
  db_token *p;
  printf ("cache.itokens ----------------\n");
  FOR_EACH_VEC_ELT (db_token, cache.itokens, ix, p)
  {
    printf ("[%d]: %s, %d\n", ix, dyn_string_buf (p->value), p->file_offset);
  }
  printf ("cache.auxiliary ----------------\n");
  let *p2;
  FOR_EACH_VEC_ELT (let, cache.auxiliary, ix, p2)
  {
    printf ("[%d]: (%s:%d), %d, %d\n", ix, dyn_string_buf (p2->leader.value),
	    p2->leader.file_offset, (int) p2->scope.start,
	    (int) p2->scope.length);
  }
  printf ("\n");
}

static void
cache_init (void)
{
  cache.itokens = VEC_alloc (db_token, heap, 10);
  cache.auxiliary = VEC_alloc (let, heap, 10);
}

static void
vec_pop_front (void *vec, int reserve)
{
  int ix;
  if (vec == cache.itokens)
    {
      db_token *a, *b;
      if (VEC_length (db_token, cache.itokens) == reserve)
	return;
      /* Don't accept overlap case. */
      gcc_assert (VEC_length (db_token, cache.itokens) >= reserve * 2);
      for (ix = 0; ix < reserve; ix++)
	{
	  a = VEC_index (db_token, cache.itokens, ix);
	  dyn_string_delete (a->value);
	}
      for (ix = 0; ix < reserve; ix++)
	{
	  a = VEC_index (db_token, cache.itokens, ix);
	  b =
	    VEC_index (db_token, cache.itokens,
		       VEC_length (db_token, cache.itokens) - reserve + ix);
	  *a = *b;
	}
      for (ix = reserve; ix < VEC_length (db_token, cache.itokens) - reserve;
	   ix++)
	{
	  a = VEC_index (db_token, cache.itokens, ix);
	  dyn_string_delete (a->value);
	}
      VEC_truncate (db_token, cache.itokens, reserve);
    }
  else if (vec == cache.auxiliary)
    {
      let *a, *b;
      gcc_assert (VEC_length (let, cache.auxiliary) >= reserve);
      if (VEC_length (let, cache.auxiliary) == reserve)
	return;
      for (ix = 0; ix < reserve; ix++)
	{
	  a = VEC_index (let, cache.auxiliary, ix);
	  dyn_string_delete (a->leader.value);
	}
      for (ix = 0; ix < reserve; ix++)
	{
	  a = VEC_index (let, cache.auxiliary, ix);
	  b =
	    VEC_index (let, cache.auxiliary,
		       VEC_length (let, cache.auxiliary) - reserve + ix);
	  *a = *b;
	}
      for (ix = reserve; ix < VEC_length (let, cache.auxiliary) - reserve;
	   ix++)
	{
	  a = VEC_index (let, cache.auxiliary, ix);
	  dyn_string_delete (a->leader.value);
	}
      VEC_truncate (let, cache.auxiliary, reserve);
    }
}

// Release cache as soon as possible.
static void
cache_reset (int reserve)
{
  if (mo_isvalid ())
    {
      /* Deal with multiple definitions are in a macro internal. */
      let *p2 = VEC_last (let, cache.auxiliary);
      mo_substract_macro_count (VEC_length (db_token, cache.itokens) -
				reserve - p2->scope.start);
      p2->scope.start = 0;
      p2->scope.length = 0x1fffffff;
      vec_pop_front (cache.auxiliary, 1);
    }
  else
    vec_pop_front (cache.auxiliary, 0);
  vec_pop_front (cache.itokens, reserve);
}

static void
cache_tini (void)
{
  cache_reset (0);
  VEC_free (db_token, heap, cache.itokens);
  VEC_free (let, heap, cache.auxiliary);
}

static void
cache_tag_let (cpp_token_p token)
{
  let *p = VEC_safe_push (let, heap, cache.auxiliary, NULL);
  p->leader.value = dyn_string_new (32);
  p->scope.start = VEC_length (db_token, cache.itokens);
  /* Here, the length is initialized as 0x1fffffff for tag all following cache.itokens
   * belong to the macro. */
  p->scope.length = 0x1fffffff;
  p->leader.file_offset = token->file_offset;
  gcc_assert (token->type == CPP_NAME);
  p->leader.flag = CPP_NAME | C_TYPE_MASK;
  print_token (token, p->leader.value);
}

static void
cache_cancel_last_let (void)
{
  gcc_assert (VEC_length (let, cache.auxiliary) != 0);
  let *p = VEC_last (let, cache.auxiliary);
  dyn_string_delete (p->leader.value);
  VEC_pop (let, cache.auxiliary);
}

static void
cache_end_let (void)
{
  let *p = VEC_last (let, cache.auxiliary);
  p->scope.length = mo_get_macro_count ();
}

static void
cache_append_itoken_cpp_stage (cpp_token_p token)
{
  db_token *p = VEC_safe_push (db_token, heap, cache.itokens, NULL);
  p->value = dyn_string_new (32);
  p->flag = token->type | C_TYPE_MASK;
  p->file_offset = token->file_offset;
  print_token (token, p->value);
  cache.last_cpp_token = p;
}

static void
cache_append_itoken_c_stage (c_token_p token)
{
  int keyword = 0;
  if (token->type != CPP_EOF)
    {
      if (token->id_kind == C_ID_ID)
	{
	  /* It's a user-defined identifier. */
	  gcc_assert (TREE_CODE (token->value) == IDENTIFIER_NODE);
	  gcc_assert (token->type == CPP_NAME);
	}
      else
	{
	  if (token->id_kind == C_ID_NONE && token->keyword != RID_MAX)
	    {
	      /* The cpp token (CPP_NAME) is a C keyword. */
	      gcc_assert (token->type == CPP_KEYWORD);
	    }
	  else
	    /* token->type is the remain CPP_*. */
	    return;
	}
    }
  else
    return;
  if (token->type == CPP_KEYWORD)
    keyword = ~token->keyword;
  keyword = (keyword << C_TYPE_SHIFT) & C_TYPE_MASK;
  cache.last_cpp_token->flag ^= keyword;
}

static int
cache_get_itoken_size (void)
{
  return VEC_length (db_token, cache.itokens);
}

static inline int
revert_index (int index)
{
  return VEC_length (db_token, cache.itokens) - 1 - index;
}

static db_token *
cache_get (int index)
{
  return VEC_index (db_token, cache.itokens, revert_index (index));
}

static int
cache_record_itoken_position (void)
{
  return VEC_length (db_token, cache.itokens);
}

static db_token *
cache_itoken_to_chtoken (int index)
{
  db_token *result = NULL;
  int ix;
  let *p;
  int tmp = revert_index (index);
  FOR_EACH_VEC_ELT_REVERSE (let, cache.auxiliary, ix, p)
  {
    if (p->scope.start <= tmp && tmp < p->scope.start + p->scope.length)
      break;
  }
  if (ix != -1)
    result = &VEC_index (let, cache.auxiliary, ix)->leader;
  else
    result = cache_get (index);
  return result;
}

static int
cache_skip_match_pair (int index, char c)
{
  int recurse = 1;
  int result = revert_index (index) - 1;
  char *from, *to;
  switch (c)
    {
    case ')':
      from = "(";
      to = ")";
      break;
    case ']':
      from = "[";
      to = "]";
      break;
    case '}':
      from = "{";
      to = "}";
      break;
    }
  while (true)
    {
      db_token *p = VEC_index (db_token, cache.itokens, result);
      if (strcmp (dyn_string_buf (p->value), from) == 0)
	{
	  recurse--;
	  if (recurse == 0)
	    break;
	}
      if (strcmp (dyn_string_buf (p->value), to) == 0)
	recurse++;
      result--;
    }
  return VEC_length (db_token, cache.itokens) - 1 - (result - 1);
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
  DEF_CALLED_FUNC,
  DEF_CALLED_POINTER,
};

static struct
{
  long long caller_id;

  struct sqlite3_stmt *helper;
  struct sqlite3_stmt *insert_def;
  struct sqlite3_stmt *select_defrel;
  struct sqlite3_stmt *insert_defrel;
} def;

static long long
insert_def (enum definition_flag flag, dyn_string_t str, int offset)
{
  int fid = file_get_current_fid ();
  long long defid;
  db_error (sqlite3_bind_int (def.helper, 1, fid));
  db_error (sqlite3_bind_int (def.helper, 2, offset));
  db_error (sqlite3_bind_text
	    (def.helper, 3, dyn_string_buf (str),
	     dyn_string_length (str), SQLITE_STATIC));
  db_error (sqlite3_bind_int (def.helper, 4, flag));
  if (sqlite3_step (def.helper) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_text
		(def.insert_def, 1, dyn_string_buf (str),
		 dyn_string_length (str), SQLITE_STATIC));
      db_error (sqlite3_bind_int (def.insert_def, 2, flag));
      db_error (sqlite3_bind_int (def.insert_def, 3, offset));
      execute_sql (def.insert_def);
      defid = sqlite3_last_insert_rowid (db);
    }
  else
    defid = sqlite3_column_int64 (def.helper, 1);
  revalidate_sql (def.helper);
  return defid;
}

static void
insert_defrel (long long callee)
{
  long long caller = def.caller_id;
  db_error (sqlite3_bind_int64 (def.select_defrel, 1, caller));
  db_error (sqlite3_bind_int64 (def.select_defrel, 2, callee));
  if (sqlite3_step (def.select_defrel) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int64 (def.insert_defrel, 1, caller));
      db_error (sqlite3_bind_int64 (def.insert_defrel, 2, callee));
      execute_sql (def.insert_defrel);
    }
  revalidate_sql (def.select_defrel);
}

static void
def_append (enum definition_flag flag, dyn_string_t str, int offset)
{
  long long defid = insert_def (flag, str, offset);
  if (flag == DEF_FUNC)
    def.caller_id = defid;
  else if (flag == DEF_CALLED_FUNC || flag == DEF_CALLED_POINTER)
    insert_defrel (defid);
  file_insert_defid (defid);
}

static inline void
def_demangle_type (db_token * token, int *cpp_type, int *c_type)
{
  enum symdb_flag flag = token->flag;
  *cpp_type = flag & CPP_TYPE_MASK;
  *c_type = (flag & C_TYPE_MASK) >> C_TYPE_SHIFT;
}

static inline bool
def_is_uid (db_token * token)
{
  int cpp_type, c_type;
  def_demangle_type (token, &cpp_type, &c_type);
  return cpp_type == CPP_NAME && c_type == 0xffff;
}

static int
def_strip_paren (int index)
{
  while (true)
    {
      int cpp_type, c_type;
      db_token *tmp = cache_get (index++);
      def_demangle_type (tmp, &cpp_type, &c_type);
      if (cpp_type != CPP_CLOSE_PAREN)
	break;
    }
  return index - 1;
}

static void
def_append_chk (int index, enum definition_flag flag, const char *p)
{
  db_token *token;
  dyn_string_t str;
  token = cache_get (index);
  str = token->value;
  if (p != NULL)
    {
      dyn_string_copy_cstr (gbuf, p);
      dyn_string_append_cstr (gbuf, "::");
      dyn_string_append (gbuf, token->value);
    }
  else
    dyn_string_copy (gbuf, str);
  gcc_assert (def_is_uid (token));
  token = cache_itoken_to_chtoken (index);
  gcc_assert (def_is_uid (token));
  def_append (flag, gbuf, token->file_offset);
}

static void
def_init (void)
{
  /* Search fileDefinition view not Definition table. */
  db_error (sqlite3_prepare_v2 (db,
				"select defID from Helper "
				"where fileID = ? and fileoffset = ? and defName = ? and flag = ?;",
				-1, &def.helper, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into Definition values (NULL, ?, ?, ?);",
				-1, &def.insert_def, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select rowid from DefinitionRelationship where caller = ? and callee = ?;",
				-1, &def.select_defrel, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into DefinitionRelationship values (?, ?);",
				-1, &def.insert_defrel, 0));
}

static void
def_tini (void)
{
  sqlite3_finalize (def.insert_defrel);
  sqlite3_finalize (def.select_defrel);
  sqlite3_finalize (def.insert_def);
  sqlite3_finalize (def.helper);
}

/* }])> */

/* ifdef <([{ */
__extension__ enum ifdef_flag
{
  CLAUSE_ITSELF = 1,
  SKIPPED,
  NO_SKIPPED,
};

static struct
{
  VEC (lpair, heap) * units;

  struct sqlite3_stmt *select_ifdef;
  struct sqlite3_stmt *insert_ifdef;
} ifdef;

static void
ifdef_append (enum ifdef_flag flag, int offset)
{
  lpair *unit = VEC_last (lpair, ifdef.units);
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
  lpair *unit = VEC_safe_push (lpair, heap, ifdef.units, NULL);
  unit->start = 0;
  unit->end = -1;
}

static void
ifdef_pop (void)
{
  ifdef_append (NO_SKIPPED, 0x10000000);
  VEC_pop (lpair, ifdef.units);
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
  ifdef.units = VEC_alloc (lpair, heap, 10);
}

static void
ifdef_tini (void)
{
  VEC_free (lpair, heap, ifdef.units);
  sqlite3_finalize (ifdef.insert_ifdef);
  sqlite3_finalize (ifdef.select_ifdef);
}

/* }])> */

/* funp alias <([{ */
static struct
{
  struct sqlite3_stmt *select_funpalias;
  struct sqlite3_stmt *insert_funpalias;
} funp_alias;

void
funp_alias_append (const char *struct_name, const char *mem_name,
		   const char *fun_decl)
{
  // printf ("falias %s::%s = %s\n", struct_name, mem_name, fun_decl);
  int fileid = file_get_current_fid ();
  int offset = cache_itoken_to_chtoken (0)->file_offset;
  db_error (sqlite3_bind_int (funp_alias.select_funpalias, 1, fileid));
  db_error (sqlite3_bind_text
	    (funp_alias.select_funpalias, 2, struct_name, -1, SQLITE_STATIC));
  db_error (sqlite3_bind_text
	    (funp_alias.select_funpalias, 3, mem_name, -1, SQLITE_STATIC));
  db_error (sqlite3_bind_text
	    (funp_alias.select_funpalias, 4, fun_decl, -1, SQLITE_STATIC));
  db_error (sqlite3_bind_int (funp_alias.select_funpalias, 5, offset));
  if (sqlite3_step (funp_alias.select_funpalias) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (funp_alias.insert_funpalias, 1, fileid));
      db_error (sqlite3_bind_text
		(funp_alias.insert_funpalias, 2, struct_name, -1,
		 SQLITE_STATIC));
      db_error (sqlite3_bind_text
		(funp_alias.insert_funpalias, 3, mem_name, -1,
		 SQLITE_STATIC));
      db_error (sqlite3_bind_text
		(funp_alias.insert_funpalias, 4, fun_decl, -1,
		 SQLITE_STATIC));
      db_error (sqlite3_bind_int (funp_alias.insert_funpalias, 5, offset));
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
				" and structName = ? and member = ? and funDecl = ?"
				" and offset = ?;",
				-1, &funp_alias.select_funpalias, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FunpAlias values (?, ?, ?, ?, ?);",
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
static void cb_macro_end (cpp_reader *);
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
cb_end_arg (cpp_reader * pfile, bool cancel)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  mo_maybe_cancel (cancel);
  cb->macro_end_arg = NULL;
}

static void
cb_macro_start (cpp_reader * pfile, cpp_token_p token,
		const cpp_hashnode * node)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  gcc_assert (token->val.node.node == node);
  bool fun_like = false;
  if (!(node->flags & NODE_BUILTIN))
    fun_like = node->value.macro->fun_like;
  if (pfile->context->prev == NULL)
    {
      bug_trap_token (token->file_offset);
      mo_enter (pfile, token);
      cache_tag_let (token);
    reinit:
      if (fun_like)
	{
	  cb->macro_end_arg = cb_end_arg;
	  cb->lex_token = cb_lex_token;
	}
    }
  else
    {
      if (mo_maybe_cascaded (pfile, fun_like))
	goto reinit;
    }
}

static void
cb_macro_end (cpp_reader * pfile)
{
  if (pfile->context->prev == NULL)
    {
      if (mo_cascaded ())
	return;
      if (mo_cancel ())
	cache_cancel_last_let ();
      else
	cache_end_let ();
      mo_leave ();
    }
}

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

/* }])> */

/* plugin callbacks <([{ */
/* The fold isn't class fold. */
static struct
{
  bool call_func;
  bool enum_spec;
} block_list =
{
.call_func = false,.enum_spec = false};

/* in_pragma is used by symdb_cpp_token and symdb_c_token. */
static bool in_pragma = false;
/* func_old_param is used by symdb_extern_func_old_param and symdb_extern_func.
 * */
static int func_old_param = 0;

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
  control_panel_init (main_input_filename);
  cache_init ();
  ifdef_init ();
  file_init (main_input_filename);
  def_init ();
  funp_alias_init ();
  mo_init ();
}

static void
symdb_unit_tini (void *gcc_data, void *user_data)
{
  mo_tini ();
  funp_alias_tini ();
  def_tini ();
  file_tini ();
  ifdef_tini ();
  cache_tini ();
  control_panel_tini ();
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
  cache_append_itoken_cpp_stage (token);
  if (mo_isvalid ())
    mo_append_macro_token (token);
}

static void
symdb_c_token (void *gcc_data, void *user_data)
{
  c_token *token = gcc_data;
  if (in_pragma)
    return;
  cache_append_itoken_c_stage (token);
}

static bool
is_funcp (tree node)
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
var_is_struct_funcp (tree var, tree * type, tree * member)
{
  if (TREE_CODE (var) != COMPONENT_REF)
    return false;
  gcc_assert (TREE_OPERAND_LENGTH (var) == 3);
  *member = TREE_OPERAND (var, 1);
  if (!is_funcp (*member))
    return false;
  gcc_assert (TREE_OPERAND (var, 2) == NULL);
  gcc_assert (TREE_CODE (*member) == FIELD_DECL);
  gcc_assert (TREE_CODE_CLASS (TREE_CODE (*member)) == tcc_declaration);
  *type = TREE_TYPE (TREE_OPERAND (var, 0));
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
  tree decl = (tree) gcc_data, type = NULL, member;
  db_token *token;
  int cpp_type, c_type;
  int index;
  enum definition_flag df = DEF_CALLED_FUNC;
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
      if (var_is_struct_funcp (decl, &type, &member))
	df = DEF_CALLED_POINTER;
      else
	goto done;
    }
  token = cache_get (0);
  def_demangle_type (token, &cpp_type, &c_type);
  gcc_assert (cpp_type == CPP_OPEN_PAREN);
  // Strip inner parens.
  index = def_strip_paren (1);
  def_append_chk (index, df, type != NULL ? get_typename (type) : NULL);

done:
  cache_reset (0);
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
  if (block_list.enum_spec)
    return;
  def_append_chk (0, DEF_ENUM_MEMBER, NULL);
  /* Don't call cache_reset(0) here, since enum specifier hasn't been parsed,
   * see symdb_declspecs. */
}

static void
symdb_extern_func_old_param (void *gcc_data, void *user_data)
{
  db_token *token;
  int cpp_type, c_type;
  token = cache_get (1);
  def_demangle_type (token, &cpp_type, &c_type);
  gcc_assert (cpp_type == CPP_CLOSE_PAREN);
  func_old_param = cache_record_itoken_position ();
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
 *
 * We also includes outer/inner-paren cases, see symdb_extern_var.
 */
static void
symdb_extern_func (void *gcc_data, void *user_data)
{
  db_token *token;
  int cpp_type, c_type;
  int index;
  if (func_old_param != cache_record_itoken_position ())
    {
      /* We encounter old-style parameter declaration of function. */
      index = cache_record_itoken_position () - func_old_param + 1;
    }
  else
    {
      token = cache_get (0);
      def_demangle_type (token, &cpp_type, &c_type);
      gcc_assert (cpp_type == CPP_OPEN_BRACE);
      index = 1;
    }

  token = cache_get (index);
  def_demangle_type (token, &cpp_type, &c_type);
  gcc_assert (cpp_type == CPP_CLOSE_PAREN);
  // here are some cases (tokens in `' are just you've seen in class cache).
  //    1) ... int `(foo(int i __attribute__((unused)))) {' ...
  //    2) ... int `*(foo(int i __attribute__((unused)))) {' ...
  // which includes outer paren, parameter-type-list, variable attribute.
  if (cache_skip_match_pair (index, ')') == cache_get_itoken_size ())
    // case 1, adjust `index' to avoid overflow cache.itokens.
    index++;
  // strip outer parens.
  while (true)
    {
      int tmp;
      tmp = cache_skip_match_pair (index, ')');
      token = cache_get (tmp);
      def_demangle_type (token, &cpp_type, &c_type);
      if ((cpp_type == CPP_NAME && c_type == 0xffff) ||
	  cpp_type == CPP_CLOSE_PAREN)
	{
	  index = tmp;
	  break;
	}
      index++;
    }
  // strip inner parens.
  index = def_strip_paren (index);
  def_append_chk (index, DEF_FUNC, NULL);

  block_list.enum_spec = true;
  block_list.call_func = false;
  cache_reset (0);
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
*
* Case `( attributes[opt] declarator )' is called paren cases, see
* test/paren_declarator/a.c. There're three -- outer, middle and inner cases.
* `int *(*(*(*(funpvar))[3])(void));'
* function pointer includes all of them, from left to right.
* `int ((arr)[3][2]);'
* includes outer and inner cases. To simple, later code calls outer as middle.
* And the remain variable types and typedef include only inner cases.
*
* To function pointer
* 1) int (*fun[2])(void); correct.
* 2) int (*fun)(void)[2]; wrong.
* Which means `[' is closer than `(void)' to `fun'.
*
* Here is a trap to function pointer.
* int ((*fun[2])(int i __attribute__((unused))));
* which includes outer paren, parameter-type-list, variable attribute. So just
* stripping parens from backward to forwared is wrong.
*
* Syntax comes from c-parser.c:c_parser_declarator, but later case is also ok?
* char c __attribute__((unused));
* To the case, class cache snapshot is `c __attribute__'.
*/
static void
symdb_extern_var (void *gcc_data, void *user_data)
{
  void **pair = (void **) gcc_data;
  const struct c_declspecs *ds = pair[0];
  const struct c_declarator *da = pair[1];
  db_token *token;
  int cpp_type, c_type;
  int index = 1;
  int funp = 0;
  int td = 0;
  int arr = 0;

  if (ds->storage_class == csc_extern)
    /* User needn't the kind of definition at all. */
    goto done;
  else if (ds->storage_class == csc_typedef)
    td = 1;

  while (da->declarator != NULL)
    {
      if (da->kind == cdk_function)
	{
	  if (da->declarator->kind != cdk_pointer && td == 0)
	    // Just function declaration.
	    goto done;
	  funp = 1;
	}
      if (da->kind == cdk_array)
	arr = 1;
      da = da->declarator;
    }

  if (funp)
    {
      if (cache_skip_match_pair (index, ')') == cache_get_itoken_size ())
	// trap case, adjust `index' to avoid overflow cache.itokens.
	index++;
      // strip outer parens.
      while (true)
	{
	  int tmp;
	  tmp = cache_skip_match_pair (index, ')');
	  token = cache_get (tmp);
	  def_demangle_type (token, &cpp_type, &c_type);
	  if ((cpp_type == CPP_NAME && c_type == 0xffff) ||
	      cpp_type == CPP_CLOSE_PAREN)
	    {
	      index = tmp;
	      break;
	    }
	  index++;
	}
    }
  // strip middle parens.
  index = def_strip_paren (index);
  if (arr)
    {
      while (1)
	{
	  token = cache_get (index);
	  def_demangle_type (token, &cpp_type, &c_type);
	  if (cpp_type == CPP_CLOSE_SQUARE)
	    index = cache_skip_match_pair (index, ']');
	  else
	    break;
	}
    }
  // strip inner parens.
  index = def_strip_paren (index);
  def_append_chk (index, td ? DEF_TYPEDEF : DEF_VAR, NULL);

done:
  cache_reset (0);
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
  void **pair = (void **) gcc_data;
  const struct c_declspecs *ds = pair[0];
  // Here pair[1] is very important since to gcc intern
  //    int i;
  // In the function, class cache snapshot is `int i', so symdb_declspecs >>
  // cache_reset must make sure later symdb_extern_var will not crashed.
  int index = (int) pair[1];
  enum definition_flag flag;
  db_token *token;
  int cpp_type, c_type;

  if (ds->typespec_kind == ctsk_typeof)
    goto done;

  while (true)
    {
      token = cache_get (index);
      def_demangle_type (token, &cpp_type, &c_type);
      if (cpp_type != CPP_CLOSE_PAREN)
	break;
      index = cache_skip_match_pair (index, ')');
      token = cache_get (index);
      def_demangle_type (token, &cpp_type, &c_type);
      gcc_assert (cpp_type == CPP_NAME && c_type == RID_ATTRIBUTE);
      index++;
    }

  token = cache_get (index);
  def_demangle_type (token, &cpp_type, &c_type);
  if (cpp_type != CPP_CLOSE_BRACE)
    goto done;

  switch (TREE_CODE (ds->type))
    {
    case ENUMERAL_TYPE:
      flag = DEF_ENUM;
      break;
    case RECORD_TYPE:
      flag = DEF_STRUCT;
      break;
    case UNION_TYPE:
      flag = DEF_UNION;
      break;
    default:
      goto done;
    }

  index = cache_skip_match_pair (index, '}');
  token = cache_get (index);
  if (def_is_uid (token))
    def_append_chk (index, flag, NULL);
  /* else is anonymous struct/union/enum. */

done:
  cache_reset ((int) pair[1]);
}

static void
symdb_extern_decl (void *gcc_data, void *user_data)
{
  block_list.enum_spec = false;
  block_list.call_func = true;
}

static void
constructor_loop (tree node)
{
  tree type = TREE_TYPE (node);
  if (TREE_CODE (type) != RECORD_TYPE && TREE_CODE (type) != UNION_TYPE)
    {
      gcc_assert (TREE_CODE (type) == ARRAY_TYPE);
      return;
    }

  int cnt;
  tree index, value;
  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (node), cnt, index, value)
  {
    gcc_assert (TREE_CODE (index) == FIELD_DECL);
    if (TREE_CODE (value) == NOP_EXPR)
      {
	if (!is_funcp (value))
	  continue;
	gcc_assert (TREE_OPERAND_LENGTH (value) == 1);
	value = TREE_OPERAND (value, 0);
      }
    if (TREE_CODE (value) == ADDR_EXPR)
      {
	if (!is_funcp (value))
	  continue;
	gcc_assert (TREE_OPERAND_LENGTH (value) == 1);
	tree arg0 = TREE_OPERAND (value, 0);
	if (TREE_CODE (arg0) != FUNCTION_DECL)
	  continue;
	gcc_assert (TREE_CODE_CLASS (TREE_CODE (arg0)) == tcc_declaration);
	gcc_assert (TREE_CODE_CLASS (TREE_CODE (index)) == tcc_declaration);
	const char *a = IDENTIFIER_POINTER (DECL_NAME (index));
	const char *b = IDENTIFIER_POINTER (DECL_NAME (arg0));
	const char *c = get_typename (type);
	funp_alias_append (c, a, b);
      }
    else if (TREE_CODE (value) == CONSTRUCTOR)
      constructor_loop (value);
  }
}

static void
modify_expr (tree node)
{
  if (!is_funcp (node))
    return;
  gcc_assert (TREE_OPERAND_LENGTH (node) == 2);
  tree type, member = NULL;
  if (!var_is_struct_funcp (TREE_OPERAND (node, 0), &type, &member))
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
  const char *a = IDENTIFIER_POINTER (DECL_NAME (member));
  const char *b = IDENTIFIER_POINTER (DECL_NAME (funcdecl));
  const char *c = get_typename (type);
  funp_alias_append (c, a, b);
}

/*
 * The feature supports
 * 1) var.member = fun-decl # Note, not var.member = a-fun-pointer.
 * 2) var = { initializer-list } # as above, assign a member with fun-decl.
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
 * And you member-function-pointer can't be 2-level function pointer or array.
 */
static void
symdb_funp_alias (void *gcc_data, void *user_data)
{
  tree node = (tree) gcc_data;
  switch (TREE_CODE (node))
    {
    case CONSTRUCTOR:
      constructor_loop (node);
      break;
    case MODIFY_EXPR:
      modify_expr (node);
      break;
    default:
      break;
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
  unregister_callback ("symdb", PLUGIN_EXTERN_FUNC_OLD_PARAM);
  unregister_callback ("symdb", PLUGIN_EXTERN_FUNC);
  unregister_callback ("symdb", PLUGIN_EXTERN_VAR);
  unregister_callback ("symdb", PLUGIN_EXTERN_DECLSPECS);
  unregister_callback ("symdb", PLUGIN_FUNP_ALIAS);
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  /* When `-E' is passed, symdb_unit_init is skipped. */
  if (flag_preprocess_only)
    {
      printf ("`-E' or `-save-temps' aren't supported by symdb.so.");
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
  register_callback ("symdb", PLUGIN_EXTERN_FUNC_OLD_PARAM,
		     &symdb_extern_func_old_param, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_FUNC, &symdb_extern_func, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_VAR, &symdb_extern_var, NULL);
  register_callback ("symdb", PLUGIN_EXTERN_DECLSPECS,
		     &symdb_declspecs, NULL);
  register_callback ("symdb", PLUGIN_FUNP_ALIAS, &symdb_funp_alias, NULL);

  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  cb->macro_start_expand = cb_macro_start;
  cb->macro_end_expand = cb_macro_end;
  cb->start_directive = cb_start_directive;
  cb->end_directive = cb_end_directive;
  /* Note: cb->file_change callback is delayed to install in symdb_unit_init
   * for there's an inner hook -- cb_file_change of gcc/c-family/c-opts.c. */
  return 0;
}
