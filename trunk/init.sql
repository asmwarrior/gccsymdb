-- vim: foldmarker=<([{,}])> foldmethod=marker
create table ProjectOverview (
	dbVersion text,
	pluginVersion text,
	gccVersion text,
	sqliteVersion text,
	projectRootPath text,
	initdbTime bigint, -- unit is second.
	enddbTime bigint,
	-- User-defined info, such as the svn revision of code.
	userDefInfo text,
	-- plugin control parameters.
	canUpdateFile boolean,
	faccessv boolean
);
insert into ProjectOverview values ("1.0", "X.0", "4.6.X", "3.6.X", "/project/root/path/", 0, 0, "user data", 't', 't');

-- chFile is the root of all tables, see trigger fold for more, delete the table will delete all things in the file.
-- But currently, macro feature doesn't depend on chFile table, which is transient.

-- File tables <([{
create table chFile (
	id integer primary key autoincrement,
	name text,
	mtime bigint,
	sysHeader boolean
);

-- a c/h file and its direct-including .h.
create table FileDependence (
	chID integer references chFile (id),
	hID integer references chFile (id),
	offset integer -- `#include' token file-offset.
);
-- }])>

-- Definition tables
create table Definition (
	id integer primary key autoincrement,
	fileID integer references chFile (id),
	name text,
	flag integer,
	fileOffset integer
);

-- fun-call-fun, or fun-call-mfp feature, abbr. f-call-f.
create table FunctionCall (
	callerID integer references Definition (id),
	fileID integer references chFile (id), -- note, a function body can be across multiple files, so we need the field too.
	name text, -- if name is like `XX::YY', that is, include '::', it's mfp call, otherwise, function call.
	fileOffset integer
);

-- fun-access-var <([{
create table FunctionAccess (
	funcID integer references Definition (id),
	fileID integer references chFile (id), -- note, a function body can be across multiple files, so we need the field too.
	name text,
	flag integer,
	fileOffset integer
);

-- When a function body has a pattern
--     void set_gv(int i) { gv = i; }
-- These functions will be used by fun-access-var feature just like it's expanded at the called position.
create table FunctionPattern (
	funcID integer references Definition (id),
	name text,
	flag integer
);
-- }])>

-- The table stores the information of which lines are skipped by such like `ifdef/if'.
create table Ifdef (
	fileID integer references chFile (id),
	flag integer,
	startOffset integer,
	endOffset integer
);

-- For function alias feature.
-- The table is used to store where a mfp is assigned.
create table FunpAlias (
	fileID integer references chFile (id),
	mfp text, -- syntax pattern: structname::mfp
	funDecl text,
	offset integer
);

-- For macro feature of `./gs macro XX'.
create table Macro (
	-- user request.
	letFileID integer,
	letOffset integer,

	-- result.
	defFileID integer,
	defFileOffset integer,
	expandedTokens text,
	macroTokens text
);

-- For offsetof feature.
-- If member field is '', offset represents the size of the struct.
create table Offsetof (
	structID integer references Definition (id),
	member text,
	offset integer
);

-- Useful views, search them instead of original tables if possible <([{
-- Search file-definition pair by defname.
create view FileSymbol as
select
	f.id as fileID, f.name as fileName, fileOffset, d.id as defID, d.name as defName, flag
from
	Definition as d
	left join chFile as f on d.fileID = f.id
;

-- Search function-call relationship, query column calleeName if you only know func-decl.
-- Note: It's possible that FunpAlias hasn't a mfp record while the mfp exists in FunctionCall -- your function calls a mfp but forget to assigns it to some func-decl?
-- Note: the second query actually includes all rows in FunctionCall. It depends on the fact that mfp data of FunctionCall has a pattern '*::XX', it's used to query direct call relationship.
-- Note: sqlite-3.6.20 hasn't a good query plan in the view, if the view only includes either the first or second query, it will use Alias2 and CalleeName indices individually. That's why app.c:callee() uses its code based on this.
create view CallRelationship as
select
	d.fileID as callerFileID, f.name as callerFileName, d.fileOffset as callerFileOffset, d.id as callerID, d.name as callerName,
	fc.fileID as calleePosFileID, f2.name as calleePosFileName, fc.fileOffset as calleePos, fa.fundecl as calleeName, fa.mfp as mfp
from
	FunctionCall as fc
	left join FunpAlias fa on fc.name = fa.mfp
	left join Definition as d on fc.callerID = d.id
		left join chFile f on d.fileID = f.id
	left join chFile f2 on fc.fileID = f2.id
union all
select
	d.fileID as callerFileID, f.name as callerFileName, d.fileOffset as callerFileOffset, d.id as callerID, d.name as callerName,
	fc.fileID as calleePosFileID, f2.name as calleePosFileName, fc.fileOffset as calleePos, fc.name as calleeName, '-' as mfp
from
	FunctionCall as fc
	left join Definition as d on fc.callerID = d.id
		left join chFile f on d.fileID = f.id
	left join chFile f2 on fc.fileID = f2.id
;

-- Search mfp alias and its position by mfp.
-- Note: It's possible that FunpAlias has a record but you can't find it in Defintion, maybe you assign the mfp to an assemble-entry.
create view MfpJumpto as
select
	fa.fileID as mfpFileID, f.name as mfpFileName, fa.offset as mfpOffset, fa.mfp as mfp,
	d.fileID as funcFileID, f2.name as funcFileName, d.fileOffset as funcOffset, fa.funDecl as funcName
from
	FunpAlias as fa
	left join Definition as d on fa.funDecl = d.Name
		left join chFile as f2 on d.fileID = f2.id
	left join chFile as f on fa.fileID = f.id
where
	d.flag = 2; -- DEF_FUNC
;

-- Search variable access by variable name.
-- NOte: since query clause of the view in most case is affixing `where VarName like 'var%'', and `like' operator doesn't use index in sqlite, so the view hasn't optimization solution.
create view AccessRelationship as
select
	f2.id as FuncFileID, f2.name as FuncFileName, d.fileOffset as FuncFileOffset, d.name as FuncName,
	f.id as ExprFileID, f.name as ExprFileName, fa.fileOffset as ExprFileOffset,
	fa.name as VarName,
	fa.flag as VarAccessFlag
from
	FunctionAccess as fa
	left join Definition as d on fa.funcID = d.id
		left join chFile as f2 on d.fileID = f2.id
	left join chFile as f on fa.fileID = f.id
;
-- }])>

-- Index <([{
create index FileName on chFile (name);

create index DefName on Definition (name);

create index Alias on FunpAlias (mfp);

create index Alias2 on FunpAlias (funDecl);

create index CalleeName on FunctionCall (name);
-- }])>

-- Triggger <([{
-- Delete trigger note: all can be indexed directely or indirectely by chFile::id. But to Definition fold, two additional trigger must be set up.
create trigger DelDefinition after delete on Definition
begin
	delete from FunctionCall where callerID = old.id;
	delete from Offsetof where structID = old.id;
	delete from FunctionAccess where funcID = old.id;
	delete from FunctionPattern where funcID = old.id;
end;

create trigger DelFile after delete on chFile
begin
	delete from FileDependence where chID = old.id;
	delete from Definition where fileID = old.id;
	delete from Ifdef where fileID = old.id;
	delete from FunpAlias where fileID = old.id;
end;
-- }])>
