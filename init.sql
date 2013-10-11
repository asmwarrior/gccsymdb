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
	canUpdateFile boolean
);
insert into ProjectOverview values ("1.0", "X.0", "4.6.X", "3.6.X", "/project/root/path/", 0, 0, "user data", 't');

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
-- Search file-definition pair.
create view FileSymbol as
select * from
(
select
	f.id as fileID, f.name as fileName, fileOffset, d.id as defID, d.name as defName, flag
from
	chFile f, Definition as d
where
	d.fileID = f.id
);

-- Search function-call relationship, query column calleName if you only know func-decl.
-- Note: It's possible that FunpAlias hasn't a mfp record which the mfp exists in FunctionCall -- your function calls a mfp but forget to assigns it to some func-decl? The view should use left join to detect the case.
create view CallRelationship as
select * from
(
select
	f.id as callerFileID, f.name as callerFileName, d.fileOffset as callerFileOffset, d.id as callerID, d.name as callerName,
	fc.fileID as calleePositionFileID, fc.fileOffset as calleePosition, fa.fundecl as calleeName, fa.mfp as mfp
from
	FunctionCall fc, FunpAlias fa, chFile f, Definition as d
where
	fc.callerID = d.id and fc.name = fa.mfp and d.fileID = f.id
union all
select
	f.id as callerFileID, f.name as callerFileName, d.fileOffset as callerFileOffset, d.id as callerID, d.name as callerName,
	fc.fileId as calleePositionFileID, fc.fileOffset as calleePosition, fc.name as calleeName, '-' as mfp
from
	FunctionCall fc, chFile f, Definition as d
where
	fc.callerID = d.id and d.fileID = f.id
);

-- Note: It's possible that FunpAlias has a record but you can't find it in Defintion, maybe you assign the mfp to an assemble-entry.
create view MfpJumpto as
select * from
(
select
	fa.fileID as mfpFileID, f2.name as mfpFileName, fa.offset as mfpOffset, fa.mfp as mfp,
	f.id as funcFileID, f.name as funcFileName, d.fileOffset as funcOffset, fa.funDecl as funcName
from
	FunpAlias fa, chFile f, chFile f2, Definition as d
where
	fa.funDecl = d.name and d.fileID = f.id and fa.fileID = f2.id
);
-- }])>

-- Index <([{
create index FileName on chFile (name);

create index DefName on Definition (name);

create index Alias on FunpAlias (mfp);
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
