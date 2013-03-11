/* vim: foldmarker=<([{,}])> foldmethod=marker
 * */
#include"include/dyn-string.h"
#include"include/libiberty.h"
#include<sys/stat.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sqlite3.h>

/* common <([{ */
static struct
{
  enum
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
  } flag;
  const char *str;
} def_flags[] =
{
[DEF_VAR].str = "DEF_VAR",
    [DEF_FUNC].str = "DEF_FUNC",
    [DEF_MACRO].str = "DEF_MACRO",
    [DEF_TYPEDEF].str = "DEF_TYPEDEF",
    [DEF_STRUCT].str = "DEF_STRUCT",
    [DEF_UNION].str = "DEF_UNION",
    [DEF_ENUM].str = "DEF_ENUM",
    [DEF_ENUM_MEMBER].str = "DEF_ENUM_MEMBER",
    [DEF_CALLED_FUNC].str = "DEF_CALLED_FUNC",
    [DEF_CALLED_POINTER].str = "DEF_CALLED_POINTER",};

enum
{
  CLAUSE_ITSELF = 1,
  SKIPPED,
  NO_SKIPPED,
};

static struct sqlite3 *db;

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
  return (long long) filestat.st_mtime;
}

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

int
usage (void)
{
  printf ("Usage:\n");
  printf ("    gs def filename definition\n");
  printf ("    gs caller -- definition\n");
  printf ("    gs callee structname definition\n");
  printf ("    gs ifdef filename fileoffset\n");
  printf ("    gs addsym/rmsym filename definition fileoffset\n");
  printf ("    gs falias member/fundecl symbol\n");
  printf ("    gs filedep/filedepee filename\n");
  printf ("    gs macro filename fileoffset\n");
  printf ("    gs macro\n");
  printf ("    gs sizeof struct\n");
  printf ("    gs offsetof struct member\n");
  printf ("    gs initdb prjpath\n");
  printf ("    gs vacuumdb prjpath\n");
  printf ("    gs relocate prjpath\n");
  printf ("    gs infodb prjpath\n");
  printf ("    Meanwhile, filename can be substituted by `--' (all files)\n");
  printf ("        `-' for anonymous struct name\n");
  printf
    ("    structname only is used when definition is a member-function-pointer\n");
  return EXIT_FAILURE;
}

/* }])> */

/* dep: deal with file-dependence. <([{ */
static struct
{
  dyn_string_t str;
  char fid[16];
} dep;

static const char *
dep_get_fid (const char *fn)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  if (strcmp (fn, "--") == 0)
    return NULL;
  dyn_string_copy_cstr (dep.str, "select id from chFile where name = '");
  dyn_string_append_cstr (dep.str, fn);
  dyn_string_append_cstr (dep.str, "';");
  db_error (sqlite3_get_table (db, dyn_string_buf (dep.str), &table,
			       &nrow, &ncolumn, &error_msg));
  assert (nrow <= 1 && ncolumn <= 1);
  if (nrow == 0 && ncolumn == 0)
    return NULL;
  strcpy (dep.fid, table[1]);
  sqlite3_free_table (table);
  return dep.fid;
}

static void
recursive_dependence (const char *fid, dyn_string_t result)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  dyn_string_copy_cstr (dep.str,
			"select hID from FileDependence where chFileID = ");
  dyn_string_append_cstr (dep.str, fid);
  dyn_string_append_cstr (dep.str, ";");
  db_error (sqlite3_get_table (db, dyn_string_buf (dep.str), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    {
      dyn_string_copy_cstr (dep.str, " ");
      dyn_string_append_cstr (dep.str, table[i]);
      dyn_string_append_cstr (dep.str, ",");
      if (strstr (dyn_string_buf (result), dyn_string_buf (dep.str)) != NULL)
	continue;
      dyn_string_append (result, dep.str);
      recursive_dependence (table[i], result);
    }
  sqlite3_free_table (table);
}

static void
dep_search_deplist (const char *root_fn, dyn_string_t list)
{
  const char *fid = dep_get_fid (root_fn);
  if (fid != NULL)
    {
      dyn_string_append_cstr (list, "fileID in ( ");
      dyn_string_append_cstr (list, fid);
      dyn_string_append_cstr (list, ",");
      recursive_dependence (fid, list);
      dyn_string_append_cstr (list, "-1)");
    }
  else
    dyn_string_append_cstr (list, "1 = 1");
}

static void
dep_init (void)
{
  dep.str = dyn_string_new (256);
}

static void
dep_tini (void)
{
  dyn_string_delete (dep.str);
}

/* }])> */

/* Do it. <([{ */
static dyn_string_t gbuf;
static dyn_string_t list;
static const char *
lltoa (long long i)
{
  static char buffer[32];
  sprintf (buffer, "%lld", i);
  return buffer;
}

static void
def (const char *root_fn, const char *def)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  dep_search_deplist (root_fn, list);
  dyn_string_copy_cstr (gbuf,
			"select fileName, fileoffset, flag from FileSymbol where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "' and flag != ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_CALLED_FUNC));
  dyn_string_append_cstr (gbuf, " and flag != ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_CALLED_POINTER));
  dyn_string_append_cstr (gbuf, " and ");
  dyn_string_append (gbuf, list);
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s\n", table[i * ncolumn + 0], table[i * ncolumn + 1],
	    def_flags[atoi (table[i * ncolumn + 2])].str);
  sqlite3_free_table (table);
}

static void
caller (const char *root_fn, const char *def)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  dyn_string_copy_cstr (gbuf,
			"select fileName, calleeFileOffset, calleeName, flag"
			" from CallRelationship where callerName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "';");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0],
	    table[i * ncolumn + 1], table[i * ncolumn + 2],
	    def_flags[atoi (table[i * ncolumn + 3])].str);
  sqlite3_free_table (table);
}

static void
callee (const char *struct_name, const char *def)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  dyn_string_copy_cstr (gbuf,
			"select distinct fileName, callerFileOffset, callerName, flag"
			" from CallRelationship where calleeName = '");
  if (strcmp (struct_name, "--") != 0)
    {
      if (strcmp (struct_name, "-") != 0)
	dyn_string_append_cstr (gbuf, struct_name);
      dyn_string_append_cstr (gbuf, "::");
    }
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "';");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0],
	    table[i * ncolumn + 1], table[i * ncolumn + 2],
	    def_flags[atoi (table[i * ncolumn + 3])].str);
  sqlite3_free_table (table);
}

static void
addsym (const char *root_fn, const char *def, const char *fileoffset)
{
  const char *fid = dep_get_fid (root_fn);
  if (fid == NULL)
    {
      long long mtime = get_mtime (root_fn);
      dyn_string_copy_cstr (gbuf, "insert into chFile values (NULL, '");
      dyn_string_append_cstr (gbuf, root_fn);
      dyn_string_append_cstr (gbuf, "', ");
      dyn_string_append_cstr (gbuf, lltoa (mtime));
      dyn_string_append_cstr (gbuf, ", 'false');");
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
      fid = dep_get_fid (root_fn);
    }
  dyn_string_copy_cstr (gbuf, "insert into Definition values (NULL, '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "', ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_VAR));
  dyn_string_append_cstr (gbuf, ", ");
  dyn_string_append_cstr (gbuf, fileoffset);
  dyn_string_append_cstr (gbuf, ");");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
  long long defid = sqlite3_last_insert_rowid (db);
  dyn_string_copy_cstr (gbuf, "insert into FileDefinition values (");
  dyn_string_append_cstr (gbuf, fid);
  dyn_string_append_cstr (gbuf, ", ");
  dyn_string_append_cstr (gbuf, lltoa (defid));
  dyn_string_append_cstr (gbuf, ", ");
  dyn_string_append_cstr (gbuf, lltoa (defid));
  dyn_string_append_cstr (gbuf, ");");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void
rmsym (const char *root_fn, const char *def, const char *fileoffset)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  const char *fid = dep_get_fid (root_fn);
  dyn_string_copy_cstr (gbuf,
			"select defID from FileSymbol where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "'");
  if (fid != NULL)
    {
      dyn_string_append_cstr (gbuf, " and fileName = '");
      dyn_string_append_cstr (gbuf, root_fn);
      dyn_string_append_cstr (gbuf, "'");
    }
  dyn_string_append_cstr (gbuf, " and fileoffset = ");
  dyn_string_append_cstr (gbuf, fileoffset);
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  if (nrow == 0)
    return;
  dyn_string_copy_cstr (list, "");
  for (int i = 1; i <= nrow; i++)
    {
      dyn_string_copy_cstr (list, table[i]);
      if (i != nrow)
	dyn_string_copy_cstr (list, ", ");
    }
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "delete from Definition where id in (");
  dyn_string_append (gbuf, list);
  dyn_string_append_cstr (gbuf, ");");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void
initdb (const char *path)
{
  dyn_string_copy_cstr (gbuf, "rm -f ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db");
  system (dyn_string_buf (gbuf));
  dyn_string_copy_cstr (gbuf, "sqlite3 -init init.sql ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db ''");
  system (dyn_string_buf (gbuf));
  char *str = lrealpath (path);
  dyn_string_copy_cstr (gbuf, "sqlite3 ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db ");
  dyn_string_append_cstr (gbuf, "\"update ProjectOverview set ");
  dyn_string_append_cstr (gbuf, "gccVersion = '<@a@>', ");
  dyn_string_append_cstr (gbuf, "pluginVersion = 'svn-<@b@>', ");
  dyn_string_append_cstr (gbuf, "sqliteVersion = '");
  dyn_string_append_cstr (gbuf, sqlite3_libversion ());
  dyn_string_append_cstr (gbuf, "', projectRootPath = '");
  dyn_string_append_cstr (gbuf, str);
  dyn_string_append_cstr (gbuf, "/';\"");
  system (dyn_string_buf (gbuf));
  free (str);
}

static void
vacuumdb (const char *path)
{
  dyn_string_copy_cstr (gbuf, "sqlite3 ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db \"vacuum;\"");
  system (dyn_string_buf (gbuf));
}

static void
ifdef (const char *root_fn, const char *offset)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  const char *fid = dep_get_fid (root_fn);
  if (fid == NULL)
    {
      printf ("File %s isn't compiled.", root_fn);
      return;
    }
  dyn_string_copy_cstr (gbuf, "select flag from Ifdef where fileID = ");
  dyn_string_append_cstr (gbuf, fid);
  dyn_string_append_cstr (gbuf, " and startOffset <= ");
  dyn_string_append_cstr (gbuf, offset);
  dyn_string_append_cstr (gbuf, " and endOffset > ");
  dyn_string_append_cstr (gbuf, offset);
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  int no_skipped = 0;
  for (int i = 1; i <= nrow; i++)
    {
      switch (atoi (table[i * ncolumn]))
	{
	case CLAUSE_ITSELF:
	  no_skipped++;
	  break;
	case SKIPPED:
	  break;
	case NO_SKIPPED:
	  no_skipped++;
	  break;
	}
    }
  if (no_skipped)
    printf ("no skipped");
  else
    printf ("skipped");
  sqlite3_free_table (table);
}

static void
falias (const char *type, const char *symbol)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  if (strcmp (type, "member") == 0)
    {
      dyn_string_copy_cstr (gbuf,
			    "select name, offset, "
			    " case when structName = '' then '-' else structName end, "
			    " funDecl"
			    " from FunpAlias, chFile "
			    " where id = fileID and member = '");
    }
  else if (strcmp (type, "fundecl") == 0)
    {
      dyn_string_copy_cstr (gbuf,
			    "select name, offset, "
			    " case when structName = '' then '-' else structName end, "
			    " member"
			    " from FunpAlias, chFile "
			    " where id = fileID and funDecl = '");
    }
  dyn_string_append_cstr (gbuf, symbol);
  dyn_string_append_cstr (gbuf, "' order by structName;");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0], table[i * ncolumn + 1],
	    table[i * ncolumn + 2], table[i * ncolumn + 3]);
  sqlite3_free_table (table);
}

static void
filedep (const char *file_name, int dep)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  const char *fid = dep_get_fid (file_name);
  if (fid == NULL)
    {
      printf ("File %s isn't compiled.", file_name);
      return;
    }

  dyn_string_copy_cstr (gbuf,
			"select name, sysHeader from chFile where id in (select ");
  if (dep)
    dyn_string_append_cstr (gbuf, " hID ");
  else
    dyn_string_append_cstr (gbuf, " chFileID ");
  dyn_string_append_cstr (gbuf, "from FileDependence where ");
  if (dep)
    dyn_string_append_cstr (gbuf, " chFileID = ");
  else
    dyn_string_append_cstr (gbuf, " hID = ");
  dyn_string_append_cstr (gbuf, fid);
  dyn_string_append_cstr (gbuf, ");");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s\n", table[i * ncolumn + 0]);
  sqlite3_free_table (table);
}

static void
macro (const char *file_name, const char *offset)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  if (file_name == NULL)
    {
      dyn_string_copy_cstr (gbuf,
			    "select name, defFileOffset, expandedTokens, macroTokens "
			    "from Macro, chFile where defFileID = id;");
      db_error (sqlite3_get_table
		(db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn,
		 &error_msg));
      for (int i = 1; i <= nrow; i++)
	{
	  printf ("def: %s, %s\n", table[i * ncolumn + 0],
		  table[i * ncolumn + 1]);
	  printf ("exp: %s\n", table[i * ncolumn + 2]);
	  printf ("mo: %s\n", table[i * ncolumn + 3]);
	}
      sqlite3_free_table (table);
    }
  else
    {
      const char *fid = dep_get_fid (file_name);
      if (fid == NULL)
	{
	  printf ("File %s isn't compiled.", file_name);
	  return;
	}
      db_error ((sqlite3_exec (db, "delete from Macro;", NULL, 0, NULL)));
      dyn_string_copy_cstr (gbuf,
			    "insert into Macro (letFileID, letOffset) values (");
      dyn_string_append_cstr (gbuf, fid);
      dyn_string_append_cstr (gbuf, ", ");
      dyn_string_append_cstr (gbuf, offset);
      dyn_string_append_cstr (gbuf, ");");
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
    }
}

static void
relocate (const char *path)
{
  dyn_string_copy_cstr (gbuf,
			"update ProjectOverview set projectRootPath = '");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/';");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void
offset_of (const char *struct_name, const char *member)
{
  int nrow, ncolumn;
  char *error_msg, **table;
  dyn_string_copy_cstr (gbuf,
			"select offset from Definition as a, Offsetof as b "
			" where a.id = b.structID " " and (flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_STRUCT));
  dyn_string_append_cstr (gbuf, " or flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_VAR));
  dyn_string_append_cstr (gbuf, " or flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_TYPEDEF));
  dyn_string_append_cstr (gbuf, " or flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_UNION));
  dyn_string_append_cstr (gbuf, ") and name = '");
  dyn_string_append_cstr (gbuf, struct_name);
  dyn_string_append_cstr (gbuf, "' and member = '");
  dyn_string_append_cstr (gbuf, member);
  dyn_string_append_cstr (gbuf, "';");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s\n", table[i * ncolumn + 0]);
  sqlite3_free_table (table);
}

static void
infodb (const char *path)
{
  char *str = lrealpath (path);
  system ("echo \"Current sqlite is:\"");
  system ("sqlite3 --version");
  dyn_string_copy_cstr (gbuf, "sqlite3 ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db \".du ProjectOverview\"");
  system (dyn_string_buf (gbuf));
  free (str);
}

/* }])> */

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;
  gbuf = dyn_string_new (1024);
  list = dyn_string_new (256);
  if (argc == 1)
    {
      ret = usage ();
      return -1;
    }
  if (argc == 3 && strcmp (argv[1], "initdb") == 0)
    {
      initdb (argv[2]);
      goto done;
    }
  if (argc == 3 && strcmp (argv[1], "vacuumdb") == 0)
    {
      vacuumdb (argv[2]);
      goto done;
    }
  if (argc == 3 && strcmp (argv[1], "infodb") == 0)
    {
      infodb (argv[2]);
      goto done;
    }
  db_error ((sqlite3_open_v2
	     ("gccsym.db", &db, SQLITE_OPEN_READWRITE, NULL)));
  db_error ((sqlite3_exec
	     (db, "begin exclusive transaction;", NULL, 0, NULL)));
  dep_init ();
  if (strcmp (argv[1], "def") == 0)
    def (argv[2], argv[3]);
  else if (strcmp (argv[1], "caller") == 0)
    caller (argv[2], argv[3]);
  else if (strcmp (argv[1], "callee") == 0)
    callee (argv[2], argv[3]);
  else if (strcmp (argv[1], "ifdef") == 0)
    ifdef (argv[2], argv[3]);
  else if (strcmp (argv[1], "addsym") == 0)
    addsym (argv[2], argv[3], argv[4]);
  else if (strcmp (argv[1], "rmsym") == 0)
    rmsym (argv[2], argv[3], argv[4]);
  else if (strcmp (argv[1], "falias") == 0)
    falias (argv[2], argv[3]);
  else if (strcmp (argv[1], "filedep") == 0)
    filedep (argv[2], 1);
  else if (strcmp (argv[1], "filedepee") == 0)
    filedep (argv[2], 0);
  else if (strcmp (argv[1], "macro") == 0)
    {
      if (argc == 2)
	macro (NULL, NULL);
      else
	macro (argv[2], argv[3]);
    }
  else if (strcmp (argv[1], "relocate") == 0)
    relocate (argv[2]);
  else if (strcmp (argv[1], "sizeof") == 0)
    offset_of (argv[2], "");
  else if (strcmp (argv[1], "offsetof") == 0)
    offset_of (argv[2], argv[3]);
  dep_tini ();
  db_error ((sqlite3_exec (db, "end transaction;", NULL, 0, NULL)));
  sqlite3_close (db);
done:
  dyn_string_delete (list);
  dyn_string_delete (gbuf);
  return ret;
}
