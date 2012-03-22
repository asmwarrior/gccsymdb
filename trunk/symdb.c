/* vim: foldmarker=<([{,}])> foldmethod=marker
 * symdb plugin -- collecting gcc intern data (cpp tokens and c/c++ tree) and
 * output them to sqlite3-format database file. See more from symdb.txt. To
 * read more efficiently, open it with vim7.
 * Copyright (C) zyf.zeroos@gmail.com.
 * */

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "c-family/c-common.h"
#include "input.h"
#include "dyn-string.h"
#include <sys/stat.h>
#include <time.h>
#include "libcpp/include/cpplib.h"
#include "libcpp/internal.h"
#include <sqlite3.h>

/* common <([{ */
/* Code and data here is used by both cpp stage and c stage. */
static dyn_string_t gbuf;

static struct sqlite3 *db;

static struct
{
  dyn_string_t db_file;
  dyn_string_t prj_dir;
  dyn_string_t cwd;		/* current work path */

  bool debug;
  bool compare_cpp_token;
  bool compare_tree;

  /* created by main stage, filled by cpp stage, used by c stage. */
  dyn_string_t compiled_file;
  int ifile_id;
  int chfile_id;
} control_panel;

static void
db_error (int cond)
{
  if (cond)
    {
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

static void
file_full_path (const char *file, dyn_string_t result)
{
  dyn_string_copy_cstr (result, "");
  if (file[0] != DIR_SEPARATOR)	/* relative path */
    {
      dyn_string_append (result, control_panel.cwd);
      dyn_string_append_char (result, DIR_SEPARATOR);
    }
  dyn_string_append_cstr (result, file);
}

static void
append_file (dyn_string_t chfile_id, dyn_string_t ifile_id)
{
  int nrow, ncolumn;
  char *error_msg, **result;
  struct stat filestat;
  char mtime[21];

  dyn_string_copy_cstr (gbuf, "select id from chFile where fullName = '");
  dyn_string_append (gbuf, control_panel.compiled_file);
  dyn_string_append_cstr (gbuf, "';");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &result,
			       &nrow, &ncolumn, &error_msg));
  if (nrow == 0)
    {
      sqlite3_free_table (result);
      dyn_string_copy_cstr (gbuf, "insert into chFile values (NULL, '");
      dyn_string_append (gbuf, control_panel.compiled_file);
      dyn_string_append_cstr (gbuf, "', ");
      if (stat (dyn_string_buf (control_panel.compiled_file), &filestat) != 0)
	{
	  perror (NULL);
	  sqlite3_close (db);
	  exit (1);
	}
      sprintf (mtime, "%lld", (long long) filestat.st_mtime);
      dyn_string_append_cstr (gbuf, mtime);
      dyn_string_append_cstr (gbuf, ", 'false');");
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
      dyn_string_copy_cstr (gbuf, "select max(id) from chFile;");
      db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &result,
				   &nrow, &ncolumn, &error_msg));
    }
  dyn_string_copy_cstr (chfile_id, result[1]);
  sqlite3_free_table (result);

  dyn_string_copy_cstr (gbuf, "select id from iFile where mainFileID = ");
  dyn_string_append (gbuf, chfile_id);
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &result,
			       &nrow, &ncolumn, &error_msg));
  if (nrow == 0)
    {
      sqlite3_free_table (result);
      dyn_string_copy_cstr (gbuf, "insert into iFile values (NULL, ");
      dyn_string_append (gbuf, chfile_id);
      dyn_string_append_cstr (gbuf, ", 0, 0, 0, 0, 0);");
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
      dyn_string_copy_cstr (gbuf, "select max(id) from iFile;");
      db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &result,
				   &nrow, &ncolumn, &error_msg));
    }
  dyn_string_copy_cstr (ifile_id, result[1]);
  sqlite3_free_table (result);
}

static time_t
file_mtime (const char *file)
{
  struct stat filestat;
  if (stat (file, &filestat) != 0)
    {
      perror (NULL);
      sqlite3_close (db);
      exit (1);
    }
  return filestat.st_mtime;
}

/* }])> */

/* CPP (c preprocess) specific. <([{ */
/* The vim fold is mainly for collecting and outputting cpp/c tokens to
 * database, as the section <DB Format> of the document shows, COMMON_TOKEN is
 * outputted by symdb_cpp_token, EXPANDED_TOKEN/MACRO_TOKEN are outputted by
 * class mo, ERASED_TOKEN is outputted by cb_callbacks::{cb_comment,
 * cb_start/end_directive}. According to current plugin architecture, c/c++
 * keyword type is collected by recording the last CPP_NAME token in class
 * c_tok.
 * */
/* cpp includes something shared by the whole symdb-cpp stage <([{ */
typedef const struct cpp_token *cpp_token_p;

__extension__ enum symdb_type
{
  CPP_TYPE_SHIFT = 0,
  CPP_TYPE_MASK = 0xff,
  C_TYPE_SHIFT = 8,
  C_TYPE_MASK = 0xffff00,
  SYMDB_TYPE_SHIFT = 24,
  SYMDB_TYPE_MASK = 0xff000000,

  EXPANDED_TOKEN = 1 << SYMDB_TYPE_SHIFT,
  ERASED_TOKEN = 2 << SYMDB_TYPE_SHIFT,
  COMMON_TOKEN = 3 << SYMDB_TYPE_SHIFT,
  MACRO_TOKEN = 4 << SYMDB_TYPE_SHIFT,
  END_TAG_TOKEN = 5 << SYMDB_TYPE_SHIFT,

  /* flags for tracing system macro expansion. */
  SYSHDR_FLAG = 16 << SYMDB_TYPE_SHIFT,
  SYSHDR_MASK = 0xf0000000,

  CH_TOKEN_START = EXPANDED_TOKEN,
  CH_TOKEN_END = MACRO_TOKEN,
  I_TOKEN_START = COMMON_TOKEN,
  I_TOKEN_END = END_TAG_TOKEN,
};

typedef void (*CB_FILE_CHANGE) (cpp_reader *, const struct line_map *);
static struct
{
  cpp_reader *pfile;
  CB_FILE_CHANGE orig_file_change;

  /* control the behaviour of cb_directive_token. */
  enum symdb_type directive_type;
  /* control the behaviour of symdb_cpp_token. */
  enum symdb_type i_type;
} cpp;

/* Keep it in mind that cpp_token_len can't get the exact length of the token!
 * */
static void
print_token (cpp_token_p token, dyn_string_t str)
{
  unsigned char *head, *tail;
  dyn_string_resize (str, cpp_token_len (token));
  head = (unsigned char *) dyn_string_buf (str);
  tail = cpp_spell_token (cpp.pfile, token, head, false);
  *tail = '\0';
  dyn_string_length (str) = tail - head;
}

static long long int insert_into_chtoken (int, int, int, int);
static void insert_into_itoken (int, int, const char *, long long int);
static int inc_top (void);
static int
output_chtoken (cpp_token_p token, int flag)
{
  print_token (token, gbuf);
  int length = dyn_string_length (gbuf);
  long long int id =
    insert_into_chtoken (flag, length, inc_top (), token->file_offset);
  insert_into_itoken (flag, length, dyn_string_buf (gbuf), id);
  return id;
}

static void mo_init (void);
static void inc_init (void);
static void cpp_db_init (void);
static void cb_macro_start (struct cpp_reader *, const struct cpp_token *,
			    const struct cpp_hashnode *);
static void cb_intern_expand (struct cpp_reader *, void *, int, bool);
static void cb_end_arg (struct cpp_reader *, bool);
static void cb_macro_end (struct cpp_reader *);
static void cb_comment (struct cpp_reader *, const struct cpp_token *);
static void cb_start_directive (struct cpp_reader *,
				const struct cpp_token *);
static void cb_directive_token (struct cpp_reader *pfile,
				const struct cpp_token *);
static void cb_end_directive (struct cpp_reader *pfile);
static void cb_file_change (cpp_reader *, const struct line_map *);
static void
symdb_token_init (cpp_reader * pfile)
{
  char id[11];

  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  cb->macro_start_expand = cb_macro_start;
  cb->macro_end_expand = cb_macro_end;
  cb->comment = cb_comment;
  cb->start_directive = cb_start_directive;
  cb->end_directive = cb_end_directive;
  cpp.orig_file_change = cb->file_change;
  cb->file_change = cb_file_change;

  cpp.pfile = pfile;
  cpp.i_type = COMMON_TOKEN;
  mo_init ();
  inc_init ();
  cpp_db_init ();

  dyn_string_copy_cstr (gbuf, "update iFile set iTokenStartID = ");
  dyn_string_append_cstr (gbuf,
			  " (select seq from sqlite_sequence where name = 'iToken') + 1 ");
  dyn_string_append_cstr (gbuf, " where id = ");
  sprintf (id, "%d", control_panel.ifile_id);
  dyn_string_append_cstr (gbuf, id);
  dyn_string_append_cstr (gbuf, ";");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void mo_tini (void);
static void inc_tini (void);
static void cpp_db_tini (void);
static void
symdb_token_tini (void)
{
  char id[11];
  dyn_string_copy_cstr (gbuf, "update iFile set iTokenEndID = ");
  dyn_string_append_cstr (gbuf,
			  " (select seq from sqlite_sequence where name = 'iToken') + 1 ");
  dyn_string_append_cstr (gbuf, " where id = ");
  sprintf (id, "%d", control_panel.ifile_id);
  dyn_string_append_cstr (gbuf, id);
  dyn_string_append_cstr (gbuf, ";");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));

  cpp_db_tini ();
  inc_tini ();
  mo_tini ();
  cpp_get_callbacks (cpp.pfile)->file_change = cpp.orig_file_change;
}

/* }])> */

/* class inc (abbr. include) is responsible for stacking include files. <([{ */
/* class inc is responsible for stacking include files and recording their
 * chFile::id for table chtoken insertion. It also manages FileDependence
 * table.
 *
 * public methods are:
 * 1) inc_trace: push include-file.
 * 2) inc_top.
 * 3) inc_init/inc_tini.
 */
DEF_VEC_I (int);
DEF_VEC_ALLOC_I (int, heap);

static struct
{
  VEC (int, heap) * stack;

  struct sqlite3_stmt *select_chfile;
  struct sqlite3_stmt *insert_chfile;
  struct sqlite3_stmt *select_filedep;
  struct sqlite3_stmt *insert_filedep;
} inc;

static void inc_trace (const char *, bool);
static void
inc_init (void)
{
  inc.stack = VEC_alloc (int, heap, 10);

  db_error (sqlite3_prepare_v2 (db,
				"select id, mtime from chFile where fullName = ?;",
				-1, &inc.select_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into chFile values (NULL, ?, ?, ?);",
				-1, &inc.insert_chfile, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select id from FileDependence where iFileID = ? and hID = ?;",
				-1, &inc.select_filedep, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into FileDependence values (NULL, ?, ?);",
				-1, &inc.insert_filedep, 0));

  inc_trace (dyn_string_buf (control_panel.compiled_file), false);
}

static void
inc_tini (void)
{
  sqlite3_finalize (inc.insert_filedep);
  sqlite3_finalize (inc.select_filedep);
  sqlite3_finalize (inc.insert_chfile);
  sqlite3_finalize (inc.select_chfile);

  VEC_free (int, heap, inc.stack);
}

static void
inc_reset_tokens (int chfile_id)
{
  char id[11];
  /* Erase iToken table. */
  sprintf (id, "%d", control_panel.ifile_id);
  dyn_string_copy_cstr (gbuf, "delete from iToken where id >= ");
  dyn_string_append_cstr (gbuf,
			  "(select iTokenStartID from iFile where id = ");
  dyn_string_append_cstr (gbuf, id);
  dyn_string_append_cstr (gbuf, " ) and id < ");
  dyn_string_append_cstr (gbuf, "(select iTokenEndID from iFile where id = ");
  dyn_string_append_cstr (gbuf, id);
  dyn_string_append_cstr (gbuf, ");");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));

  /* Erase the cpp stage tokens, note, there's some triggers in database which
   * can erase MacroDescription and MacroToken cascade. */
  sprintf (id, "%d", chfile_id);
  dyn_string_copy_cstr (gbuf, "delete from chToken where chFileID = ");
  dyn_string_append_cstr (gbuf, id);
  dyn_string_append_cstr (gbuf, ";");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));

  /* Erase C Tree nodes. @TODO@. */
}

static void
inc_push (const char *fn, bool sysp)
{
  int size, chfile_id;
  time_t mtime;
  if (fn == NULL)
    {
      VEC_pop (int, inc.stack);
      return;
    }
  mtime = file_mtime (fn);
  size = strlen (fn);
  db_error (sqlite3_bind_text
	    (inc.select_chfile, 1, fn, size, SQLITE_STATIC));
  if (sqlite3_step (inc.select_chfile) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_text (inc.insert_chfile, 1,
				   fn, size, SQLITE_STATIC));
      db_error (sqlite3_bind_int64 (inc.insert_chfile, 2, mtime));
      if (sysp)
	db_error (sqlite3_bind_text (inc.insert_chfile, 3,
				     "true", sizeof ("true"), SQLITE_STATIC));
      else
	db_error (sqlite3_bind_text (inc.insert_chfile, 3,
				     "false", sizeof ("false"),
				     SQLITE_STATIC));
      execute_sql (inc.insert_chfile);
      sqlite3_reset (inc.select_chfile);
      db_error (sqlite3_step (inc.select_chfile) != SQLITE_ROW);
    }
  chfile_id = sqlite3_column_int (inc.select_chfile, 0);
  if (mtime > sqlite3_column_int64 (inc.select_chfile, 1))
    inc_reset_tokens (chfile_id);
  VEC_safe_push (int, heap, inc.stack, chfile_id);
  revalidate_sql (inc.select_chfile);
  db_error (sqlite3_bind_int (inc.select_filedep, 1, control_panel.ifile_id));
  db_error (sqlite3_bind_int (inc.select_filedep, 2, chfile_id));
  if (sqlite3_step (inc.select_filedep) != SQLITE_ROW)
    {
      db_error (sqlite3_bind_int (inc.insert_filedep, 1,
				  control_panel.ifile_id));
      db_error (sqlite3_bind_int (inc.insert_filedep, 2, chfile_id));
      execute_sql (inc.insert_filedep);
    }
  revalidate_sql (inc.select_filedep);
}

static void
inc_trace (const char *fn, bool sysp)
{
  if (fn != NULL)
    {
      dyn_string_t full_path = dyn_string_new (256);
      file_full_path (fn, full_path);
      inc_push (dyn_string_buf (full_path), sysp);
      dyn_string_delete (full_path);
    }
  else
    inc_push (NULL, sysp);
}

static int
inc_top (void)
{
  return VEC_last (int, inc.stack);
}

/* }])> */

/* cpp_db includes sql clauses for inserting token to database. <([{ */
struct
{
  struct sqlite3_stmt *select_chtoken;
  struct sqlite3_stmt *insert_chtoken;
  struct sqlite3_stmt *max_chtoken;
  struct sqlite3_stmt *insert_macrodesc;
  struct sqlite3_stmt *insert_macrotoken;
  struct sqlite3_stmt *insert_itoken;
} cpp_db;

static void
cpp_db_init (void)
{
  db_error (sqlite3_prepare_v2 (db,
				"select id, flag from chToken where chFileID = ? and fileOffset = ?;",
				-1, &cpp_db.select_chtoken, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into chToken values (NULL, ?, ?, ?, ?);",
				-1, &cpp_db.insert_chtoken, 0));
  db_error (sqlite3_prepare_v2 (db, "select max(id) from chToken;",
				-1, &cpp_db.max_chtoken, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into MacroDescription values (NULL, ?, ?, ?, ?);",
				-1, &cpp_db.insert_macrodesc, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into MacroToken values (NULL, ?, ?);",
				-1, &cpp_db.insert_macrotoken, 0));
  db_error (sqlite3_prepare_v2 (db,
				"insert into iToken values (NULL, ?, ?, ?, ?);",
				-1, &cpp_db.insert_itoken, 0));
}

static void
cpp_db_tini (void)
{
  sqlite3_finalize (cpp_db.insert_itoken);
  sqlite3_finalize (cpp_db.insert_macrotoken);
  sqlite3_finalize (cpp_db.insert_macrodesc);
  sqlite3_finalize (cpp_db.max_chtoken);
  sqlite3_finalize (cpp_db.insert_chtoken);
  sqlite3_finalize (cpp_db.select_chtoken);
}

static void
insert_into_itoken (int flag, int length, const char *value, long long int id)
{
  switch (flag & SYMDB_TYPE_MASK)
    {
    case ERASED_TOKEN:
    case EXPANDED_TOKEN:
    case EXPANDED_TOKEN | SYSHDR_FLAG:
      if (!control_panel.debug)
	return;
      break;
    case MACRO_TOKEN | EXPANDED_TOKEN | SYSHDR_FLAG:
      return;
    }
  flag &= ~SYSHDR_FLAG;
  db_error (sqlite3_bind_int (cpp_db.insert_itoken, 1, flag));
  if (control_panel.debug)
    {
      db_error (sqlite3_bind_int (cpp_db.insert_itoken, 2, length));
      db_error (sqlite3_bind_text (cpp_db.insert_itoken, 3,
				   value, length, SQLITE_STATIC));
    }
  else
    {
      db_error (sqlite3_bind_null (cpp_db.insert_itoken, 2));
      db_error (sqlite3_bind_null (cpp_db.insert_itoken, 3));
    }
  db_error (sqlite3_bind_int64 (cpp_db.insert_itoken, 4, id));
  execute_sql (cpp_db.insert_itoken);
}

static void
insert_into_macrotoken (int flag, const char *value, int length)
{
  db_error (sqlite3_bind_int (cpp_db.insert_macrotoken, 1, flag));
  db_error (sqlite3_bind_text (cpp_db.insert_macrotoken, 2,
			       value, length, SQLITE_STATIC));
  execute_sql (cpp_db.insert_macrotoken);
}

static void
insert_into_macrodesc (long long int leader, int ep_count, int mo_count,
		       long long int mo_start)
{
  db_error (sqlite3_bind_int64 (cpp_db.insert_macrodesc, 1, leader));
  db_error (sqlite3_bind_int (cpp_db.insert_macrodesc, 2, ep_count));
  db_error (sqlite3_bind_int (cpp_db.insert_macrodesc, 3, mo_count));
  db_error (sqlite3_bind_int64 (cpp_db.insert_macrodesc, 4, mo_start));
  execute_sql (cpp_db.insert_macrodesc);
}

static long long int
insert_into_chtoken (int flag, int length, int file_id, int file_offset)
{
  long long int ret = -1;
  db_error (sqlite3_bind_int (cpp_db.select_chtoken, 1, file_id));
  db_error (sqlite3_bind_int (cpp_db.select_chtoken, 2, file_offset));
  while (sqlite3_step (cpp_db.select_chtoken) == SQLITE_ROW)
    {
      // In fact, at most 2 rows are returned, see symdb document.
      if ((sqlite3_column_int (cpp_db.select_chtoken, 1) &
	   (signed) (CPP_TYPE_MASK | SYMDB_TYPE_MASK)) == flag)
	{
	  ret = sqlite3_column_int64 (cpp_db.select_chtoken, 0);
	  break;
	}
    }
  if (ret == -1)
    {
      db_error (sqlite3_bind_int (cpp_db.insert_chtoken, 1, flag));
      db_error (sqlite3_bind_int (cpp_db.insert_chtoken, 2, length));
      db_error (sqlite3_bind_int (cpp_db.insert_chtoken, 3, file_id));
      db_error (sqlite3_bind_int (cpp_db.insert_chtoken, 4, file_offset));
      execute_sql (cpp_db.insert_chtoken);
      db_error (sqlite3_step (cpp_db.max_chtoken) != SQLITE_ROW);
      ret = sqlite3_column_int64 (cpp_db.max_chtoken, 0);
      db_error (sqlite3_reset (cpp_db.max_chtoken));
    }
  db_error (sqlite3_reset (cpp_db.select_chtoken));
  return ret;
}

/* }])> */

/* class mo (abbr. macro) is responsible for macro... <([{ */
/* class mo is responsible for collecting all EXPANDED_TOKEN and MACRO_TOKEN
 * tokens and flush them together to database by mo_flush.
 *
 * public methods are:
 * 1) mo_append_token.
 * 2) mo_cancel.
 * 3) mo_init/mo_tini.
 * 4) mo_maybe_cascaded.
 * 1 and 2 are called by <cpp_callbacks>.
 * data member mo.cascaded_func/sys_macro can be visited by class cpp_callbacks directly.
 */
/* db_token is used to cache EXPANDED_TOKEN and MACRO_TOKEN tokens coming from
 * cpp_get_token for class mo. */
typedef struct
{
  int cpp_type;
  dyn_string_t value;
  int file_offset;
} db_token;
DEF_VEC_O (db_token);
DEF_VEC_ALLOC_O (db_token, heap);
static struct
{
  VEC (db_token, heap) * expanded_tokens;
  VEC (db_token, heap) * macro_tokens;
  long long int leader_expanded_token;
  bool cancel;
  bool cascaded_func;
  int sys_macro;

  struct sqlite3_stmt *select_macrodesc;
  struct sqlite3_stmt *select_macrotoken;
  struct sqlite3_stmt *select_max_macrotoken;
} mo;

static void
mo_init (void)
{
  mo.expanded_tokens = VEC_alloc (db_token, heap, 10);
  mo.macro_tokens = VEC_alloc (db_token, heap, 10);
  mo.leader_expanded_token = 0;
  mo.cancel = false;
  mo.cascaded_func = false;
  mo.sys_macro = 0;

  db_error (sqlite3_prepare_v2 (db,
				"select expandedCount, macroCount, macroStartID from MacroDescription"
				" where leaderchTokenID = ?;",
				-1, &mo.select_macrodesc, 0));
  db_error (sqlite3_prepare_v2 (db, "select max(id) from MacroToken;",
				-1, &mo.select_max_macrotoken, 0));
  db_error (sqlite3_prepare_v2 (db,
				"select value from MacroToken where id >= ? and id < ? order by id;",
				-1, &mo.select_macrotoken, 0));
}

static void
mo_tini (void)
{
  int ix;
  db_token *p;

  sqlite3_finalize (mo.select_macrotoken);
  sqlite3_finalize (mo.select_max_macrotoken);
  sqlite3_finalize (mo.select_macrodesc);

  for (ix = 0; VEC_iterate (db_token, mo.macro_tokens, ix, p); ix++)
    dyn_string_delete (p->value);
  VEC_free (db_token, heap, mo.macro_tokens);
  VEC_free (db_token, heap, mo.expanded_tokens);
}

static int
mo_compare (void)
{
  int ep_count = VEC_length (db_token, mo.expanded_tokens);
  int mo_count = VEC_length (db_token, mo.macro_tokens);
  int ix;
  long long int mo_start = 0, tmp;
  db_token *p;
  db_error (sqlite3_bind_int64 (mo.select_macrodesc, 1,
				mo.leader_expanded_token));
  while (sqlite3_step (mo.select_macrodesc) == SQLITE_ROW)
    {
      if (ep_count != sqlite3_column_int (mo.select_macrodesc, 0)
	  || mo_count != sqlite3_column_int (mo.select_macrodesc, 1))
	continue;
      tmp = sqlite3_column_int64 (mo.select_macrodesc, 2);
      if (!control_panel.compare_cpp_token)
	{
	  mo_start = tmp;
	  break;
	}
      /* heavy comparision. */
      db_error (sqlite3_bind_int64 (mo.select_macrotoken, 1, tmp));
      db_error (sqlite3_bind_int64 (mo.select_macrotoken, 2, tmp + mo_count));
      sqlite3_step (mo.select_macrotoken);
      for (ix = 0; VEC_iterate (db_token, mo.macro_tokens, ix, p); ix++)
	{
	  if (strcmp
	      ((const char *) sqlite3_column_text (mo.select_macrotoken, 0),
	       dyn_string_buf (p->value)) != 0)
	    break;
	  sqlite3_step (mo.select_macrotoken);
	}
      revalidate_sql (mo.select_macrotoken);
      if (ix == mo_count)
	{
	  mo_start = tmp;
	  break;
	}
    }
  revalidate_sql (mo.select_macrodesc);
  return mo_start;
}

static void
mo_flush_macrotoken (long long int mo_start)
{
  int ix, flag, length;
  db_token *p;
  const char *buf;
  if (mo_start == 0)
    {
      sqlite3_step (mo.select_max_macrotoken);
      mo_start = sqlite3_column_int64 (mo.select_max_macrotoken, 0) + 1;
      revalidate_sql (mo.select_max_macrotoken);
      insert_into_macrodesc (mo.leader_expanded_token,
			     VEC_length (db_token, mo.expanded_tokens),
			     VEC_length (db_token, mo.macro_tokens),
			     mo_start);
      for (ix = 0; VEC_iterate (db_token, mo.macro_tokens, ix, p); ix++)
	{
	  flag = p->cpp_type;
	  length = dyn_string_length (p->value);
	  buf = dyn_string_buf (p->value);
	  insert_into_macrotoken (flag, buf, length);
	  insert_into_itoken (flag, length, buf, mo_start + ix);
	}
    }
  else
    {
      for (ix = 0; VEC_iterate (db_token, mo.macro_tokens, ix, p); ix++)
	{
	  flag = p->cpp_type;
	  length = dyn_string_length (p->value);
	  buf = dyn_string_buf (p->value);
	  insert_into_itoken (flag, length, buf, mo_start + ix);
	}
    }
}

static void
mo_revalidate (void)
{
  int ix;
  db_token *p;
  for (ix = 0; VEC_iterate (db_token, mo.expanded_tokens, ix, p); ix++)
    dyn_string_delete (p->value);
  VEC_truncate (db_token, mo.expanded_tokens, 0);
  for (ix = 0; VEC_iterate (db_token, mo.macro_tokens, ix, p); ix++)
    dyn_string_delete (p->value);
  VEC_truncate (db_token, mo.macro_tokens, 0);
  mo.cancel = false;
  mo.cascaded_func = false;
}

static int
mo_flush_expandedtoken (db_token * p)
{
  int flag = p->cpp_type;
  int length = dyn_string_length (p->value);
  long long int id =
    insert_into_chtoken (flag, length, inc_top (), p->file_offset);
  insert_into_itoken (flag, length, dyn_string_buf (p->value), id);
  return id;
}

static void
mo_flush (void)
{
  int ix;
  db_token *p;
  if (mo.cancel)
    {
      gcc_assert (VEC_length (db_token, mo.expanded_tokens) == 2 &&
		  VEC_length (db_token, mo.macro_tokens) == 0);
      /* the two tokens are outputted again from symdb_cpp_token(). */
      mo_revalidate ();
    }
  else
    {
      p = VEC_index (db_token, mo.expanded_tokens, 0);
      mo.leader_expanded_token = mo_flush_expandedtoken (p);
      for (ix = 1; VEC_iterate (db_token, mo.expanded_tokens, ix, p); ix++)
	mo_flush_expandedtoken (p);
      mo_flush_macrotoken (mo_compare ());
      mo_revalidate ();
    }
}

static void ctoken_push_macrotoken (db_token *);
static void
mo_append_token (cpp_token_p token, int trace)
{
  db_token *p;
  switch (trace)
    {
    case EXPANDED_TOKEN:
    case EXPANDED_TOKEN | SYSHDR_FLAG:
      p = VEC_safe_push (db_token, heap, mo.expanded_tokens, NULL);
      break;
    case MACRO_TOKEN:
    case MACRO_TOKEN | SYSHDR_FLAG:
    case MACRO_TOKEN | EXPANDED_TOKEN | SYSHDR_FLAG:
      p = VEC_safe_push (db_token, heap, mo.macro_tokens, NULL);
      break;
    default:
      gcc_unreachable ();
    }
  p->value = dyn_string_new (32);
  p->cpp_type = trace | token->type;
  p->file_offset = token->file_offset;
  print_token (token, p->value);
  if (token->type == CPP_NAME)
    ctoken_push_macrotoken (p);
}

static inline void
mo_cancel (void)
{
  if (mo.sys_macro)
    VEC_pop (db_token, mo.macro_tokens);
  else
    mo.cancel = true;
}

static inline bool
mo_maybe_cascaded (void)
{
  if (VEC_length (db_token, mo.expanded_tokens) == 1
      && VEC_length (db_token, mo.macro_tokens) == 0)
    return true;
  return false;
}

/* }])> */

/* class c_tok <([{ */
/* 
 * According to current plugin architecture, I append c/c++ keyword type in
 * PLUGIN_C_TOKEN after PLUGIN_CPP_TOKEN for every CPP_NAME token.
 * */
static struct
{
  cpp_token_p last_common_token;
  enum symdb_type common_type;
  db_token *last_macro_token;
} c_tok;

static void
ctoken_push_macrotoken (db_token * p)
{
  c_tok.last_macro_token = p;
}

/* }])> */
/* }])> */

/* C specific. <([{ */
/* The vim fold is used to output gcc tree to database, currently, it isn't
 * completed! */
/* definitions <([{ */
__extension__ enum symdb_tree_code
{
  decoration_code = MAX_TREE_CODES * 2,
  initializer_code,
  function_args_code,
  function_result_code,
  function_body_code,
};

struct type_value
{
  /* storage declaration specifier. */
  __extension__ enum
  {
    sds_typedef,
    sds_extern,
    sds_static,
    sds_auto,
    sds_register,
    sds_inline,
    sds___thread,
  } sds:8;
  /* type specifier. */
  __extension__ enum
  {
    ts_void = itk_none * 2,
    ts_float = itk_none * 2,
    ts_double,
    ts_struct,
    ts_enum,
    /* gcc extensions. */
  } ts:16;
  /* type qualifier. */
  __extension__ enum
  {
    tq_const,
    tq_restrict,
    tq_volatile,
  } tq:8;
};
/* }])> */

static struct
{
  struct sqlite3_stmt *stmt_ctree;
  struct sqlite3_stmt *stmt_ctree2;
  struct sqlite3_stmt *stmt_ctree3;
  struct sqlite3_stmt *stmt_ctree4;

  int token_id;
} c;

/* class syntax tree. <([{ */
static inline int
get_syntax_id (void)
{
  int result;
  db_error (sqlite3_step (c.stmt_ctree2) != SQLITE_ROW);
  result = sqlite3_column_int (c.stmt_ctree2, 0);
  db_error (sqlite3_reset (c.stmt_ctree2));
  return result;
}

static int
output_syntax (int code, void *data, int size, int parent_id)
{
  db_error (sqlite3_bind_int (c.stmt_ctree, 1, code));
  if (data == NULL)
    db_error (sqlite3_bind_null (c.stmt_ctree, 2));
  else
    db_error (sqlite3_bind_blob (c.stmt_ctree, 2, data, size, SQLITE_STATIC));
  db_error (sqlite3_bind_int (c.stmt_ctree, 3, parent_id));
  execute_sql (c.stmt_ctree);
  return get_syntax_id ();
}

static void
output_semantic (int syntax_id, int scope_id, int decl_id, int token_id)
{
  db_error (sqlite3_bind_int (c.stmt_ctree3, 1, syntax_id));
  db_error (sqlite3_bind_int (c.stmt_ctree3, 2, scope_id));
  db_error (sqlite3_bind_int (c.stmt_ctree3, 3, decl_id));
  db_error (sqlite3_bind_int (c.stmt_ctree3, 4, token_id));
  execute_sql (c.stmt_ctree3);
}

static bool
compare_semantic (int syntax_id, int scope_id, int decl_id, int token_id)
{
  return true;
}

static void output_type (tree, int);

static int
output_storage_specifier (tree decl)
{
  int result = 0;
  if (!TREE_PUBLIC (decl))
    result += 1;
  if (DECL_EXTERNAL (decl))
    result += 2;
  if (TREE_CODE (decl) == VAR_DECL)
    {
      if (DECL_REGISTER (decl))
	result += 4;
      if (DECL_THREAD_LOCAL_P (decl))
	result += 8;
    }
  else if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      if (DECL_DECLARED_INLINE_P (decl))
	result += 16;
    }
  return result;
}

static int
output_type_specifier (tree type)
{
  int result = 0;
  if (type == void_type_node)
    result = ts_void;
  else if (type == char_type_node)
    result = itk_char;
  else if (type == signed_char_type_node)
    result = itk_signed_char;
  else if (type == unsigned_char_type_node)
    result = itk_unsigned_char;
  else if (type == short_integer_type_node)
    result = itk_short;
  else if (type == short_unsigned_type_node)
    result = itk_unsigned_short;
  else if (type == integer_type_node)
    result = itk_int;
  else if (type == unsigned_type_node)
    result = itk_unsigned_int;
  else if (type == long_integer_type_node)
    result = itk_long;
  else if (type == long_unsigned_type_node)
    result = itk_unsigned_long;
  else if (type == long_long_integer_type_node)
    result = itk_long_long;
  else if (type == long_long_unsigned_type_node)
    result = itk_unsigned_long_long;
  return result << 8;
}

static int
output_type_qualifier (tree type)
{
  int result = 0;
  if (TYPE_VOLATILE (type))
    result += 1;
  if (TYPE_READONLY (type))
    result += 2;
  if (TYPE_RESTRICT (type))
    result += 4;
  return result << 24;
}

static tree
output_array (tree type, int parent_id)
{
  int capacity = 10;
  int *value = XNEWVEC (int, capacity);
  int pos = 0;
  for (; TREE_CODE (type) == ARRAY_TYPE; pos++, type = TREE_TYPE (type))
    {
      tree domain = TYPE_DOMAIN (type);
      if (pos >= capacity)
	value = XRESIZEVEC (int, value, capacity * 1.1);
      value[pos] = TREE_INT_CST_LOW (TYPE_MAX_VALUE (domain)) + 1;
    }
  if (pos != 0)
    output_syntax (ARRAY_TYPE, value, sizeof (int) * pos, parent_id);
  XDELETEVEC (value);
  return type;
}

static void
output_function_pointer (tree type, int parent_id)
{
  int temp;
  tree args = TYPE_ARG_TYPES (type);
  tree result = TREE_TYPE (type);
  output_syntax ((int) TREE_CODE (type), NULL, 0, parent_id);
  parent_id = get_syntax_id ();
  output_syntax (function_args_code, NULL, 0, parent_id);
  temp = get_syntax_id ();
  for (; args != void_list_node; args = TREE_CHAIN (args))
    {
      output_type (TREE_VALUE (args), temp);
    }
  output_syntax (function_result_code, NULL, 0, parent_id);
  temp = get_syntax_id ();
  output_type (result, temp);
}

static tree
output_pointer (tree type, int parent_id)
{
  int capacity = 10;
  int *value = XNEWVEC (int, capacity);
  int pos = 0;
  for (; TREE_CODE (type) == POINTER_TYPE; pos++, type = TREE_TYPE (type))
    {
      if (pos >= capacity)
	value = XRESIZEVEC (int, value, capacity * 1.1);
      value[pos] = output_type_qualifier (type);
    }
  if (pos != 0)
    output_syntax (POINTER_TYPE, value, sizeof (int) * pos, parent_id);
  XDELETEVEC (value);
  return type;
}

static void
output_initializer (tree decl)
{
}

static void
output_attribute (tree decl)
{
}

static void
output_type (tree target, int parent_id)
{
  tree type;
  int value = 0;
  if (DECL_P (target))
    {
      type = TREE_TYPE (target);
      value = output_storage_specifier (target);
    }
  else
    type = target;
  if (TREE_CODE (type) == ARRAY_TYPE)
    type = output_array (type, parent_id);
  if (TREE_CODE (type) == POINTER_TYPE)
    type = output_pointer (type, parent_id);
  if (TREE_CODE (type) == FUNCTION_TYPE)
    output_function_pointer (type, parent_id);
  else
    {
      value |= output_type_specifier (type);
      value |= output_type_qualifier (type);
    }
  output_syntax (decoration_code, &value, sizeof (value), parent_id);
}

/* variable tree: TREE_CODE (var) == VAR_DECL
 *    <root> = DECL_NAME (var).
 *    type = TREE_TYPE (var).
 *    attributes = DECL_ATTRIBUTES (var)
 *    initializer = DECL_INITIAL (var);
 *    location = DECL_SOURCE_LOCATION (var)
 */
static void
output_var (tree var)
{
  int parent_id;
  c.token_id = 0;		/* SYMDB_INDEX (DECL_NAME (var)); */
  if (!compare_semantic (parent_id, 0, 0, c.token_id))
    return;
  output_syntax ((int) TREE_CODE (var), &c.token_id, sizeof (c.token_id), 0);
  parent_id = get_syntax_id ();
  output_semantic (parent_id, 0, 0, c.token_id);
  output_type (var, parent_id);
  output_initializer (var);
  output_attribute (var);
}

/* function body: TREE_CODE (fnbody) == BIND_EXPR
 *    vars = BIND_EXPR_VARS (fnbody), which is a brief profile of all variables
 *    in the function.
 *    statements = BIND_EXPR_BODY (fnbody), the set of function statements.
 *    BIND_EXPR_BLOCK (fnbody), not be outputted.
 *    misc. see more from BIND_EXPR of gcc/tree.def.
 */
static void
output_fnbody (tree fnbody)
{
}

static void
symdb_c_tree (tree ctree)
{
  tree tmp;
  debug_tree (ctree);
  if (TREE_CODE (ctree) == TYPE_DECL)
    {
      tmp = DECL_NAME (ctree);
    }
  else if (TREE_CODE (ctree) == RECORD_TYPE ||
	   TREE_CODE (ctree) == UNION_TYPE ||
	   TREE_CODE (ctree) == ENUMERAL_TYPE)
    {
    }
  else if (TREE_CODE (ctree) == BIND_EXPR)
    {
      output_fnbody (ctree);
    }
  else if (TREE_CODE (ctree) == VAR_DECL)
    {
      output_var (ctree);
    }
  else if (TREE_CODE (ctree) == FUNCTION_DECL && !DECL_BUILT_IN (ctree))
    {
    }
}

static void
symdb_tree_init (void)
{
}

static void
symdb_tree_tini (void)
{
}

/* }])> */
/* }])> */

/* cpp_callbacks <([{ */
static void
cb_file_change (cpp_reader * pfile, const struct line_map *map)
{
  if (map != NULL)
    {
      if (map->reason == LC_ENTER && map->included_from != -1)
	inc_trace (map->to_file, map->sysp);
      else if (map->reason == LC_LEAVE)
	inc_trace (NULL, false);
    }
  if (cpp.orig_file_change != NULL)
    cpp.orig_file_change (pfile, map);
}

static void
cb_macro_start (cpp_reader * pfile, const cpp_token * token,
		const cpp_hashnode * node)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  bool syshdr = false;
  bool fun_like = false;
  if (!(node->flags & NODE_BUILTIN))
    {
      cpp_macro *macro = node->value.macro;
      syshdr = macro->syshdr;
      fun_like = macro->fun_like;
    }
  if (pfile->context->prev == NULL)
    {
      gcc_assert (mo.sys_macro == 0);
      cpp.directive_type = EXPANDED_TOKEN;
      if (syshdr)
	{
	  mo.sys_macro = 1;
	  cpp.directive_type |= SYSHDR_FLAG;
	}
      /* The token is leader EXPANDED_TOKEN. */
      mo_append_token (token, cpp.directive_type);
    reinit:
      if (fun_like)
	{
	  /* Macro expansion occurs in normal chToken stream. To capture all
	   * expanded tokens which are masked by symdb_cpp_token by default, we
	   * must use cb_directive_token. */
	  cb->directive_token = cb_directive_token;
	  cb->macro_end_arg = cb_end_arg;
	}
      else			/* token following leader EXPANDED_TOKEN is MACRO_TOKEN */
	{
	  cpp.i_type = MACRO_TOKEN;
	  if (mo.sys_macro)
	    cpp.i_type |= SYSHDR_FLAG;
	}
      cb->macro_intern_expand = NULL;
    }
  else
    {
      if (syshdr)
	{
	  if (!mo.sys_macro)
	    {
	      cpp.i_type = MACRO_TOKEN | EXPANDED_TOKEN | SYSHDR_FLAG;
	      mo_append_token (token, cpp.i_type);
	      if (fun_like)
		{
		  cb->macro_intern_expand = cb_intern_expand;
		  cb->macro_end_arg = cb_end_arg;
		}
	      else
		cpp.i_type = MACRO_TOKEN | SYSHDR_FLAG;
	    }
	  mo.sys_macro++;
	}
      if (mo_maybe_cascaded ())
	{
	  if (fun_like)
	    mo.cascaded_func = true;
	  goto reinit;
	}
    }
}

static void
cb_end_arg (cpp_reader * pfile, bool cancel)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  if (cancel)
    {
      if (mo.sys_macro)
	cb->macro_intern_expand = NULL;
      mo_cancel ();
      return;
    }
  cb->macro_end_arg = NULL;
  cb->directive_token = NULL;
  cpp.i_type = MACRO_TOKEN;
  if (mo.sys_macro)
    cpp.i_type |= SYSHDR_FLAG;
}

static void
cb_intern_expand (struct cpp_reader *pfile, void *base, int count,
		  bool direct)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  cpp.directive_type = MACRO_TOKEN | EXPANDED_TOKEN | SYSHDR_FLAG;
  cb->macro_intern_expand = NULL;
  cpp_token_p token;
  cpp_token_p p, *pp;
  if (direct)
    p = (cpp_token_p) base;
  else
    pp = (cpp_token_p *) base;
  while (count--)
    {
      token = direct ? p++ : *pp++;
      if (token->type == CPP_PADDING)
	continue;
      mo_append_token (token, cpp.directive_type);
    }
  cpp.i_type = MACRO_TOKEN | SYSHDR_FLAG;
}

static void
cb_macro_end (cpp_reader * pfile)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  if (mo.sys_macro)
    mo.sys_macro--;
  if (!mo.sys_macro)
    cpp.i_type = MACRO_TOKEN;
  if (pfile->context->prev == NULL)
    {
      /* We've left macro expansion totally. */
      if (mo.cascaded_func)
	{
	  /* See document, we must cancel calling mo_flush. */
	  mo.cascaded_func = false;
	  return;
	}
      mo_flush ();
      cb->macro_end_arg = NULL;
      cb->directive_token = NULL;
      cpp.i_type = COMMON_TOKEN;
    }
}

static void
cb_comment (cpp_reader * pfile, const cpp_token * token)
{
  cpp.directive_type = ERASED_TOKEN;
  cb_directive_token (pfile, token);
}

static void
cb_start_directive (cpp_reader * pfile, const cpp_token * token)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  cpp.directive_type = ERASED_TOKEN;
  cb->directive_token = cb_directive_token;
  cb_directive_token (pfile, token);
}

static void
cb_end_directive (cpp_reader * pfile)
{
  cpp_callbacks *cb = cpp_get_callbacks (pfile);
  cb->directive_token = NULL;
}

static void
cb_directive_token (cpp_reader * pfile, const cpp_token * token)
{
  if (cpp.directive_type == ERASED_TOKEN)
    /* from cb_start_directive */
    output_chtoken (token, cpp.directive_type | token->type);
  else
    /* from cb_macro_start */
    mo_append_token (token, cpp.directive_type);
}

/* }])> */

/* plugin <([{ */
static void
symdb_init (const char *db_file)
{
  int nrow, ncolumn;
  char *error_msg, **result;

  if (!sqlite3_threadsafe ())
    {
      fprintf (stderr, "sqlite3 is compiled without thread-safe!\n");
      exit (1);
    }

  gbuf = dyn_string_new (1024);
  control_panel.db_file = dyn_string_new (256);
  control_panel.prj_dir = dyn_string_new (256);
  control_panel.cwd = dyn_string_new (256);
  dyn_string_copy_cstr (control_panel.cwd, getpwd ());
  dyn_string_copy_cstr (control_panel.db_file, db_file);

  db_error ((sqlite3_open_v2 (db_file, &db, SQLITE_OPEN_READWRITE, NULL)));
  db_error ((sqlite3_exec
	     (db, "begin exclusive transaction;", NULL, 0, NULL)));

  /* initilize control data. */
  db_error (sqlite3_get_table (db,
			       "select debug, projectRootPath from ProjectOverview;",
			       &result, &nrow, &ncolumn, &error_msg));
  control_panel.debug = strcmp (result[2], "true") == 0 ? true : false;
  control_panel.compare_cpp_token = control_panel.debug;
  dyn_string_copy_cstr (control_panel.prj_dir, result[3]);
  sqlite3_free_table (result);
}

static void
symdb_unit_init (void *gcc_data, void *user_data)
{
  int nrow, ncolumn;
  char *error_msg, **result;
  dyn_string_t chfile_id = dyn_string_new (8);
  dyn_string_t ifile_id = dyn_string_new (8);

  control_panel.compiled_file = dyn_string_new (256);
  file_full_path (main_input_filename, control_panel.compiled_file);
  append_file (chfile_id, ifile_id);
  control_panel.ifile_id = strtol (dyn_string_buf (ifile_id), NULL, 10);
  control_panel.chfile_id = strtol (dyn_string_buf (chfile_id), NULL, 10);

  dyn_string_delete (ifile_id);
  dyn_string_delete (chfile_id);

  symdb_token_init (parse_in);
  symdb_tree_init ();
}

static void
symdb_unit_tini (void *gcc_data, void *user_data)
{
  symdb_tree_tini ();
  symdb_token_tini ();

  dyn_string_delete (control_panel.compiled_file);
}

static void
symdb_tini (void *gcc_data, void *user_data)
{
  db_error ((sqlite3_exec (db, "end transaction;", NULL, 0, NULL)));
  sqlite3_close (db);

  dyn_string_delete (control_panel.cwd);
  dyn_string_delete (control_panel.prj_dir);
  dyn_string_delete (control_panel.db_file);
  dyn_string_delete (gbuf);
}

static void
symdb_cpp_token (void *gcc_data, void *user_data)
{
  cpp_token_p token = (cpp_token_p) gcc_data;
  if (token->type == CPP_EOF || token->type == CPP_PADDING)
    return;
  if (cpp.i_type == COMMON_TOKEN)
    {
      if (token->type != CPP_NAME)
	output_chtoken (token, cpp.i_type | token->type);
      else
	{
	  c_tok.last_common_token = token;
	  c_tok.common_type = cpp.i_type | token->type;
	}
    }
  else
    mo_append_token (token, cpp.i_type);
}

static void
symdb_c_token (void *gcc_data, void *user_data)
{
  c_token *token = gcc_data;
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
    keyword = token->keyword;
  keyword <<= C_TYPE_SHIFT;
  if (cpp.i_type == COMMON_TOKEN)
    {
      c_tok.common_type |= keyword;
      output_chtoken (c_tok.last_common_token, c_tok.common_type);
    }
  else
    {
      c_tok.last_macro_token->cpp_type |= keyword;
    }
}

int plugin_is_GPL_compatible;

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  printf ("%d, %s, %s\n", plugin_info->argc, plugin_info->argv->key,
	  plugin_info->argv->value);
  gcc_assert (plugin_info->argc == 1
	      && strcmp (plugin_info->argv[0].key, "dbfile") == 0);
  symdb_init (plugin_info->argv[0].value);
  register_callback ("symdb_start_unit", PLUGIN_START_UNIT, &symdb_unit_init,
		     NULL);
  register_callback ("symdb_finish_unit", PLUGIN_FINISH_UNIT,
		     &symdb_unit_tini, NULL);
  register_callback ("symdb_finish", PLUGIN_FINISH, &symdb_tini, NULL);
  register_callback ("symdb_cpp_token", PLUGIN_CPP_TOKEN, &symdb_cpp_token,
		     NULL);
  register_callback ("symdb_c_token", PLUGIN_C_TOKEN, &symdb_c_token, NULL);
  return 0;
}

/* }])> */
