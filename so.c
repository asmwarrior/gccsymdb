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
/* }])> */

/* common <([{ */
typedef const struct cpp_token *cpp_token_p;
typedef const struct c_token *c_token_p;

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
			       "select projectRootPath from ProjectOverview;",
			       &result, &nrow, &ncolumn, &error_msg));
  dyn_string_copy_cstr (control_panel.prj_dir, result[1]);
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
  long long start;
  long long end;
} scope;
DEF_VEC_O (scope);
DEF_VEC_ALLOC_O (scope, heap);

DEF_VEC_I (int);
DEF_VEC_ALLOC_I (int, heap);
static struct
{
  VEC (scope, heap) * scopes;	/* for FileDefinition table. */
  VEC (int, heap) * includee;

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
  int nrow, ncolumn;
  char *error_msg, **table;
  int ix, p;
  dyn_string_t str = dyn_string_new (32);
  FOR_EACH_VEC_ELT (int, file.includee, ix, p)
  {
    char tmp[16];
    sprintf (tmp, "%d", p);
    dyn_string_copy_cstr (str, "select name from chFile where id = ");
    dyn_string_append_cstr (str, tmp);
    dyn_string_append_cstr (str, ";");
    db_error (sqlite3_get_table (db, dyn_string_buf (str), &table,
				 &nrow, &ncolumn, &error_msg));
    gcc_assert (nrow == 1 && ncolumn == 1);
    printf ("%s:%s >> ", tmp, table[1]);
    sqlite3_free_table (table);
  }
  dyn_string_delete (str);
  printf ("\n");
}

static int
file_get_current_fid (void)
{
  return VEC_last (int, file.includee);
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
  if (new_mtime < *mtime)
    {
      printf ("Update single file isn't supported now.");
      gcc_assert (false);
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
      scope *s;
      if (VEC_length (scope, file.scopes) != 0)
	{
	  s = VEC_last (scope, file.scopes);
	  if (defid == s->end + 1)
	    {
	      s->end++;
	      goto done;
	    }
	}
      s = VEC_safe_push (scope, heap, file.scopes, NULL);
      s->start = s->end = defid;
    }
done:
  revalidate_sql (file.select_filedef);
}

static void
flush_scopes (void)
{
  int ix, fileid = file_get_current_fid ();
  scope *p;
  FOR_EACH_VEC_ELT_REVERSE (scope, file.scopes, ix, p)
  {
    db_error (sqlite3_bind_int (file.insert_filedef, 1, fileid));
    db_error (sqlite3_bind_int64 (file.insert_filedef, 2, p->start));
    db_error (sqlite3_bind_int64 (file.insert_filedef, 3, p->end));
    execute_sql (file.insert_filedef);
  }
  VEC_truncate (scope, file.scopes, 0);
}

static bool mo_isvalid (void);
static void
file_push (const char *f, bool sys)
{
  int id;
  long long mtime;
  gcc_assert (!mo_isvalid ());
  insert_file (f, &id, &mtime, sys);
  if (VEC_length (int, file.includee) != 0)
    {
      flush_scopes ();
      insert_filedep (id);
    }
  VEC_safe_push (int, heap, file.includee, id);
}

static void
file_pop (void)
{
  gcc_assert (!mo_isvalid ());
  flush_scopes ();
  VEC_pop (int, file.includee);
}

static void
file_init (const char *main_file)
{
  file.scopes = VEC_alloc (scope, heap, 10);
  file.includee = VEC_alloc (int, heap, 10);

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

  VEC_free (int, heap, file.includee);
  VEC_free (scope, heap, file.scopes);
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
} mo =
{
0, 0, false, false, false};

static void
mo_append_expanded_token (void)
{
  mo.expanded_count++;
}

static void
mo_append_macro_token (void)
{
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
  mo.expanded_count = mo.macro_count = 0;
  mo.cancel = mo.cascaded = false;
  gcc_assert (mo.valid == true);
  mo.valid = false;
}

static void
mo_enter (void)
{
  mo_append_expanded_token ();
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
  int start;
  int length;
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
	    p2->leader.file_offset, p2->start, p2->length);
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

static void
cache_reset (int reserve)
{
  if (mo_isvalid ())
    {
      /* Deal with multiple definitions are in a macro internal. */
      let *p2 = VEC_last (let, cache.auxiliary);
      mo_substract_macro_count (VEC_length (db_token, cache.itokens) -
				reserve - p2->start);
      p2->start = 0;
      p2->length = 0x1fffffff;
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
  p->start = VEC_length (db_token, cache.itokens);
  /* Here, the length is initialized as 0x1fffffff for tag all following cache.itokens
   * belong to the macro. */
  p->length = 0x1fffffff;
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
  p->length = mo_get_macro_count ();
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
    if (p->start <= tmp && tmp < p->start + p->length)
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
  else if (flag == DEF_CALLED_FUNC)
    insert_defrel (defid);
  file_insert_defid (defid);
}

static inline bool
is_uid (int cpp_type, int c_type)
{
  return cpp_type == CPP_NAME && c_type == 0xffff;
}

static inline void
demangle_type (db_token * token, int *cpp_type, int *c_type)
{
  enum symdb_flag flag = token->flag;
  *cpp_type = flag & CPP_TYPE_MASK;
  *c_type = (flag & C_TYPE_MASK) >> C_TYPE_SHIFT;
}

/* To strip outer parens successfully, we need the head of cache.itokens. */
static int
def_strip_paren_declarator_outer (int index)
{
  int pair = 0;
  while (true)
    {
      int cpp_type, c_type;
      db_token *tmp = cache_get (index);
      demangle_type (tmp, &cpp_type, &c_type);
      if (cpp_type != CPP_CLOSE_PAREN)
	break;
      if (VEC_length (db_token, cache.itokens) -
	  cache_skip_match_pair (index, ')') != pair++)
	break;
      index++;
    }
  return index;
}

static int
def_strip_paren_declarator_inner (int index)
{
  while (true)
    {
      int cpp_type, c_type;
      db_token *tmp = cache_get (index++);
      demangle_type (tmp, &cpp_type, &c_type);
      if (cpp_type != CPP_CLOSE_PAREN)
	break;
    }
  return index - 1;
}

static int
def_strip_paren_declarator_middle (int index)
{
  return def_strip_paren_declarator_inner (index);
}

static void
def_append_chk (int index, enum definition_flag flag)
{
  db_token *token;
  dyn_string_t str;
  int cpp_type, c_type;
  token = cache_get (index);
  str = token->value;
  demangle_type (token, &cpp_type, &c_type);
  gcc_assert (is_uid (cpp_type, c_type));
  token = cache_itoken_to_chtoken (index);
  demangle_type (token, &cpp_type, &c_type);
  gcc_assert (is_uid (cpp_type, c_type));
  def_append (flag, str, token->file_offset);
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

/* cpp callbacks <([{ */
/* The fold isn't class fold. */
static struct
{
  int macro_tristate;
} define_helper;

static void
cb_lex_token (cpp_reader * pfile, cpp_token_p token)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  /* Whether we're skipping by #ifdef/#if clause. */
  if (pfile->state.skipping)
    return;
  if (token->type == CPP_EOF)
    return;
  if (mo_isvalid ())
    mo_append_expanded_token ();
  else
    {
      print_token (token, gbuf);
      if (define_helper.macro_tristate == 1
	  && strcmp (dyn_string_buf (gbuf), "define") == 0)
	{
	  define_helper.macro_tristate = 2;
	  return;
	}
      if (define_helper.macro_tristate == 2)
	{
	  def_append (DEF_MACRO, gbuf, token->file_offset);
	  define_helper.macro_tristate = 0;
	}
      cb->lex_token = NULL;
    }
}

static void
cb_start_directive (cpp_reader * pfile)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  define_helper.macro_tristate = 1;
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
  bool fun_like = false;
  if (!(node->flags & NODE_BUILTIN))
    fun_like = node->value.macro->fun_like;
  if (pfile->context->prev == NULL)
    {
      bug_trap_token (token->file_offset);
      mo_enter ();
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

  control_panel_init (main_input_filename);
  cache_init ();
  file_init (main_input_filename);
  def_init ();
  gbuf = dyn_string_new (1024);

}

static void
symdb_unit_tini (void *gcc_data, void *user_data)
{
  dyn_string_delete (gbuf);
  def_tini ();
  file_tini ();
  cache_tini ();
  control_panel_tini ();

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
    mo_append_macro_token ();
}

static void
symdb_c_token (void *gcc_data, void *user_data)
{
  c_token *token = gcc_data;
  if (in_pragma)
    return;
  cache_append_itoken_c_stage (token);
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
  tree decl = (tree) gcc_data;
  db_token *token;
  int cpp_type, c_type;
  int index;
  if (block_list.call_func)
    return;
  if (TREE_CODE (decl) != FUNCTION_DECL)
    /* function-pointer, we don't care about it. */
    goto done;
  if (DECL_BUILT_IN (decl))
    goto done;
  token = cache_get (0);
  demangle_type (token, &cpp_type, &c_type);
  gcc_assert (cpp_type == CPP_OPEN_PAREN);
  index = def_strip_paren_declarator_inner (1);
  token = cache_get (index);
  demangle_type (token, &cpp_type, &c_type);
  gcc_assert (is_uid (cpp_type, c_type));
  def_append_chk (index, DEF_CALLED_FUNC);

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
  def_append_chk (0, DEF_ENUM_MEMBER);
  /* Don't call cache_reset(0) here, since enum specifier hasn't been parsed,
   * see symdb_declspecs. */
}

static void
symdb_extern_func_old_param (void *gcc_data, void *user_data)
{
  db_token *token;
  int cpp_type, c_type;
  token = cache_get (1);
  demangle_type (token, &cpp_type, &c_type);
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
      demangle_type (token, &cpp_type, &c_type);
      gcc_assert (cpp_type == CPP_OPEN_BRACE);
      index = 1;
    }
  token = cache_get (index);
  demangle_type (token, &cpp_type, &c_type);
  gcc_assert (cpp_type == CPP_CLOSE_PAREN);
  index = def_strip_paren_declarator_outer (index);
  index = cache_skip_match_pair (index, ')');
  index = def_strip_paren_declarator_inner (index);
  def_append_chk (index, DEF_FUNC);
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
*   direct-declarator ( parameter-type-list ) << function declaration.
*   direct-declarator ( identifier-list[opt] ) << function call.
* pointer:
*   type-qualifier-list[opt]
*   type-qualifier-list[opt] pointer
* type-qualifier-list:
*   type-qualifier
*   attributes
*   type-qualifier-list type-qualifier
*   type-qualifier-list attributes
*
* Our callback hook makes sure there's not function-call case at all.
*
* Case `( attributes[opt] declarator )' is called paren cases, see
* test/paren_declarator/a.c. There're three -- outer, middle and inner cases.
* `int ((**(funtdpcomplex)[2][3]))(void));'
* function pointer includes all of them, from left to right.
*/
static void
symdb_extern_var (void *gcc_data, void *user_data)
{
  void **pair = (void **) gcc_data;
  const struct c_declspecs *ds = pair[0];
  const struct c_declarator *da = pair[1];
  db_token *token;
  int cpp_type, c_type;
  int index;
  int fun = 0;

  if (ds->storage_class == csc_extern)
    /* User needn't the kind of definition at all. */
    goto done;

  while (da->kind == cdk_pointer || da->kind == cdk_attrs)
    da = da->declarator;
  if (da->kind == cdk_function)
    {
      if (da->declarator->kind != cdk_pointer)
	{
	  gcc_assert (da->declarator->kind == cdk_id);
	  /* We encountered a function declaration or `typedef int fun(void);'. */
	  if (ds->storage_class == csc_typedef)
	    fun = 1;
	  else
	    goto done;
	}
      else
	fun = 2;
    }

  index = def_strip_paren_declarator_outer (1);
  while (true)
    {
      token = cache_get (index);
      demangle_type (token, &cpp_type, &c_type);
      if (fun)
	{
	  gcc_assert (cpp_type == CPP_CLOSE_PAREN);
	  index = cache_skip_match_pair (index, ')');
	  if (fun == 2)
	    {
	      token = cache_get (index);
	      demangle_type (token, &cpp_type, &c_type);
	      gcc_assert (cpp_type == CPP_CLOSE_PAREN);
	      index = def_strip_paren_declarator_middle (index);
	    }
	  fun = 0;
	}
      else if (cpp_type == CPP_CLOSE_SQUARE)
	index = cache_skip_match_pair (index, ']');
      else
	{
	  index = def_strip_paren_declarator_inner (index);
	  if (ds->storage_class == csc_typedef)
	    def_append_chk (index, DEF_TYPEDEF);
	  else
	    def_append_chk (index, DEF_VAR);
	  break;
	}
    }

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
  int index = (int) pair[1];
  enum definition_flag flag;
  db_token *token;
  int cpp_type, c_type;

  if (ds->typespec_kind == ctsk_typeof)
    goto done;

  while (true)
    {
      token = cache_get (index);
      demangle_type (token, &cpp_type, &c_type);
      if (cpp_type != CPP_CLOSE_PAREN)
	break;
      index = cache_skip_match_pair (index, ')');
      token = cache_get (index);
      demangle_type (token, &cpp_type, &c_type);
      gcc_assert (cpp_type == CPP_NAME && c_type == RID_ATTRIBUTE);
      index++;
    }

  token = cache_get (index);
  demangle_type (token, &cpp_type, &c_type);
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
  demangle_type (token, &cpp_type, &c_type);
  if (is_uid (cpp_type, c_type))
    def_append_chk (index, flag);
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
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  /* When `-E' is passed, symdb_unit_init is skipped. */
  if (flag_preprocess_only)
    {
      printf ("`-E' or `-save-temps' aren't supported by symdb.so.");
      return -1;
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

  cpp_callbacks *cb = cpp_get_callbacks (parse_in);
  cb->macro_start_expand = cb_macro_start;
  cb->macro_end_expand = cb_macro_end;
  cb->start_directive = cb_start_directive;
  cb->end_directive = cb_end_directive;
  /* Note: cb->file_change callback is delayed to install in symdb_unit_init
   * for there's an inner hook -- cb_file_change of gcc/c-family/c-opts.c. */
  return 0;
}
