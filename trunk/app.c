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
    [DEF_CALLED_FUNC].str = "DEF_CALLED_FUNC",};

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
  printf ("    gs def/caller/callee filename definition\n");
  printf ("    gs addsym/rmsym filename definition fileoffset\n");
  printf ("    gs initdb prjpath\n");
  printf ("    gs vacuumdb prjpath\n");
  printf ("    Meanwhile, filename can be substituted by `--' (all files)\n");
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
			"select fileName, fileoffset, flag from Helper where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "' and flag != ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_CALLED_FUNC));
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

  const char *fid = dep_get_fid (root_fn);
  dyn_string_copy_cstr (gbuf,
			"select fileName, fileoffset, defName, flag from Helper where defID in (");
  dyn_string_append_cstr (gbuf,
			  "select callee from DefinitionRelationship where caller in (");
  dyn_string_append_cstr (gbuf, "select defID from Helper where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "' and flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_FUNC));
  if (fid != NULL)
    {
      dyn_string_append_cstr (gbuf, " and fileID = ");
      dyn_string_append_cstr (gbuf, fid);
    }
  dyn_string_append_cstr (gbuf, "));");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0],
	    table[i * ncolumn + 1], table[i * ncolumn + 2],
	    def_flags[atoi (table[i * ncolumn + 3])].str);
  sqlite3_free_table (table);
}

static void
callee (const char *root_fn, const char *def)
{
  int nrow, ncolumn;
  char *error_msg, **table;

  const char *fid = dep_get_fid (root_fn);
  dyn_string_copy_cstr (gbuf,
			"select fileName, fileoffset, defName, flag from Helper where defID in (");
  dyn_string_append_cstr (gbuf,
			  "select caller from DefinitionRelationship where callee in (");
  dyn_string_append_cstr (gbuf, "select defID from Helper where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "' and flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_CALLED_FUNC));
  if (fid != NULL)
    {
      dyn_string_append_cstr (gbuf, " and fileID = ");
      dyn_string_append_cstr (gbuf, fid);
    }
  dyn_string_append_cstr (gbuf, "));");
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
  dyn_string_copy_cstr (gbuf, "select defID from Helper where defName = '");
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
  dyn_string_copy_cstr (gbuf, "echo \"update ProjectOverview set ");
  dyn_string_append_cstr (gbuf, "gccVersion = '<@a@>', ");
  dyn_string_append_cstr (gbuf, "pluginVersion = 'svn-<@b@>', ");
  dyn_string_append_cstr (gbuf, "projectRootPath = '");
  dyn_string_append_cstr (gbuf, str);
  dyn_string_append_cstr (gbuf, "/';\" | sqlite3 -batch ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db");
  system (dyn_string_buf (gbuf));
  free (str);
}

static void
vacuumdb (const char *path)
{
  dyn_string_copy_cstr (gbuf, "echo 'vacuum;' | sqlite3 -batch  ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db");
  system (dyn_string_buf (gbuf));
}

/* }])> */

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;
  gbuf = dyn_string_new (1024);
  list = dyn_string_new (256);
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
  db_error ((sqlite3_open_v2
	     ("gccsym.db", &db, SQLITE_OPEN_READWRITE, NULL)));
  db_error ((sqlite3_exec
	     (db, "begin exclusive transaction;", NULL, 0, NULL)));
  dep_init ();
  if (argc < 4)
    ret = usage ();
  else if (strcmp (argv[1], "def") == 0)
    def (argv[2], argv[3]);
  else if (strcmp (argv[1], "caller") == 0)
    caller (argv[2], argv[3]);
  else if (strcmp (argv[1], "callee") == 0)
    callee (argv[2], argv[3]);
  else if (strcmp (argv[1], "addsym") == 0)
    {
      if (strcmp (argv[2], "--") == 0)
	ret = usage ();
      else
	addsym (argv[2], argv[3], argv[4]);
    }
  else if (strcmp (argv[1], "rmsym") == 0)
    rmsym (argv[2], argv[3], argv[4]);
  else
    ret = usage ();
  dep_tini ();
  db_error ((sqlite3_exec (db, "end transaction;", NULL, 0, NULL)));
  sqlite3_close (db);
done:
  dyn_string_delete (list);
  dyn_string_delete (gbuf);
  return ret;
}
