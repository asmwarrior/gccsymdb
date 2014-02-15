/* vim: foldmarker=<([{,}])> foldmethod=marker
 * */
#include"include/dyn-string.h"
#include"include/libiberty.h"
#include<sys/stat.h>
#include<time.h>
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
    DEF_ENUMERATOR,
    DEF_USER,
    DEF_CLASS,
    DEF_METHOD,
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
    [DEF_ENUMERATOR].str = "DEF_ENUMERATOR",
    [DEF_USER].str = "DEF_USER",
    [DEF_CLASS].str = "DEF_CLASS",[DEF_METHOD].str = "DEF_METHOD",};

enum
{
  CLAUSE_ITSELF = 1,
  SKIPPED,
  NO_SKIPPED,
};

static struct
{
  enum
  {
    ACCESS_READ = 1,
    ACCESS_WRITE = 2,
    ACCESS_ADDR = 4,
    ACCESS_POINTER_READ = 8,
    ACCESS_POINTER_WRITE = 16,
  } flag;
  const char *str;
} access_flags[] =
{
[ACCESS_READ].str = "r",
    [ACCESS_WRITE].str = "w",
    [ACCESS_ADDR].str = "&",
    [ACCESS_POINTER_READ].str = "R",[ACCESS_POINTER_WRITE].str = "W",};

static struct sqlite3 *db;
int nrow, ncolumn;
char *error_msg, **table;

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
  printf
    ("       filename isn't null means searching definition by the file and its dependence files.\n");
  printf ("       filename is - means all project files.\n");
  printf ("    gs callee function\n");
  printf ("    gs ifdef filename fileoffset\n");
  printf ("    gs addsym/rmsym filename definition fileoffset\n");
  printf ("       to rmsym filename is - means all project files.\n");
  printf ("    gs falias mfp\n");
  printf ("    gs faccessv var\n");
  printf ("    gs faccessv-expansion struct field\n");
  printf ("    gs filedep/filedepee filename\n");
  printf ("    gs macro filename fileoffset\n");
  printf ("    gs macro\n");
  printf ("    gs sizeof struct\n");
  printf ("    gs offsetof struct member\n");
  printf ("    gs infodb\n");
  printf ("    gs initdb prjpath user-defined-info\n");
  printf ("    gs enddb prjpath\n");
  printf ("    gs relocate prjpath\n");
  printf ("    gs ctrl XXX YYY\n");
  printf ("       ctrl canUpdateFile t/f\n");
  printf ("       ctrl userDefInfo xxx\n");
  printf ("       ctrl faccessv t/f\n");
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
  dyn_string_copy_cstr (dep.str,
			"select hID from FileDependence where chID = ");
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
  dep_search_deplist (root_fn, list);
  dyn_string_copy_cstr (gbuf,
			"select fileName, fileOffset, flag from FileSymbol where defName = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "' and ");
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
callee (const char *def)
{
  dyn_string_copy_cstr (gbuf,
			"select distinct * from ( "
			"select "
			"f.name, d.fileOffset, d.name, fa.mfp "
			"from "
			"FunctionCall as fc "
			"left join FunctionAlias fa on fc.name = fa.mfp "
			"left join Definition as d on fc.callerID = d.id "
			"left join chFile f on d.fileID = f.id "
			"where fa.fundecl = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf,
			  "' "
			  "union all "
			  "select "
			  "f.name, d.fileOffset, d.name, '-' "
			  "from "
			  "FunctionCall as fc "
			  "left join Definition as d on fc.callerID = d.id "
			  "left join chFile f on d.fileID = f.id "
			  "where fc.name = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "');");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0],
	    table[i * ncolumn + 1], table[i * ncolumn + 2],
	    table[i * ncolumn + 3]);
  sqlite3_free_table (table);
}

static void
addsym (const char *root_fn, const char *def, const char *fileoffset)
{
  if (strcmp (root_fn, "-") == 0)
    return;
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
  dyn_string_copy_cstr (gbuf, "insert into Definition values (NULL, ");
  dyn_string_append_cstr (gbuf, fid);
  dyn_string_append_cstr (gbuf, ", '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "', ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_USER));
  dyn_string_append_cstr (gbuf, ", ");
  dyn_string_append_cstr (gbuf, fileoffset);
  dyn_string_append_cstr (gbuf, ");");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void
rmsym (const char *root_fn, const char *def, const char *fileoffset)
{
  const char *fid = dep_get_fid (root_fn);
  dyn_string_copy_cstr (gbuf, "delete from Definition where name = '");
  dyn_string_append_cstr (gbuf, def);
  dyn_string_append_cstr (gbuf, "'");
  if (fid != NULL)
    {
      dyn_string_append_cstr (gbuf, " and fileID = ");
      dyn_string_append_cstr (gbuf, fid);
    }
  dyn_string_append_cstr (gbuf, " and fileoffset = ");
  dyn_string_append_cstr (gbuf, fileoffset);
  dyn_string_append_cstr (gbuf, ";");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

static void
initdb (const char *path, const char *user_def)
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
  dyn_string_append_cstr (gbuf, "', userDefInfo = '");
  dyn_string_append_cstr (gbuf, user_def);
  dyn_string_append_cstr (gbuf, "', projectRootPath = '");
  dyn_string_append_cstr (gbuf, str);
  dyn_string_append_cstr (gbuf, "/', initdbTime = ");
  dyn_string_append_cstr (gbuf, lltoa (time (NULL)));
  dyn_string_append_cstr (gbuf, ";");
  /* Some temporary indices to accelerate database establishment. */
  dyn_string_append_cstr (gbuf,
			  "create index idx_tempFileDependence on FileDependence"
			  "(chID, hID, offset);"
			  "create index idx_tempDefinition on Definition"
			  "(fileID, name, flag, fileOffset);"
			  "create index idx_tempFunctionCall on FunctionCall"
			  "(callerID, fileID, name, fileOffset);"
			  "create index idx_tempFunctionAccess on FunctionAccess"
			  "(funcID, fileID, name, fileOffset);"
			  "create index idx_tempFunctionPattern on FunctionPattern"
			  "(funcID, name, flag);"
			  "create index idx_tempIfdef on Ifdef"
			  "(fileID, flag, startOffset, endOffset);"
			  "create index idx_tempFunctionAlias on FunctionAlias"
			  "(fileID, mfp, funDecl, offset);"
			  "create index idx_tempOffsetof on Offsetof"
			  "(structID);");
  dyn_string_append_cstr (gbuf, "\"");
  system (dyn_string_buf (gbuf));
  free (str);
}

static void
enddb (const char *path)
{
  dyn_string_copy_cstr (gbuf, "sqlite3 ");
  dyn_string_append_cstr (gbuf, path);
  dyn_string_append_cstr (gbuf, "/gccsym.db ");
  dyn_string_append_cstr (gbuf, "\"update ProjectOverview set ");
  dyn_string_append_cstr (gbuf, "enddbTime = ");
  dyn_string_append_cstr (gbuf, lltoa (time (NULL)));
  dyn_string_append_cstr (gbuf, ";");
  dyn_string_append_cstr (gbuf,
			  "drop index idx_tempOffsetof;"
			  "drop index idx_tempFunctionAlias;"
			  "drop index idx_tempIfdef;"
			  "drop index idx_tempFunctionPattern;"
			  "drop index idx_tempFunctionAccess;"
			  "drop index idx_tempFunctionCall;"
			  "drop index idx_tempDefinition;"
			  "drop index idx_tempFileDependence;");
  dyn_string_append_cstr (gbuf, "vacuum;\"");
  system (dyn_string_buf (gbuf));
}

static void
ifdef (const char *root_fn, const char *offset)
{
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
falias (const char *mfp)
{
  dyn_string_copy_cstr (gbuf,
			"select distinct funcFileName, funcOffset, mfp, funcName "
			"from MfpJumpto where mfp ");
  if (strstr (mfp, "::") != NULL)
    {
      dyn_string_append_cstr (gbuf, " = '");
      dyn_string_append_cstr (gbuf, mfp);
      dyn_string_append_cstr (gbuf, "';");
    }
  else
    {
      dyn_string_append_cstr (gbuf, " like '%::");
      dyn_string_append_cstr (gbuf, mfp);
      dyn_string_append_cstr (gbuf, "';");
    }
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s %s\n", table[i * ncolumn + 0],
	    table[i * ncolumn + 1], table[i * ncolumn + 2],
	    table[i * ncolumn + 3]);
  sqlite3_free_table (table);
}

static void
faccessv_output (void)
{
  const char *a, *b, *c, *d;
  if (nrow != 0)
    {
      a = table[5];
      b = table[6];
      c = table[7];
      d = table[8];
      int flag = 0;
      for (int i = 1; i <= nrow; i++)
	{
	  if (strcmp (a, table[i * ncolumn + 0]) == 0
	      && strcmp (b, table[i * ncolumn + 1]) == 0
	      && strcmp (c, table[i * ncolumn + 2]) == 0
	      && strcmp (d, table[i * ncolumn + 3]) == 0)
	    flag |= atoi (table[i * ncolumn + 4]);
	  else
	    {
	      printf ("%s %s %s %s ", a, b, c, d);
	      if (flag & ACCESS_READ)
		printf ("%s", access_flags[ACCESS_READ].str);
	      if (flag & ACCESS_WRITE)
		printf ("%s", access_flags[ACCESS_WRITE].str);
	      if (flag & ACCESS_ADDR)
		printf ("%s", access_flags[ACCESS_ADDR].str);
	      if (flag & ACCESS_POINTER_READ)
		printf ("%s", access_flags[ACCESS_POINTER_READ].str);
	      if (flag & ACCESS_POINTER_WRITE)
		printf ("%s", access_flags[ACCESS_POINTER_WRITE].str);
	      printf ("\n");
	      a = table[i * ncolumn + 0];
	      b = table[i * ncolumn + 1];
	      c = table[i * ncolumn + 2];
	      d = table[i * ncolumn + 3];
	      flag = atoi (table[i * ncolumn + 4]);
	    }
	}
      printf ("%s %s %s %s ", a, b, c, d);
      if (flag & ACCESS_READ)
	printf ("%s", access_flags[ACCESS_READ].str);
      if (flag & ACCESS_WRITE)
	printf ("%s", access_flags[ACCESS_WRITE].str);
      if (flag & ACCESS_ADDR)
	printf ("%s", access_flags[ACCESS_ADDR].str);
      if (flag & ACCESS_POINTER_READ)
	printf ("%s", access_flags[ACCESS_POINTER_READ].str);
      if (flag & ACCESS_POINTER_WRITE)
	printf ("%s", access_flags[ACCESS_POINTER_WRITE].str);
      printf ("\n");
    }
}

static void
faccessv (const char *var)
{
  dyn_string_copy_cstr (gbuf,
			"select FuncFileName, FuncFileOffset, FuncName, "
			"VarName, VarAccessFlag "
			"from AccessRelationship where VarName like '");
  dyn_string_append_cstr (gbuf, var);
  dyn_string_append_cstr (gbuf,
			  "%' order by FuncFileName, FuncFileOffset, FuncName, VarName;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  faccessv_output ();
  sqlite3_free_table (table);
}

static void
faccessv_expansion (const char *strut, const char *field)
{
  if (strut == NULL)
    {
      dyn_string_copy_cstr (gbuf,
			    "select FuncFileName, FuncFileOffset, FuncName, "
			    "VarName, VarAccessFlag "
			    "from AccessRelationship order by FuncFileName, FuncFileOffset, FuncName, VarName;");
      db_error (sqlite3_get_table
		(db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn,
		 &error_msg));
      faccessv_output ();
      sqlite3_free_table (table);
    }
  else
    {
      dyn_string_copy_cstr (gbuf,
			    "update ProjectOverview set faccessvStruct = '");
      dyn_string_append_cstr (gbuf, strut);
      dyn_string_append_cstr (gbuf, "', faccessvField = '");
      dyn_string_append_cstr (gbuf, field);
      dyn_string_append_cstr (gbuf, "';");
      db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
    }
}

static void
filedep (const char *file_name, int dep)
{
  const char *fid = dep_get_fid (file_name);
  if (fid == NULL)
    {
      printf ("File %s isn't compiled.\n", file_name);
      return;
    }

  dyn_string_copy_cstr (gbuf,
			"select name, offset, sysHeader from FileDependence "
			"left join chFile on ");
  if (dep)
    dyn_string_append_cstr (gbuf, " hID ");
  else
    dyn_string_append_cstr (gbuf, " chID ");
  dyn_string_append_cstr (gbuf, " = id where ");
  if (dep)
    dyn_string_append_cstr (gbuf, " chID = ");
  else
    dyn_string_append_cstr (gbuf, " hID = ");
  dyn_string_append_cstr (gbuf, fid);
  dyn_string_append_cstr (gbuf, " order by offset;");
  db_error (sqlite3_get_table (db, dyn_string_buf (gbuf), &table,
			       &nrow, &ncolumn, &error_msg));
  for (int i = 1; i <= nrow; i++)
    printf ("%s %s %s\n", table[i * ncolumn + 0], table[i * ncolumn + 1],
	    table[i * ncolumn + 2]);
  sqlite3_free_table (table);
}

static void
macro (const char *file_name)
{
  if (file_name == NULL)
    {
      dyn_string_copy_cstr (gbuf,
			    "select f1.name, fileOffset, "
			    "case when defFileID = -1 then '<command-line>' "
			    "else f2.name END, defFileOffset, "
			    "expandedTokens, macroTokens "
			    "from Macro "
			    "left join chFile as f1 on fileID = f1.id "
			    "left join chFile as f2 on defFileID = f2.id;");
      db_error (sqlite3_get_table
		(db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn,
		 &error_msg));
      for (int i = 1; i <= nrow; i++)
	{
	  printf ("macro occurs: %s, %s\n", table[i * ncolumn + 0],
		  table[i * ncolumn + 1]);
	  printf ("macro def: %s, %s\n", table[i * ncolumn + 2],
		  table[i * ncolumn + 3]);
	  printf ("macro tokens: %s\n", table[i * ncolumn + 4]);
	  printf ("macro-replaced tokens: %s\n", table[i * ncolumn + 5]);
	  printf ("\n");
	}
      sqlite3_free_table (table);
    }
  else
    {
      dyn_string_copy_cstr (gbuf, "update ProjectOverview set macroFile = '");
      dyn_string_append_cstr (gbuf, file_name);
      dyn_string_append_cstr (gbuf, "';");
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
  dyn_string_copy_cstr (gbuf,
			"select offset from "
			"Offsetof left join Definition on structID = id "
			" where (flag = ");
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
infodb (void)
{
  char fbuf[32];
  FILE *ftmp = popen ("sqlite3 --version", "r");
  fbuf[fread (fbuf, sizeof (char), sizeof (fbuf), ftmp)] = '\0';
  pclose (ftmp);
  printf ("Current sqlite is: %s\n", fbuf);
  dyn_string_copy_cstr (gbuf, "select * from ProjectOverview;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  for (int i = 0; i < nrow + 1; i++)
    {
      for (int j = 0; j < ncolumn; j++)
	printf ("%s,", table[j + i * ncolumn]);
      printf ("\n");
    }
  sqlite3_free_table (table);
  printf ("\n");

  dyn_string_copy_cstr (gbuf, "select count(*) from Definition;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("Definition count is %s\n", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select count(*) from chFile;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("File count is %s\n", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf,
			"select a.name as main, b.name as secondary, offset "
			"from chFile as a, chFile as b, FileDependence "
			"where hID = b.id and b.name like '%.c' and chID = a.id;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  if (nrow != 0)
    {
      printf ("Caution: .c files are included by other files\n");
      for (int i = 0; i < nrow + 1; i++)
	{
	  for (int j = 0; j < ncolumn; j++)
	    printf ("%s,", table[j + i * ncolumn]);
	  printf ("\n");
	}
    }
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select name from chFile, FileDependence "
			"where hID = chID and chID = id;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  if (nrow != 0)
    {
      printf ("Caution: files try to include itself\n");
      for (int i = 0; i < nrow + 1; i++)
	{
	  for (int j = 0; j < ncolumn; j++)
	    printf ("%s,", table[j + i * ncolumn]);
	  printf ("\n");
	}
    }
  sqlite3_free_table (table);
  printf ("\n");

  dyn_string_copy_cstr (gbuf, "select count(*) from Definition "
			"where flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_VAR));
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("Global variable count is %s\n", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select fileName, fileOffset, defName "
			"from FileSymbol where defName in "
			"(select name from definition where flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_VAR));
  dyn_string_append_cstr (gbuf,
			  " group by (name) having count(name) > 1 order by count(name));");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  if (nrow != 0)
    {
      printf ("Global variable duplication overview\n");
      for (int i = 0; i < nrow + 1; i++)
	{
	  for (int j = 0; j < ncolumn; j++)
	    printf ("%s,", table[j + i * ncolumn]);
	  printf ("\n");
	}
    }
  sqlite3_free_table (table);
  printf ("\n");

  dyn_string_copy_cstr (gbuf, "select count(*) from Definition "
			"where flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_FUNC));
  dyn_string_append_cstr (gbuf, ";");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("Function count is %s\n", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select fileName, fileOffset, defName "
			"from FileSymbol where defName in "
			"(select name from definition where flag = ");
  dyn_string_append_cstr (gbuf, lltoa (DEF_FUNC));
  dyn_string_append_cstr (gbuf,
			  " group by (name) having count(name) > 1 order by count(name));");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  if (nrow != 0)
    {
      printf ("Function duplication overview\n");
      for (int i = 0; i < nrow + 1; i++)
	{
	  for (int j = 0; j < ncolumn; j++)
	    printf ("%s,", table[j + i * ncolumn]);
	  printf ("\n");
	}
    }
  sqlite3_free_table (table);
  printf ("\n");

  dyn_string_copy_cstr (gbuf, "select count(*) from ( "
			"select * from FunctionCall group by callerID);");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("%s functions call ", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select count(*) from FunctionCall;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("%s functions/mfps. Meanwhile ", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf,
			"select count(*) from FunctionCall where name like '%::%';");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("%s mfps.\n", table[1]);
  sqlite3_free_table (table);
  printf ("\n");

  dyn_string_copy_cstr (gbuf, "select count(*) from ( "
			"select * from FunctionAccess group by funcID);");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("%s functions access ", table[1]);
  sqlite3_free_table (table);
  dyn_string_copy_cstr (gbuf, "select count(*) from FunctionAccess;");
  db_error (sqlite3_get_table
	    (db, dyn_string_buf (gbuf), &table, &nrow, &ncolumn, &error_msg));
  printf ("%s variables.", table[1]);
  sqlite3_free_table (table);
  printf ("\n");
}

static void
ctrl (const char *parameter, const char *value)
{
  dyn_string_copy_cstr (gbuf, "update ProjectOverview set ");
  if (strcmp (parameter, "canUpdateFile") == 0)
    {
      dyn_string_append_cstr (gbuf, parameter);
      dyn_string_append_cstr (gbuf, " = '");
      dyn_string_append_cstr (gbuf, value);
      dyn_string_append_cstr (gbuf, "'");
    }
  else if (strcmp (parameter, "userDefInfo") == 0)
    {
      dyn_string_append_cstr (gbuf, parameter);
      dyn_string_append_cstr (gbuf, " = '");
      dyn_string_append_cstr (gbuf, value);
      dyn_string_append_cstr (gbuf, "'");
    }
  else if (strcmp (parameter, "faccessv") == 0)
    {
      dyn_string_append_cstr (gbuf, parameter);
      dyn_string_append_cstr (gbuf, " = '");
      dyn_string_append_cstr (gbuf, value);
      dyn_string_append_cstr (gbuf, "'");
    }
  dyn_string_append_cstr (gbuf, ";");
  db_error ((sqlite3_exec (db, dyn_string_buf (gbuf), NULL, 0, NULL)));
}

/* }])> */

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;
  gbuf = dyn_string_new (1024);
  list = dyn_string_new (256);
  if (argc == 1
      || (argc == 2
	  && (strcmp (argv[1], "--help") == 0 || strcmp (argv[1], "-h") == 0
	      || strcmp (argv[1], "help") == 0)))
    {
      ret = usage ();
      return -1;
    }
  if (strcmp (argv[1], "initdb") == 0)
    {
      initdb (argv[2], argc == 3 ? "" : argv[3]);
      goto done;
    }
  if (strcmp (argv[1], "enddb") == 0)
    {
      enddb (argv[2]);
      goto done;
    }
  db_error ((sqlite3_open_v2
	     ("gccsym.db", &db, SQLITE_OPEN_READWRITE, NULL)));
  db_error ((sqlite3_exec
	     (db, "begin exclusive transaction;", NULL, 0, NULL)));
  dep_init ();
  if (strcmp (argv[1], "def") == 0)
    def (argv[2], argv[3]);
  else if (strcmp (argv[1], "callee") == 0)
    callee (argv[2]);
  else if (strcmp (argv[1], "ifdef") == 0)
    ifdef (argv[2], argv[3]);
  else if (strcmp (argv[1], "addsym") == 0)
    addsym (argv[2], argv[3], argv[4]);
  else if (strcmp (argv[1], "rmsym") == 0)
    rmsym (argv[2], argv[3], argv[4]);
  else if (strcmp (argv[1], "falias") == 0)
    falias (argv[2]);
  else if (strcmp (argv[1], "faccessv") == 0)
    faccessv (argv[2]);
  else if (strcmp (argv[1], "faccessv-expansion") == 0)
    {
      if (argc == 2)
	faccessv_expansion (NULL, NULL);
      else
	faccessv_expansion (argv[2], argv[3]);
    }
  else if (strcmp (argv[1], "filedep") == 0)
    filedep (argv[2], 1);
  else if (strcmp (argv[1], "filedepee") == 0)
    filedep (argv[2], 0);
  else if (strcmp (argv[1], "macro") == 0)
    {
      if (argc == 2)
	macro (NULL);
      else
	macro (argv[2]);
    }
  else if (strcmp (argv[1], "relocate") == 0)
    relocate (argv[2]);
  else if (strcmp (argv[1], "sizeof") == 0)
    offset_of (argv[2], "");
  else if (strcmp (argv[1], "offsetof") == 0)
    offset_of (argv[2], argv[3]);
  else if (strcmp (argv[1], "infodb") == 0)
    infodb ();
  else if (strcmp (argv[1], "ctrl") == 0)
    ctrl (argv[2], argv[3]);
  dep_tini ();
  db_error ((sqlite3_exec (db, "end transaction;", NULL, 0, NULL)));
  sqlite3_close (db);
done:
  dyn_string_delete (list);
  dyn_string_delete (gbuf);
  return ret;
}
